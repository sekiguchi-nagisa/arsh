/*
 * Copyright (C) 2026 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "input.h"
#include "matcher.h"
#include "misc/inlined_stack.hpp"
#include "misc/rtti.hpp"
#include "regex.h"

namespace arsh::regex {

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

enum class BacktrackOp : unsigned char {
  None,
  SetIns,
  SetCapture,
};

union Backtrack {
  BacktrackOp op;

  union {
    BacktrackOp op;
    uint32_t target;
    const char *iter;
  } setIns;

  union {
    BacktrackOp op;
    uint32_t index;
    Capture capture;
  } setCapture;

private:
  explicit Backtrack(BacktrackOp op) : op(op) {}

public:
  Backtrack() : op(BacktrackOp::None) {}

  static Backtrack newSetIns(const Input &input, uint32_t target) {
    Backtrack bt(BacktrackOp::SetIns);
    bt.setIns.iter = input.getIter();
    bt.setIns.target = target;
    return bt;
  }

  static Backtrack newSetCapture(uint32_t index, Capture capture) {
    Backtrack bt(BacktrackOp::SetCapture);
    bt.setCapture.index = index;
    bt.setCapture.capture = capture;
    return bt;
  }
};

class BacktrackStack {
private:
  static_assert(std::is_trivially_copy_constructible_v<Backtrack>);
  const Inst *const begin;
  InlinedStack<Backtrack, 3> bts;

public:
  explicit BacktrackStack(const Inst *begin) : begin(begin) {}

  const Inst *getBeginInst() const { return this->begin; }

  bool push(Backtrack bt) {
    if (unlikely(this->bts.size() == Regex::MAX_STACK_DEPTH)) {
      return false;
    }
    this->bts.push(bt);
    return true;
  }

  bool backtrack(const Inst *&inst, Input &input, FlexBuffer<Capture> &captures) {
    while (this->bts.size()) {
      auto &bt = this->bts.back();
      switch (bt.op) {
      case BacktrackOp::None:
        this->bts.pop();
        return true; // do nothing
      case BacktrackOp::SetIns:
        inst = this->begin + bt.setIns.target;
        input.setIter(bt.setIns.iter);
        this->bts.pop();
        return true;
      case BacktrackOp::SetCapture:
        captures[bt.setCapture.index] = bt.setCapture.capture;
        this->bts.pop();
        break;
      }
    }
    return false;
  }
};

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return MatchStatus::STACK_LIMIT;                                                             \
    }                                                                                              \
  } while (false)

#define vmdispatch(op) switch (op)
#define vmcase(OP) case OpCode::OP:
#define vmnext continue;

MatchStatus match(const Regex &regex, const StringRef text, FlexBuffer<Capture> &captures) {
  // prepare
  Input input;
  if (auto s = Input::create(text, input); s == Input::Status::TOO_LARGE) {
    return MatchStatus::INPUT_LIMIT;
  } else if (s == Input::Status::INVALID_UTF8) {
    return MatchStatus::INVALID_UTF8;
  }
  const char *oldIter = input.getIter();
  const Inst *inst = regex.getInstSeq().data();
  captures.resize(regex.getCaptureGroupCount() + 1);
  BacktrackStack bts(inst);
  bts.push(Backtrack()); // push dummy

  // match
BACKTRACK:
  while (bts.backtrack(inst, input, captures)) {
    while (true) {
      vmdispatch(inst->op) {
        vmcase(Nop) {
          inst += sizeof(NopIns);
          vmnext;
        }
        vmcase(Match) {
          captures[0].offset = oldIter - input.getBegin();
          captures[0].size = input.getIter() - oldIter;
          return MatchStatus::OK;
        }
        vmcase(Jump) {
          auto &ins = cast<JumpIns>(*inst);
          inst = bts.getBeginInst() + ins.getTarget();
          vmnext;
        }
        vmcase(Alt) {
          auto &ins = cast<AltIns>(*inst);
          TRY(bts.push(Backtrack::newSetIns(input, ins.getSecond())));
          inst += sizeof(AltIns);
          vmnext;
        }
        vmcase(Start) {
          auto &ins = cast<StartIns>(*inst);
          if (input.isBegin() || (ins.multiline && isLineTerminator(input.prev()))) {
            inst += sizeof(StartIns);
            vmnext;
          }
          goto BACKTRACK;
        }
        vmcase(End) {
          auto &ins = cast<EndIns>(*inst);
          if (input.isEnd() || (ins.multiline && isLineTerminator(input.cur()))) {
            inst += sizeof(EndIns);
            vmnext;
          }
          goto BACKTRACK;
        }
        vmcase(Any) {
          if (input.available()) {
            input.consumeForward();
            inst += sizeof(AnyIns);
            vmnext;
          }
          goto BACKTRACK;
        }
        vmcase(AnyExceptNL) {
          if (input.available()) {
            int codePoint = input.consumeForward();
            if (!isLineTerminator(codePoint)) {
              inst += sizeof(AnyExceptNLIns);
              vmnext;
            }
          }
          goto BACKTRACK;
        }
        vmcase(Char) {
          auto &ins = cast<CharIns>(*inst);
          if (input.available() && input.consumeForward() == ins.getCodePoint()) {
            inst += sizeof(CharIns);
            vmnext;
          }
          goto BACKTRACK;
        }
      }
    }
  }
  // increment input and redo until end-of-input. TODO: remove
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();
    oldIter = input.getIter();
    inst = bts.getBeginInst();
    captures.clear();
    captures.resize(regex.getCaptureGroupCount() + 1);
    bts.push(Backtrack()); // dummy
    goto BACKTRACK;
  }
  return MatchStatus::FAIL;
}

} // namespace arsh::regex