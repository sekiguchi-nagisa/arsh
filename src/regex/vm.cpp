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
#include "unicode/case_fold.h"

namespace arsh::regex {

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

struct LoopState {
  uint16_t count{0};
  uint32_t inputOffset{0};

  LoopState() = default;
};

enum class BacktrackOp : unsigned char {
  None,
  SetIns,
  SetCapture,
  SetLoopState,
  NonGreedyLoop,
};

union Backtrack {
  BacktrackOp op;

  struct {
    BacktrackOp op;
    uint32_t target;
    const char *iter;
  } setIns;

  struct {
    BacktrackOp op;
    uint32_t index;
    Capture capture;
  } setCapture;

  struct {
    BacktrackOp op;
    uint16_t loopIndex;
    LoopState state;
  } setLoopState;

  struct {
    BacktrackOp op;
    uint16_t loopIndex;
    LoopState state;
  } setNonGreedyLoop;

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

  static Backtrack newSetLoopState(uint16_t loopIndex, LoopState state) {
    Backtrack bt(BacktrackOp::SetLoopState);
    bt.setLoopState.loopIndex = loopIndex;
    bt.setLoopState.state = state;
    return bt;
  }

  static Backtrack newNonGreedyLoop(uint16_t loopIndex, LoopState state) {
    Backtrack bt(BacktrackOp::NonGreedyLoop);
    bt.setNonGreedyLoop.loopIndex = loopIndex;
    bt.setNonGreedyLoop.state = state;
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

  bool backtrack(const Inst *&inst, Input &input, FlexBuffer<Capture> &captures,
                 FlexBuffer<LoopState> &loopStates) {
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
      case BacktrackOp::SetLoopState:
        loopStates[bt.setLoopState.loopIndex] = bt.setLoopState.state;
        this->bts.pop();
        break;
      case BacktrackOp::NonGreedyLoop: {
        const auto loopIndex = bt.setNonGreedyLoop.loopIndex;
        loopStates[loopIndex] = bt.setNonGreedyLoop.state;
        this->bts.pop();
        auto extra = this->bts.back();
        assert(extra.op == BacktrackOp::SetIns);
        inst = this->begin + extra.setIns.target;
        input.setIter(extra.setIns.iter);
        this->bts.pop();
        inst += sizeof(BeginLoopIns); // goto loop body
        return this->prepareLoopBody(input, loopIndex, loopStates[loopIndex]);
      }
      }
    }
    return false;
  }

  bool prepareLoopBody(const Input &input, uint16_t loopIndex, LoopState &loop) {
    if (!this->push(Backtrack::newSetLoopState(loopIndex, loop))) {
      return false;
    }
    loop.count++;
    loop.inputOffset = input.getOffset();
    return true;
  }

  bool prepareNonGreedyLoop(const Input &input, const Inst *beginInst, const LoopState &loop) {
    return this->push(Backtrack::newSetIns(input, beginInst - this->getBeginInst())) &&
           this->push(
               Backtrack::newNonGreedyLoop(cast<BeginLoopIns>(*beginInst).getLoopIndex(), loop));
  }
};

static Capture resolveNamedBackRef(const NamedCaptureEntry &entry,
                                   const FlexBuffer<Capture> &captures) {
  if (entry.hasMultipleIndices()) {
    for (unsigned int i = 0; i < entry.getSize(); i++) {
      unsigned int capIndex = entry[i];
      if (auto cap = captures[capIndex]) {
        return cap;
      }
    }
    return {};
  }
  return captures[entry.getIndex()];
}

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
  const auto matchers = regex.getMatchers();
  captures.clear();
  captures.resize(regex.getCaptureGroupCount() + 1);
  FlexBuffer<LoopState> loopStates;
  loopStates.resize(regex.getLoopCount());
  BacktrackStack bts(inst);
  bts.push(Backtrack()); // push dummy

  // match
BACKTRACK:
  while (bts.backtrack(inst, input, captures, loopStates)) {
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
        vmcase(Word) {
          const bool invert = cast<WordIns>(*inst).invert;
          const bool prevIsWord = !input.isBegin() && isWord(input.prev());
          const bool curIsWord = !input.isEnd() && isWord(input.cur());
          if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
            inst += sizeof(WordIns);
            vmnext;
          }
          goto BACKTRACK;
        }
        vmcase(IWord) {
          const bool invert = cast<IWordIns>(*inst).invert;
          const bool prevIsWord = !input.isBegin() && isWord(doSimpleCaseFolding(input.prev()));
          const bool curIsWord = !input.isEnd() && isWord(doSimpleCaseFolding(input.cur()));
          if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
            inst += sizeof(IWordIns);
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
        vmcase(IChar) {
          auto &ins = cast<ICharIns>(*inst);
          if (input.available() &&
              doSimpleCaseFolding(input.consumeForward()) == ins.getCodePoint()) {
            inst += sizeof(ICharIns);
            vmnext;
          }
          goto BACKTRACK;
        }
        vmcase(CharSet) {
          auto &ins = cast<CharSetIns>(*inst);
          if (input.available()) {
            bool s = matchers[ins.getMatcherIndex()].contains(input.consumeForward());
            if (ins.invert) {
              s = !s;
            }
            if (s) {
              inst += sizeof(CharSetIns);
              vmnext
            }
          }
          goto BACKTRACK;
        }
        vmcase(BeginCapture) {
          auto &ins = cast<BeginCaptureIns>(*inst);
          captures[ins.getCaptureIndex()] = {.offset = input.getOffset(), .size = 0};
          TRY(bts.push(Backtrack::newSetCapture(ins.getCaptureIndex(), Capture())));
          inst += sizeof(BeginCaptureIns);
          vmnext;
        }
        vmcase(EndCapture) {
          auto &ins = cast<EndCaptureIns>(*inst);
          captures[ins.getCaptureIndex()].size =
              input.getOffset() - captures[ins.getCaptureIndex()].offset;
          inst += sizeof(EndCaptureIns);
          vmnext;
        }
        vmcase(ResetCaptures) {
          auto &ins = cast<ResetCapturesIns>(*inst);
          const auto last = ins.getLastIndex();
          for (uint16_t i = ins.getFirstIndex(); i <= last; i++) {
            TRY(bts.push(Backtrack::newSetCapture(i, captures[i]))); // save original capture
            captures[i] = Capture();
          }
          inst += sizeof(ResetCapturesIns);
          vmnext
        }
        vmcase(BackRef) {
          auto &ins = cast<BackRefIns>(*inst);
          Capture capture;
          if (ins.named) {
            capture = resolveNamedBackRef(
                regex.getNamedCaptureGroups().toArrayRef()[ins.getRefIndex()].second, captures);
          } else {
            capture = captures[ins.getRefIndex()];
          }
          if (capture) {
            StringRef ref(input.getBegin() + capture.offset, capture.size);
            if (!input.expectPrefix(ref)) {
              goto BACKTRACK;
            }
          }
          inst += sizeof(BackRefIns);
          vmnext;
        }
        vmcase(IBackRef) {
          auto &ins = cast<IBackRefIns>(*inst);
          Capture capture;
          if (ins.named) {
            capture = resolveNamedBackRef(
                regex.getNamedCaptureGroups().toArrayRef()[ins.getRefIndex()].second, captures);
          } else {
            capture = captures[ins.getRefIndex()];
          }
          if (capture) {
            const StringRef ref(input.getBegin() + capture.offset, capture.size);
            const char *end = ref.end();
            for (const char *iter = ref.begin(); iter != end;) {
              int codePoint;
              unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint);
              assert(byteSize);
              iter += byteSize;
              codePoint = doSimpleCaseFolding(codePoint);
              if (!input.available() || codePoint != doSimpleCaseFolding(input.consumeForward())) {
                goto BACKTRACK;
              }
            }
          }
          inst += sizeof(IBackRefIns);
          vmnext;
        }
        vmcase(BeginLoop) {
          loopStates[cast<BeginLoopIns>(*inst).getLoopIndex()] = LoopState();
          goto LOOP;
        }
        vmcase(EndLoop) {
          inst = bts.getBeginInst() + cast<EndLoopIns>(*inst).getTarget();
        LOOP:
          auto &loopIns = cast<BeginLoopIns>(*inst);
          auto &loop = loopStates[loopIns.getLoopIndex()];
          if (loop.inputOffset == input.getOffset() && loop.count > loopIns.getMin()) {
            goto BACKTRACK; // after minimum repeat, if not consume input, backtrack
          }
          if (const auto count = loop.count; count < loopIns.getMin()) {
            TRY(bts.prepareLoopBody(input, loopIns.getLoopIndex(), loop));
            inst += sizeof(BeginLoopIns);
          } else if (count == loopIns.getMax()) {
            inst = bts.getBeginInst() + loopIns.getOuter();
          } else if (loopIns.greedy) {
            TRY(bts.push(Backtrack::newSetIns(input, loopIns.getOuter())));
            TRY(bts.prepareLoopBody(input, loopIns.getLoopIndex(), loop));
            inst += sizeof(BeginLoopIns);
          } else {
            TRY(bts.prepareNonGreedyLoop(input, inst, loop));
            inst = bts.getBeginInst() + loopIns.getOuter();
          }
          vmnext;
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