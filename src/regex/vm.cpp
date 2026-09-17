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

#include "instruction.h"
#include "match_context.h"
#include "matcher.h"
#include "misc/rtti.hpp"
#include "regex.h"
#include "unicode/case_fold.h"
#include "unicode/grapheme.h"

#include "vm_common.h"
#include <cstdio>

namespace arsh::regex {

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return MatchStatus::STACK_LIMIT;                                                             \
    }                                                                                              \
  } while (false)

#ifndef ARSH_USE_TAILCALL_VM

#ifdef __GNUC__
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

MatchStatus match(MatchContext &ctx, ObserverPtr<Timer> timer) {
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
  std::string foldBuf;
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

START:
  // search string
  if (inst->op == OpCode::Char || inst->op == OpCode::String) {
    char data[4];
    StringRef needle;
    if (inst->op == OpCode::Char) {
      int codePoint = cast<CharIns>(*inst).getCodePoint();
      unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data);
      needle = StringRef(data, len);
      inst += sizeof(CharIns);
    } else {
      needle = matchers[cast<StringIns>(*inst).getIndex()].asStrRef();
      inst += sizeof(StringIns);
    }
    if (auto retPos = input.remainForward().find(needle); retPos == StringRef::npos) {
      oldIter = input.getEnd();
      goto BACKTRACK;
    } else {
      oldIter = input.getIter() + retPos;
      input.setIter(input.getIter() + retPos + needle.size());
    }
  } else if (inst->op == OpCode::CharSet) {
    const unsigned int index = cast<CharSetIns>(*inst).getMatcherIndex();
    const bool invert = cast<CharSetIns>(*inst).invert;
    bool matched = false;
    inst += sizeof(CharSetIns);
    while (input.available()) {
      oldIter = input.getIter();
      if (matchers[index].contains(input.consumeForward()) != invert) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      goto BACKTRACK;
    }
  }

  // match
  bts.push(Backtrack::dummy()); // dummy
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
            iterateGraphemeUntil(ref, 1, true, [&byteSize](const GraphemeCluster &grapheme) {
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
            /**
             * contain==true, invert==true => false
             * contain==true, invert==false => true
             * contain==false, invert==true => true
             * contain==false, invert==false => false
             */
            if (matchers[ins.getMatcherIndex()].contains(input.consumeForward()) != ins.invert) {
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
                         doSimpleCaseFolding(input.consumeForward())) != ins.invert;
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
            if (matchers[ins.getMatcherIndex()].contains(codePoint) != ins.invert) {
              inst += sizeof(LBCharSetIns);
              vmnext;
            }
          }
          goto BACKTRACK;
        }
        vmcase(PrepareRadix) {
          inst += sizeof(PrepareRadixIns);
          auto &radixIns = cast<RadixOrEmojiIns>(*inst);
          unsigned int codePointCount = 0;
          if (radixIns.hasEmoji()) {
            codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
          }
          if (radixIns.hasRadix) {
            codePointCount = std::max<unsigned int>(
                codePointCount, matchers[radixIns.getIndex()].asRadixTree().getMaxCodePointCount());
          }
          unsigned int size = input.remainForwardOfCodePoints(codePointCount).size();
          TRY(bts.push(Backtrack::newRadixState(size)));
          goto RADIX_OR_EMOJI;
        }
        vmcase(RadixOrEmoji) {
          {
            StringRef ref(input.getIter(), bts.getRadixState());
            unsafeRemoveSuffixUtf8(ref);
            bts.updateRadixState(ref.size());
          }
        RADIX_OR_EMOJI:
          auto &radixIns = cast<RadixOrEmojiIns>(*inst);
          if (const auto strSize = bts.getRadixState(); strSize && input.available()) {
            const StringRef ref(input.getIter(), strSize);
            const auto nextOffset = radixIns.nextOffset;
            unsigned int consumedSize = 0;
            if (radixIns.hasEmoji()) {
              auto [s, p] =
                  findLongestMatched(ucp::getEmojiTrie(), ref, foldBuf, radixIns.ignoreCase());
              if (p && hasFlag(toUnderlying(radixIns.emoji), p)) {
                consumedSize = s;
              }
            }
            if (radixIns.hasRadix) {
              auto [s, p] = findLongestMatched(matchers[radixIns.getIndex()].asRadixTree(), ref,
                                               foldBuf, radixIns.ignoreCase());
              if (p) {
                consumedSize = std::max<unsigned int>(consumedSize, s);
              }
            }
            if (consumedSize) {
              bts.updateRadixState(consumedSize);
              TRY(bts.push(Backtrack::newSetIns(input, inst - bts.getStartInst())));
              input.setIter(input.getIter() + consumedSize);
              inst += sizeof(RadixOrEmojiIns) + nextOffset;
              vmnext;
            }
            if (nextOffset) {
              inst += sizeof(RadixOrEmojiIns);
              vmnext; // try next
            }
          }
          goto BACKTRACK;
        }
        vmcase(PrepareLBRadix) {
          inst += sizeof(PrepareLBRadixIns);
          auto &radixIns = cast<LBRadixOrEmojiIns>(*inst);
          unsigned int codePointCount = 0;
          if (radixIns.hasEmoji()) {
            codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
          }
          if (radixIns.hasRadix) {
            codePointCount = std::max<unsigned int>(
                codePointCount, matchers[radixIns.getIndex()].asRadixTree().getMaxCodePointCount());
          }
          unsigned int size = input.remainBackwardOfCodePoints(codePointCount).size();
          TRY(bts.push(Backtrack::newRadixState(size)));
          goto LBRADIX_OR_EMOJI;
        }
        vmcase(LBRadixOrEmoji) {
          {
            StringRef ref(input.getIter() - bts.getRadixState(), bts.getRadixState());
            unsafeRemovePrefixUtf8(ref);
            bts.updateRadixState(ref.size());
          }
        LBRADIX_OR_EMOJI:
          auto &radixIns = cast<LBRadixOrEmojiIns>(*inst);
          if (const auto strSize = bts.getRadixState(); strSize && input.availableBackward()) {
            const StringRef ref(input.getIter() - strSize, strSize);
            const auto nextOffset = radixIns.nextOffset;
            unsigned int consumedSize = 0;
            if (radixIns.hasEmoji()) {
              auto [s, p] = findBackwardLongestMatched(ucp::getEmojiTrie(), ref, foldBuf,
                                                       radixIns.ignoreCase());
              if (p && hasFlag(toUnderlying(radixIns.emoji), p)) {
                consumedSize = s;
              }
            }
            if (radixIns.hasRadix) {
              auto [s, p] = findBackwardLongestMatched(matchers[radixIns.getIndex()].asRadixTree(),
                                                       ref, foldBuf, radixIns.ignoreCase());
              if (p) {
                consumedSize = std::max<unsigned int>(consumedSize, s);
              }
            }
            if (consumedSize) {
              bts.updateRadixState(consumedSize);
              TRY(bts.push(Backtrack::newSetIns(input, inst - bts.getStartInst())));
              input.setIter(input.getIter() - consumedSize);
              inst += sizeof(LBRadixOrEmojiIns) + nextOffset;
              vmnext;
            }
            if (nextOffset) {
              inst += sizeof(LBRadixOrEmojiIns);
              vmnext; // try next
            }
          }
          goto BACKTRACK;
        }
        vmcase(String) {
          if (input.expectForward(matchers[cast<StringIns>(*inst).getIndex()].asStrRef())) {
            inst += sizeof(StringIns);
            vmnext;
          }
          goto BACKTRACK;
        }
        vmcase(LBString) {
          if (input.expectBackward(matchers[cast<LBStringIns>(*inst).getIndex()].asStrRef())) {
            inst += sizeof(LBStringIns);
            vmnext;
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
          const unsigned int last = ins.getLastIndex();
          for (unsigned int i = ins.getFirstIndex(); i <= last; i++) {
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
  // increment input and redo until end-of-input.
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();
    oldIter = input.getIter();
    inst = bts.getStartInst();
    ctx.clearCaptures();
    captures = ctx.getCaptures();
    goto START;
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
}

#endif // ARSH_USE_TAILCALL_VM

} // namespace arsh::regex