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

struct Frame {
  const char *iter{nullptr};
  const Inst *inst{nullptr};

  Frame() = default;

  Frame(const Input &input, const Inst *ins) : iter(input.getIter()), inst(ins) {}
};

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define vmdispatch(op) switch (op)
#define vmcase(OP) case OpCode::OP:
#define vmnext continue;

#define CHECK_STACK_DEPTH(ss)                                                                      \
  do {                                                                                             \
    if (unlikely((ss).size() == Regex::MAX_STACK_DEPTH)) {                                         \
      return MatchStatus::STACK_LIMIT;                                                             \
    }                                                                                              \
  } while (false)

MatchStatus match(const Regex &regex, const StringRef text, FlexBuffer<Capture> &captures) {
  // prepare
  Input input;
  if (auto s = Input::create(text, input); s == Input::Status::TOO_LARGE) {
    return MatchStatus::INPUT_LIMIT;
  } else if (s == Input::Status::INVALID_UTF8) {
    return MatchStatus::INVALID_UTF8;
  }
  const char *oldIter = input.getIter();
  const Inst *const startIns = regex.getInstSeq().data();
  captures.resize(regex.getCaptureGroupCount() + 1);
  InlinedStack<Frame, 3> frames;
  frames.push(Frame(input, startIns));

  // match
BACKTRACK:
  while (frames.size()) {
    const Inst *inst = frames.back().inst;
    input.setIter(frames.back().iter);
    frames.pop();
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
          inst = startIns + ins.getTarget();
          vmnext;
        }
        vmcase(Alt) {
          CHECK_STACK_DEPTH(frames);
          auto &ins = cast<AltIns>(*inst);
          frames.push(Frame(input, startIns + ins.getSecond())); // push second branch addr
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
  // increment input and redo until end-of-input
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();
    captures.clear();
    captures.resize(regex.getCaptureGroupCount() + 1);
    oldIter = input.getIter();
    frames.push(Frame(input, startIns));
    goto BACKTRACK;
  }
  return MatchStatus::FAIL;
}

} // namespace arsh::regex