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
#include "misc/format.hpp"
#include "misc/rtti.hpp"
#include "regex.h"
#include "unicode/case_fold.h"
#include "unicode/grapheme.h"

namespace arsh::regex {

const char *toString(const MatchStatus s) {
  switch (s) {
  case MatchStatus::OK:
    break;
  case MatchStatus::FAIL:
    return "failed";
  case MatchStatus::INVALID_UTF8:
    return "input string is invalid UTF-8";
  case MatchStatus::INPUT_LIMIT:
    return "size of input string is too large";
  case MatchStatus::CANCEL:
    return "canceled";
  case MatchStatus::TIMEOUT:
    return "match timeout";
  case MatchStatus::STACK_LIMIT:
    return "stack depth reaches limit";
  case MatchStatus::INVALID_REPLACE_PATTERN:
  case MatchStatus::REPLACED_LIMIT:
    break;
  }
  return "";
}

struct LoopState {
  uint32_t count{0};
  uint32_t inputOffset{0};
};

enum class BacktrackOp : unsigned char {
  None,
  SetIns,
  SetCapture,
  SetLoopState,
  NonGreedyLoop,
  LookAround,
};

union Backtrack {
  BacktrackOp op;

  struct {
    BacktrackOp op; // NOLINT
    uint32_t target;
    const char *iter;
  } setIns;

  struct {
    BacktrackOp op; // NOLINT
    uint32_t index;
    Capture capture;
  } setCapture;

  struct {
    BacktrackOp op; // NOLINT
    uint16_t loopIndex;
    LoopState state;
  } setLoopState;

  struct {
    BacktrackOp op; // NOLINT
    uint16_t loopIndex;
    LoopState state;
  } nonGreedyLoop;

  struct {
    BacktrackOp op; // NOLINT
    bool negate;
    bool matched;
    uint32_t target;
    const char *iter;
  } lookAround;

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
    bt.nonGreedyLoop.loopIndex = loopIndex;
    bt.nonGreedyLoop.state = state;
    return bt;
  }

  static Backtrack newLookAround(const Input &input, uint32_t target, bool negate) {
    Backtrack bt(BacktrackOp::LookAround);
    bt.lookAround.negate = negate;
    bt.lookAround.matched = !negate;
    bt.lookAround.iter = input.getIter();
    bt.lookAround.target = target;
    return bt;
  }
};

class BacktrackStack {
private:
  static_assert(std::is_trivially_copy_constructible_v<Backtrack>);
  const Inst *const start;
  std::vector<Backtrack> bts;

public:
  explicit BacktrackStack(const Inst *start) : start(start) {}

  const Inst *getStartInst() const { return this->start; }

  bool push(Backtrack bt) {
    if (unlikely(this->bts.size() == Regex::MAX_STACK_DEPTH)) {
      return false;
    }
    this->bts.push_back(bt);
    return true;
  }

  bool backtrack(const Inst *&inst, Input &input, Capture *captures, LoopState *loopStates) {
    while (this->bts.size()) {
      auto bt = this->bts.back();
      this->bts.pop_back();
      switch (bt.op) {
      case BacktrackOp::None:
        return true; // do nothing
      case BacktrackOp::SetIns:
        inst = this->getStartInst() + bt.setIns.target;
        input.setIter(bt.setIns.iter);
        return true;
      case BacktrackOp::SetCapture:
        captures[bt.setCapture.index] = bt.setCapture.capture;
        break;
      case BacktrackOp::SetLoopState:
        loopStates[bt.setLoopState.loopIndex] = bt.setLoopState.state;
        break;
      case BacktrackOp::NonGreedyLoop: {
        const auto loopIndex = bt.nonGreedyLoop.loopIndex;
        loopStates[loopIndex] = bt.nonGreedyLoop.state;
        const auto extra = this->bts.back();
        this->bts.pop_back();
        assert(extra.op == BacktrackOp::SetIns);
        inst = this->getStartInst() + extra.setIns.target;
        input.setIter(extra.setIns.iter);
        inst += sizeof(BeginLoopIns); // goto loop body
        return this->prepareLoopBody(input, loopIndex, loopStates[loopIndex]);
      }
      case BacktrackOp::LookAround:
        inst = this->getStartInst() + bt.lookAround.target;
        bt.lookAround.matched = bt.lookAround.negate; // if negative lookaround, matched
        return this->push(bt);
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

  bool prepareGreedyLoop(const Input &input, const BeginLoopIns &loopIns, LoopState &loop) {
    return this->push(Backtrack::newSetIns(input, loopIns.getOuter())) &&
           this->prepareLoopBody(input, loopIns.getLoopIndex(), loop);
  }

  bool prepareNonGreedyLoop(const Input &input, const Inst *beginInst, const LoopState &loop) {
    return this->push(Backtrack::newSetIns(input, beginInst - this->getStartInst())) &&
           this->push(
               Backtrack::newNonGreedyLoop(cast<BeginLoopIns>(*beginInst).getLoopIndex(), loop));
  }

  bool cleanupLookAround(Input &input, Capture *captures) {
    // find original lookaround state
    bool negate = false;
    for (ssize_t i = static_cast<ssize_t>(this->bts.size()) - 1; i > -1; i--) {
      if (auto &bt = this->bts[i]; bt.op == BacktrackOp::LookAround) {
        negate = bt.lookAround.negate;
        break;
      }
    }

    while (this->bts.size() && this->bts.back().op != BacktrackOp::LookAround) {
      if (negate && this->bts.back().op == BacktrackOp::SetCapture) {
        auto bt = this->bts.back();
        captures[bt.setCapture.index] = bt.setCapture.capture; // force reset capture
      }
      this->bts.pop_back();
    }
    assert(this->bts.size());
    auto bt = this->bts.back();
    input.setIter(bt.lookAround.iter);
    this->bts.pop_back();
    return bt.lookAround.matched;
  }
};

class MatchContext {
private:
  const Regex &regex;
  Input &input;
  std::vector<Capture> &captures;
  std::vector<LoopState> loops;

public:
  MatchContext(const Regex &regex, Input &input, std::vector<Capture> &captures)
      : regex(regex), input(input), captures(captures) {
    this->loops.resize(this->regex.getLoopCount());
  }

  const Regex &getRegex() const { return this->regex; }

  Capture *getCaptures() const { return this->captures.data(); }

  LoopState *getLoops() { return this->loops.data(); }

  Input copyInput() const { return this->input; }

  void syncInput(const Input &in) { this->input = in; }

  const Inst *getInst() const { return this->regex.getInstSeq().data(); }

  void clearCaptures() const {
    this->captures.clear();
    this->captures.resize(this->regex.getCaptureGroupCount() + 1);
  }

  ArrayRef<Matcher> getMatchers() const { return this->regex.getMatchers(); }

  Capture resolveNamedBackRef(unsigned int refIndex) const {
    auto &entry = this->regex.getNamedCaptureGroups().toArrayRef()[refIndex].second;
    return this->resolveNamedBackRef(entry);
  }

  Capture resolveNamedBackRef(const NamedCaptureEntry &entry) const {
    if (entry.hasMultipleIndices()) {
      for (unsigned int i = 0; i < entry.getSize(); i++) {
        unsigned int capIndex = entry[i];
        if (auto cap = this->captures[capIndex]) {
          return cap;
        }
      }
      return {};
    }
    return this->captures[entry.getIndex()];
  }
};

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return MatchStatus::STACK_LIMIT;                                                             \
    }                                                                                              \
  } while (false)

#if defined(__GNUC__)
#define USE_THREADED_CODE
#else
#endif

#ifdef USE_THREADED_CODE
#define vmdispatch(op) goto *jumpTable[toUnderlying(op)];
#define vmcase(OP) L_##OP:
#define vmnext vmdispatch(inst->op)
#else
#define vmdispatch(op) switch (op)
#define vmcase(OP) case OpCode::OP:
#define vmnext continue
#endif

static MatchStatus match(MatchContext &ctx, ObserverPtr<Timer> timer) {
  // prepare
  Input input = ctx.copyInput();
  const char *oldIter = input.getIter();
  const Inst *inst = ctx.getInst();
  const auto matchers = ctx.getMatchers();
  LoopState *loopStates = ctx.getLoops();
  ctx.clearCaptures();
  Capture *captures = ctx.getCaptures();
  unsigned int btCount = 0;
  BacktrackStack bts(inst);
  bts.push(Backtrack()); // push dummy
  if (timer) {
    timer->start();
  }

#ifdef USE_THREADED_CODE
  static const void *jumpTable[] = {
#define GEN_TABLE(E) &&L_##E,
      EACH_RE_OPCODE(GEN_TABLE)
#undef GEN_TABLE
  };
#endif

  // match
BACKTRACK:
  while (bts.backtrack(inst, input, captures, loopStates)) {
    if (unlikely(++btCount == Regex::TIMER_CHECK_INTERVAL)) {
      btCount = 0;
      if (timer) {
        switch (timer->check()) {
        case Timer::Status::None:
          break;
        case Timer::Status::Canceled:
          return MatchStatus::CANCEL;
        case Timer::Status::Expired:
          return MatchStatus::TIMEOUT;
        }
      }
    }

    while (true) {
      vmdispatch(inst->op) {
        vmcase(Nop) {
          inst += sizeof(NopIns);
          vmnext;
        }
        vmcase(Match) {
          captures[0].offset = oldIter - input.getBegin();
          captures[0].size = input.getIter() - oldIter;
          ctx.syncInput(input);
          return MatchStatus::OK;
        }
        vmcase(Jump) {
          auto &ins = cast<JumpIns>(*inst);
          inst = bts.getStartInst() + ins.getTarget();
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
          const bool prevIsWord = !input.isBegin() && isExtendWord(input.prev());
          const bool curIsWord = !input.isEnd() && isExtendWord(input.cur());
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
        vmcase(LBAny) {
          if (input.availableBackward()) {
            int codePoint = input.consumeBackward();
            if (cast<LBAnyIns>(*inst).dotAll || !isLineTerminator(codePoint)) {
              inst += sizeof(LBAnyIns);
              vmnext;
            }
          }
          goto BACKTRACK;
        }
        vmcase(Grapheme) {
          if (input.available()) {
            StringRef ref = input.remainForward();
            size_t byteSize = 0;
            iterateGraphemeUntil(ref, 1, [&byteSize](const GraphemeCluster &grapheme) {
              byteSize = grapheme.getRef().size();
            });
            input.setIter(input.getIter() + byteSize);
            inst += sizeof(GraphemeIns);
            vmnext;
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
        vmcase(LBChar) {
          auto &ins = cast<LBCharIns>(*inst);
          if (input.availableBackward()) {
            int codePoint = input.consumeBackward();
            if (ins.ignoreCase) {
              codePoint = doSimpleCaseFolding(codePoint);
            }
            if (codePoint == ins.getCodePoint()) {
              inst += sizeof(LBCharIns);
              vmnext;
            }
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
              vmnext;
            }
          }
          goto BACKTRACK;
        }
        vmcase(ICharSet) {
          auto &ins = cast<ICharSetIns>(*inst);
          if (input.available()) {
            bool s = matchers[ins.getMatcherIndex()].contains(
                doSimpleCaseFolding(input.consumeForward()));
            if (ins.invert) {
              s = !s;
            }
            if (s) {
              inst += sizeof(ICharSetIns);
              vmnext;
            }
          }
          goto BACKTRACK;
        }
        vmcase(LBCharSet) {
          auto &ins = cast<LBCharSetIns>(*inst);
          if (input.availableBackward()) {
            int codePoint = input.consumeBackward();
            if (ins.ignoreCase) {
              codePoint = doSimpleCaseFolding(codePoint);
            }
            bool s = matchers[ins.getMatcherIndex()].contains(codePoint);
            if (ins.invert) {
              s = !s;
            }
            if (s) {
              inst += sizeof(LBCharSetIns);
              vmnext;
            }
          }
          goto BACKTRACK;
        }
        vmcase(BeginCapture) {
          auto &ins = cast<BeginCaptureIns>(*inst);
          const unsigned int index = ins.getCaptureIndex();
          captures[index] = {.offset = input.getOffset(), .size = 0};
          TRY(bts.push(Backtrack::newSetCapture(index, Capture())));
          inst += sizeof(BeginCaptureIns);
          vmnext;
        }
        vmcase(EndCapture) {
          auto &ins = cast<EndCaptureIns>(*inst);
          const unsigned int index = ins.getCaptureIndex();
          auto &capture = captures[index];
          assert(capture.offset <= input.getOffset());
          capture.size = input.getOffset() - capture.offset;
          inst += sizeof(EndCaptureIns);
          vmnext;
        }
        vmcase(LBEndCapture) {
          auto &ins = cast<LBEndCaptureIns>(*inst);
          const unsigned int index = ins.getCaptureIndex();
          auto &capture = captures[index];
          const unsigned int actualEndOffset = capture.endOffset();
          capture.offset = input.getOffset();
          assert(capture.offset <= actualEndOffset);
          capture.size = actualEndOffset - capture.offset;
          inst += sizeof(LBEndCaptureIns);
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
          vmnext;
        }
        vmcase(BackRef) {
          auto &ins = cast<BackRefIns>(*inst);
          Capture capture;
          if (ins.named) {
            capture = ctx.resolveNamedBackRef(ins.getRefIndex());
          } else {
            capture = captures[ins.getRefIndex()];
          }
          if (capture) {
            StringRef ref(input.getBegin() + capture.offset, capture.size);
            if (!input.expectForward(ref)) {
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
            capture = ctx.resolveNamedBackRef(ins.getRefIndex());
          } else {
            capture = captures[ins.getRefIndex()];
          }
          if (capture) {
            const StringRef ref(input.getBegin() + capture.offset, capture.size);
            const char *end = ref.end();
            for (const char *iter = ref.begin(); iter != end;) {
              if (input.available() && doSimpleCaseFolding(unsafeNextUtf8(iter)) ==
                                           doSimpleCaseFolding(input.consumeForward())) {
                continue;
              }
              goto BACKTRACK;
            }
          }
          inst += sizeof(IBackRefIns);
          vmnext;
        }
        vmcase(LBBackRef) {
          auto &ins = cast<LBBackRefIns>(*inst);
          Capture capture;
          if (ins.named) {
            capture = ctx.resolveNamedBackRef(ins.getRefIndex());
          } else {
            capture = captures[ins.getRefIndex()];
          }
          if (capture) {
            const StringRef ref(input.getBegin() + capture.offset, capture.size);
            if (ins.ignoreCase) {
              const char *begin = ref.begin();
              for (const char *iter = ref.end(); iter != begin;) {
                if (input.availableBackward() && doSimpleCaseFolding(unsafePrevUtf8(iter)) ==
                                                     doSimpleCaseFolding(input.consumeBackward())) {
                  continue;
                }
                goto BACKTRACK;
              }
            } else {
              if (!input.expectBackward(ref)) {
                goto BACKTRACK;
              }
            }
          }
          inst += sizeof(LBBackRefIns);
          vmnext;
        }
        vmcase(BeginLoop) {
          loopStates[cast<BeginLoopIns>(*inst).getLoopIndex()] = LoopState();
          goto LOOP;
        }
        vmcase(EndLoop) {
          inst = bts.getStartInst() + cast<EndLoopIns>(*inst).getTarget();
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
            inst = bts.getStartInst() + loopIns.getOuter();
          } else if (loopIns.greedy) {
            TRY(bts.prepareGreedyLoop(input, loopIns, loop));
            inst += sizeof(BeginLoopIns);
          } else {
            TRY(bts.prepareNonGreedyLoop(input, inst, loop));
            inst = bts.getStartInst() + loopIns.getOuter();
          }
          vmnext;
        }
        vmcase(BeginLookAround) {
          auto &lookAround = cast<BeginLookAroundIns>(*inst);
          TRY(bts.push(Backtrack::newLookAround(input, lookAround.getTarget(), lookAround.negate)));
          inst += sizeof(BeginLookAroundIns);
          vmnext;
        }
        vmcase(EndLookAround) {
          if (bts.cleanupLookAround(input, captures)) {
            inst += sizeof(EndLookAroundIns);
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
    inst = bts.getStartInst();
    ctx.clearCaptures();
    captures = ctx.getCaptures();
    bts.push(Backtrack()); // dummy
    goto BACKTRACK;
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
}

MatchStatus match(const Regex &regex, const StringRef text, std::vector<Capture> &captures,
                  const ObserverPtr<Timer> timer) {
  Input input;
  if (auto s = Input::create(text, input); s == Input::Status::TOO_LARGE) {
    return MatchStatus::INPUT_LIMIT;
  } else if (s == Input::Status::INVALID_UTF8) {
    return MatchStatus::INVALID_UTF8;
  }
  MatchContext ctx(regex, input, captures);
  return match(ctx, timer);
}

#undef TRY
#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return MatchStatus::REPLACED_LIMIT;                                                          \
    }                                                                                              \
  } while (false)

static MatchStatus interpretReplacePattern(const MatchContext &ctx, const ReplaceParam &param) {
  for (size_t pos = 0;;) {
    auto retPos = param.replacement.find('$', pos);
    const auto sub = param.replacement.slice(pos, retPos);
    if (param.consumer) {
      TRY(param.consumer(sub));
    }
    if (retPos == StringRef::npos) {
      break;
    }
    retPos++;
    if (retPos == param.replacement.size()) { // end with '$'
      if (param.err) {
        *param.err += "invalid replace pattern: `$'";
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    StringRef inserting;
    switch (param.replacement[retPos]) {
    case '$':
      inserting = "$";
      retPos++;
      break;
    case '&':
      inserting = param.text.substr(ctx.getCaptures()[0].offset, ctx.getCaptures()[0].size);
      retPos++;
      break;
    case '`':
      inserting = param.text.substr(0, ctx.getCaptures()[0].offset);
      retPos++;
      break;
    case '\'':
      inserting = param.text.substr(ctx.getCaptures()[0].endOffset());
      retPos++;
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
      const auto startPos = retPos++;
      while (retPos < param.replacement.size() && isDecimal(param.replacement[retPos])) {
        retPos++;
      }
      StringRef num = param.replacement.slice(startPos, retPos);
      if (auto ret = convertToNum10<unsigned int>(num.begin(), num.end());
          ret && ret.value <= ctx.getRegex().getCaptureGroupCount() && ret.value > 0) {
        if (auto cap = ctx.getCaptures()[ret.value]) {
          inserting = param.text.substr(cap.offset, cap.size);
        }
        break;
      }
      if (param.err) {
        *param.err += "undefined capture group index: `";
        *param.err += num;
        *param.err += '\'';
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    case '<': {
      retPos++;
      auto p = param.replacement.find('>', retPos);
      if (p == StringRef::npos) {
        if (param.err) {
          *param.err += "replace pattern `$<' must end with `>'";
        }
        return MatchStatus::INVALID_REPLACE_PATTERN;
      }
      auto name = param.replacement.slice(retPos, p);
      if (auto *e = ctx.getRegex().getNamedCaptureGroups().find(name)) {
        if (auto cap = ctx.resolveNamedBackRef(*e)) {
          inserting = param.text.substr(cap.offset, cap.size);
        }
        retPos = p + 1;
        break;
      }
      if (param.err) {
        *param.err += "undefined capture group name: `";
        *param.err += name;
        *param.err += '\'';
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    default:
      if (param.err) {
        *param.err += "invalid replace pattern: `$'";
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    if (param.consumer) {
      TRY(param.consumer(inserting));
    }
    pos = retPos;
  }
  return MatchStatus::OK;
}

MatchStatus replace(const Regex &regex, const ReplaceParam &param, const ObserverPtr<Timer> timer) {
  Input input;
  if (auto s = Input::create(param.text, input); s == Input::Status::TOO_LARGE) {
    return MatchStatus::INPUT_LIMIT;
  } else if (s == Input::Status::INVALID_UTF8) {
    return MatchStatus::INVALID_UTF8;
  }
  std::vector<Capture> captures;
  MatchContext ctx(regex, input, captures);
  unsigned int matchStartOffset = 0;
  do {
    matchStartOffset = input.getOffset();
    if (auto s = match(ctx, timer); s == MatchStatus::OK) {
      if (param.consumer) {
        TRY(param.consumer(param.text.slice(matchStartOffset, captures[0].offset)));
      }
      if (auto s2 = interpretReplacePattern(ctx, param); s2 != MatchStatus::OK) {
        return s2;
      }
      if (input.available() && matchStartOffset == input.getOffset()) { // not consume input
        input.consumeForward();
        if (param.consumer) {
          TRY(param.consumer(StringRef(input.getBegin() + matchStartOffset,
                                       input.getOffset() - matchStartOffset)));
        }
      }
    } else if (s == MatchStatus::FAIL) {
      input.setIter(input.getBegin() + matchStartOffset);
      break;
    } else {
      return s;
    }
  } while (param.global && input.getOffset() != matchStartOffset);
  if (param.consumer) {
    TRY(param.consumer(input.remainForward()));
  }
  return MatchStatus::OK;
}

#undef TRY
#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool escape(const StringRef ref, const size_t maxSize, std::string &out) {
  const char *const end = ref.end();
  const char *iter = ref.begin();
  if (iter != end && isLetterOrDigit(*iter)) {
    char ch = *(iter++);
    char data[16];
    snprintf(data, std::size(data), "\\x%02x", ch);
    TRY(checkedAppend(data, maxSize, out));
  }
  for (; iter != end; ++iter) {
    char buf[16];
    switch (*iter) {
    case '^':
    case '$':
    case '\\':
    case '.':
    case '*':
    case '+':
    case '?':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '|':
    case '/':
      buf[0] = '\\';
      buf[1] = *iter;
      buf[2] = '\0';
      break;
    case ',':
    case '-':
    case '=':
    case '<':
    case '>':
    case '#':
    case '&':
    case '!':
    case '%':
    case ':':
    case ';':
    case '@':
    case '~':
    case '\'':
    case '`':
    case '"':
      snprintf(buf, std::size(buf), "\\x%02x", *iter);
      break;
    case '\f':
      buf[0] = '\\';
      buf[1] = 'f';
      buf[2] = '\0';
      break;
    case '\n':
      buf[0] = '\\';
      buf[1] = 'n';
      buf[2] = '\0';
      break;
    case '\r':
      buf[0] = '\\';
      buf[1] = 'r';
      buf[2] = '\0';
      break;
    case '\t':
      buf[0] = '\\';
      buf[1] = 't';
      buf[2] = '\0';
      break;
    case '\v':
      buf[0] = '\\';
      buf[1] = 'v';
      buf[2] = '\0';
      break;
    case ' ':
      buf[0] = '\\';
      buf[1] = 'x';
      buf[2] = '2';
      buf[3] = '0';
      buf[4] = '\0';
      break;
    default:
      int codePoint;
      if (unsigned int len = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint)) {
        if (ucp::hasPrimeLoneProperty(codePoint, ucp::Lone::ESRegexClassSpace)) {
          snprintf(buf, std::size(buf), "\\u%04x", codePoint);
        } else {
          memcpy(buf, iter, len);
          buf[len] = '\0';
        }
        iter += len - 1;
      } else {
        buf[0] = *iter;
        buf[1] = '\0';
      }
      break;
    }
    TRY(checkedAppend(buf, maxSize, out));
  }
  return true;
}

} // namespace arsh::regex