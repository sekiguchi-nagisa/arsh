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

#include "stencil_runtime.h"

#include <cstring>

#include "../backtrack.h"
#include "../radix_helper.h"
#include "unicode/case_fold.h"
#include "unicode/grapheme.h"
#include "unicode/property.h"

namespace arsh::regex::jit {

namespace {

/**
 * `RGIEmojiSeq::CASE_IGNORE` is not an emoji sequence, it selects case folding, so the raw flag
 * word an instruction carries encodes two independent facts (see `RadixOrEmojiIns::hasEmoji`).
 */
constexpr auto RADIX_CASE_IGNORE = toUnderlying(ucp::RGIEmojiSeq::CASE_IGNORE);

bool radixHasEmoji(const uint32_t flags) { return (flags & ~RADIX_CASE_IGNORE) != 0; }

bool radixIgnoreCase(const uint32_t flags) { return (flags & RADIX_CASE_IGNORE) != 0; }

/**
 * push the initial `RadixState` and return `JIT_ACTION_FIRST` / `JIT_ACTION_STACK_LIMIT`.
 *
 * mirrors the `PrepareRadix` / `PrepareLBRadix` prologue of the interpreter.
 */
int32_t pushRadixState(const Matcher *matchers, Input *input, BacktrackStack *bts, const bool hasEmoji,
                       const bool hasRadix, const uint16_t matcherIndex, const bool backward) {
  unsigned int codePointCount = 0;
  if (hasEmoji) {
    codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
  }
  if (hasRadix) {
    codePointCount =
        std::max<unsigned int>(codePointCount, matchers[matcherIndex].asRadixTree().getMaxCodePointCount());
  }
  const unsigned int size = backward ? input->remainBackwardOfCodePoints(codePointCount).size()
                                     : input->remainForwardOfCodePoints(codePointCount).size();
  if (!bts->push(Backtrack::newRadixState(size))) {
    return JIT_ACTION_STACK_LIMIT;
  }
  return JIT_ACTION_FIRST;
}

} // namespace

extern "C" {

int32_t arsh_jit_case_fold(const int32_t codePoint) noexcept {
  return doSimpleCaseFolding(codePoint);
}

int32_t arsh_jit_is_extend_word(const int32_t codePoint) noexcept {
  return isExtendWord(codePoint) ? 1 : 0;
}

uint32_t arsh_jit_grapheme_size(const char *data, const uint32_t size) noexcept {
  StringRef ref(data, size);
  size_t byteSize = 0;
  iterateGraphemeUntil(ref, 1, true, [&byteSize](const GraphemeCluster &grapheme) {
    byteSize = grapheme.getRef().size();
  });
  return static_cast<uint32_t>(byteSize);
}

int32_t arsh_jit_matcher_contains(const Matcher *matchers, const uint16_t index,
                                  const int32_t codePoint) noexcept {
  return matchers[index].contains(codePoint) ? 1 : 0;
}

int32_t arsh_jit_expect_forward(Input *input, const Matcher *matchers,
                                const uint16_t index) noexcept {
  return input->expectForward(matchers[index].asStrRef()) ? 1 : 0;
}

int32_t arsh_jit_expect_backward(Input *input, const Matcher *matchers,
                                 const uint16_t index) noexcept {
  return input->expectBackward(matchers[index].asStrRef()) ? 1 : 0;
}

void arsh_jit_resolve_named_backref(const MatchContext *ctx, const uint16_t refIndex,
                                    Capture *out) noexcept {
  *out = ctx->resolveNamedBackRef(refIndex);
}

int32_t arsh_jit_backref_forward(Input *input, const Capture *capture, const char *begin) noexcept {
  if (*capture) {
    return input->expectForward(StringRef(begin + capture->offset, capture->size)) ? 1 : 0;
  }
  return 1;
}

int32_t arsh_jit_ibackref_forward(Input *input, const Capture *capture, const char *begin) noexcept {
  if (*capture) {
    const StringRef ref(begin + capture->offset, capture->size);
    const char *const end = ref.end();
    for (const char *iter = ref.begin(); iter != end;) {
      if (input->available() && doSimpleCaseFolding(unsafeNextUtf8(iter)) ==
                                    doSimpleCaseFolding(input->consumeForward())) {
        continue;
      }
      return 0;
    }
  }
  return 1;
}

int32_t arsh_jit_lbbackref_backward(Input *input, const Capture *capture, const char *begin,
                                    const int32_t ignoreCase) noexcept {
  if (*capture) {
    const StringRef ref(begin + capture->offset, capture->size);
    if (ignoreCase) {
      const char *const b = ref.begin();
      for (const char *iter = ref.end(); iter != b;) {
        if (input->availableBackward() && doSimpleCaseFolding(unsafePrevUtf8(iter)) ==
                                              doSimpleCaseFolding(input->consumeBackward())) {
          continue;
        }
        return 0;
      }
    } else if (!input->expectBackward(ref)) {
      return 0;
    }
  }
  return 1;
}

int32_t arsh_jit_push_set_ins(BacktrackStack *bts, const Input *input,
                              const uint32_t target) noexcept {
  return bts->push(Backtrack::newSetIns(*input, target)) ? 1 : 0;
}

int32_t arsh_jit_push_set_capture(BacktrackStack *bts, const uint32_t index,
                                  const Capture *capture) noexcept {
  return bts->push(Backtrack::newSetCapture(index, *capture)) ? 1 : 0;
}

int32_t arsh_jit_push_reset_captures(BacktrackStack *bts, Capture *captures, const uint32_t first,
                                     const uint32_t last) noexcept {
  for (uint32_t i = first; i <= last; i++) {
    if (!bts->push(Backtrack::newSetCapture(i, captures[i]))) { // save original capture
      return 0;
    }
    captures[i] = Capture();
  }
  return 1;
}

int32_t arsh_jit_push_lookaround(BacktrackStack *bts, const Input *input, const uint32_t target,
                                 const int32_t negate) noexcept {
  return bts->push(Backtrack::newLookAround(*input, target, negate != 0)) ? 1 : 0;
}

int32_t arsh_jit_cleanup_lookaround(BacktrackStack *bts, Input *input,
                                    Capture *captures) noexcept {
  return bts->cleanupLookAround(*input, captures) ? 1 : 0;
}

void arsh_jit_finish(MatchContext *ctx, Input *input, Capture *captures,
                     const char *matchStart) noexcept {
  captures[0].offset = static_cast<unsigned int>(matchStart - input->getBegin());
  captures[0].size = static_cast<unsigned int>(input->getIter() - matchStart);
  ctx->syncInput(*input);
}

/**
 * the `BeginLoop` / `EndLoop` body.
 *
 * the stencil already read the loop operands into registers and passed them as plain values, so the
 * step never touches the bytecode. it returns a `JIT_ACTION_*` telling the stencil which edge to
 * take: `FIRST` is the loop body (which keeps the loop state), `SECOND` is the instruction after the
 * loop (`jit_target_loop_outer`, or the successor when the maximum was reached).
 */
int32_t arsh_jit_loop_step(Input *input, LoopState *loops, BacktrackStack *bts,
                           const uint16_t loopIndex, const uint16_t min, const uint32_t max,
                           const int32_t greedy, const uint32_t beginOffset,
                           const uint32_t outerOffset) noexcept {
  auto &loop = loops[loopIndex];
  if (loop.inputOffset == input->getOffset() && loop.count > min) {
    // after minimum repeat, if the input is not consumed, backtrack
    return JIT_ACTION_BACKTRACK;
  }
  if (loop.count < min) {
    if (!bts->prepareLoopBody(*input, loopIndex, loop)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    return JIT_ACTION_FIRST; // the caller tail-calls the body
  }
  if (loop.count == max) {
    return JIT_ACTION_SECOND; // the caller tail-calls the instruction after the loop
  }
  if (greedy) {
    if (!bts->prepareGreedyLoop(*input, outerOffset, loopIndex, loop)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    return JIT_ACTION_FIRST;
  }
  if (!bts->prepareNonGreedyLoop(*input, beginOffset, loopIndex, loop)) {
    return JIT_ACTION_STACK_LIMIT;
  }
  return JIT_ACTION_SECOND;
}

int32_t arsh_jit_prepare_radix(const Matcher *matchers, Input *input, BacktrackStack *bts,
                               const uint16_t index, const uint32_t emojiFlags,
                               const int32_t hasRadix) noexcept {
  return pushRadixState(matchers, input, bts, radixHasEmoji(emojiFlags), hasRadix != 0, index, false);
}

int32_t arsh_jit_prepare_lb_radix(const Matcher *matchers, Input *input, BacktrackStack *bts,
                                  const uint16_t index, const uint32_t emojiFlags,
                                  const int32_t hasRadix) noexcept {
  return pushRadixState(matchers, input, bts, radixHasEmoji(emojiFlags), hasRadix != 0, index, true);
}

/**
 * the shared radix body.
 *
 * `remove` distinguishes a radix entered from the top of `Prepare*Radix` (whose window was just
 * pushed) from one re-entered by backtracking (whose window is one code point too wide), so the two
 * cases share the matching below and differ only in the shrink step.
 *
 * every operand was read by the stencil from its holes and is passed as a plain value: `index` and
 * `emojiFlags`/`hasRadix` describe the radix instruction (`emojiFlags` also carries whether case
 * folding applies), `radixOffset`/`nextOffset` where the two outcomes lead. the helper therefore
 * never reads the bytecode. it reports which edge to take as a `JIT_ACTION_*` and pushes the
 * backtrack entry that re-enters the instruction through `radixOffset`.
 */
int32_t radixBody(Input *input, BacktrackStack *bts, std::string *foldBuf, const Matcher *matchers,
                  const uint16_t index, const uint32_t emojiFlags, const bool hasRadix,
                  const uint32_t radixOffset, const uint32_t nextOffset, const bool backward,
                  const bool remove) noexcept {
  const auto emoji = static_cast<ucp::RGIEmojiSeq>(emojiFlags);
  const bool hasEmoji = radixHasEmoji(emojiFlags);
  const bool ignoreCase = radixIgnoreCase(emojiFlags);
  if (remove) {
    if (backward) {
      StringRef ref(input->getIter() - bts->getRadixState(), bts->getRadixState());
      unsafeRemovePrefixUtf8(ref);
      bts->updateRadixState(ref.size());
    } else {
      StringRef ref(input->getIter(), bts->getRadixState());
      unsafeRemoveSuffixUtf8(ref);
      bts->updateRadixState(ref.size());
    }
  }
  const unsigned int strSize = bts->getRadixState();
  const bool available = backward ? input->availableBackward() : input->available();
  if (!strSize || !available) {
    return JIT_ACTION_BACKTRACK;
  }
  const StringRef ref = backward ? StringRef(input->getIter() - strSize, strSize)
                                 : StringRef(input->getIter(), strSize);
  unsigned int consumedSize = 0;
  if (hasEmoji) {
    auto [s, p] = backward
                      ? findBackwardLongestMatched(ucp::getEmojiTrie(), ref, *foldBuf, ignoreCase)
                      : findLongestMatched(ucp::getEmojiTrie(), ref, *foldBuf, ignoreCase);
    // only accept the emoji sequences this instruction asked for
    if (p && hasFlag(toUnderlying(emoji), p)) {
      consumedSize = s;
    }
  }
  if (hasRadix) {
    auto [s, p] = backward ? findBackwardLongestMatched(matchers[index].asRadixTree(), ref, *foldBuf,
                                                       ignoreCase)
                           : findLongestMatched(matchers[index].asRadixTree(), ref, *foldBuf,
                                                ignoreCase);
    if (p) {
      consumedSize = std::max<unsigned int>(consumedSize, s);
    }
  }
  if (consumedSize) {
    bts->updateRadixState(consumedSize);
    // a backtrack into this instruction must re-run it one code point shorter
    if (!bts->push(Backtrack::newSetInsFromOffset(radixOffset, input->getIter()))) {
      return JIT_ACTION_STACK_LIMIT;
    }
    input->setIter(backward ? input->getIter() - consumedSize : input->getIter() + consumedSize);
    return JIT_ACTION_FIRST; // the caller tail-calls the match target
  }
  if (nextOffset) {
    return JIT_ACTION_SECOND; // the successor is the next chained radix instruction
  }
  return JIT_ACTION_BACKTRACK;
}

int32_t arsh_jit_radix_body(const Matcher *matchers, Input *input, BacktrackStack *bts,
                            std::string *foldBuf, const uint16_t index, const uint32_t emojiFlags,
                            const int32_t hasRadix, const uint32_t radixOffset,
                            const int32_t nextOffset, const int32_t removeSuffix) noexcept {
  return radixBody(input, bts, foldBuf, matchers, index, emojiFlags, hasRadix != 0, radixOffset,
                   nextOffset, false, removeSuffix != 0);
}

int32_t arsh_jit_lb_radix_body(const Matcher *matchers, Input *input, BacktrackStack *bts,
                               std::string *foldBuf, const uint16_t index,
                               const uint32_t emojiFlags, const int32_t hasRadix,
                               const uint32_t radixOffset, const int32_t nextOffset,
                               const int32_t removePrefix) noexcept {
  return radixBody(input, bts, foldBuf, matchers, index, emojiFlags, hasRadix != 0, radixOffset,
                   nextOffset, true, removePrefix != 0);
}

} // extern "C"

#define GEN_JIT_RUNTIME_ENTRY(name, ret, args) {#name, reinterpret_cast<void *>(name)},

namespace {
const struct {
  const char *name;
  void *address;
} JIT_RUNTIME_FNS[] = {EACH_JIT_RUNTIME_FN(GEN_JIT_RUNTIME_ENTRY)};
} // namespace
#undef GEN_JIT_RUNTIME_ENTRY

void *lookupJitRuntimeSymbol(const char *name) {
  for (const auto &entry : JIT_RUNTIME_FNS) {
    if (strcmp(entry.name, name) == 0) {
      return entry.address;
    }
  }
  return nullptr;
}

} // namespace arsh::regex::jit
