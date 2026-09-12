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
 * push the initial `RadixState` and return `JIT_ACTION_FIRST` / `JIT_ACTION_STACK_LIMIT`.
 *
 * mirrors the `PrepareRadix` / `PrepareLBRadix` prologue of the interpreter.
 */
int32_t pushRadixState(JitContext *ctx, const bool hasEmoji, const bool hasRadix,
                       const uint16_t matcherIndex, const bool backward) {
  unsigned int codePointCount = 0;
  if (hasEmoji) {
    codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
  }
  if (hasRadix) {
    codePointCount = std::max<unsigned int>(
        codePointCount, ctx->matchers[matcherIndex].asRadixTree().getMaxCodePointCount());
  }
  const unsigned int size =
      backward ? ctx->input->remainBackwardOfCodePoints(codePointCount).size()
               : ctx->input->remainForwardOfCodePoints(codePointCount).size();
  if (!ctx->bts->push(Backtrack::newRadixState(size))) {
    return JIT_ACTION_STACK_LIMIT;
  }
  return JIT_ACTION_FIRST;
}

const Inst *advance(const Inst *inst, const size_t size) {
  return reinterpret_cast<const Inst *>(reinterpret_cast<const char *>(inst) + size);
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

int32_t arsh_jit_ibackref_forward(Input *input, const Capture *capture,
                                 const char *begin) noexcept {
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

void arsh_jit_finish(JitContext *ctx) noexcept {
  ctx->captures[0].offset = static_cast<unsigned int>(ctx->matchStart - ctx->input->getBegin());
  ctx->captures[0].size = static_cast<unsigned int>(ctx->input->getIter() - ctx->matchStart);
  ctx->ctx->syncInput(*ctx->input);
}

/**
 * the `BeginLoop` / `EndLoop` body.
 *
 * `*next` receives the instruction to continue with: the loop body (after the `BeginLoop`) or the
 * instruction after the loop (`getOuter`).
 */
int32_t arsh_jit_loop_step(JitContext *ctx, const BeginLoopIns *loopIns,
                          const Inst **next) noexcept {
  auto &loop = ctx->loopStates[loopIns->getLoopIndex()];
  if (loop.inputOffset == ctx->input->getOffset() && loop.count > loopIns->getMin()) {
    // after minimum repeat, if the input is not consumed, backtrack
    return JIT_ACTION_BACKTRACK;
  }
  if (loop.count < loopIns->getMin()) {
    if (!ctx->bts->prepareLoopBody(*ctx->input, loopIns->getLoopIndex(), loop)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    *next = advance(loopIns, sizeof(BeginLoopIns));
  } else if (loop.count == loopIns->getMax()) {
    *next = reinterpret_cast<const Inst *>(ctx->instSeqBase + loopIns->getOuter());
  } else if (loopIns->greedy) {
    if (!ctx->bts->prepareGreedyLoop(*ctx->input, *loopIns, loop)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    *next = advance(loopIns, sizeof(BeginLoopIns));
  } else {
    if (!ctx->bts->prepareNonGreedyLoop(*ctx->input, loopIns, loop)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    *next = reinterpret_cast<const Inst *>(ctx->instSeqBase + loopIns->getOuter());
  }
  return JIT_ACTION_FIRST;
}

int32_t arsh_jit_prepare_radix(JitContext *ctx, const RadixOrEmojiIns *ins) noexcept {
  return pushRadixState(ctx, ins->hasEmoji(), ins->hasRadix, ins->getIndex(), false);
}

int32_t arsh_jit_prepare_lb_radix(JitContext *ctx, const LBRadixOrEmojiIns *ins) noexcept {
  return pushRadixState(ctx, ins->hasEmoji(), ins->hasRadix, ins->getIndex(), true);
}

int32_t arsh_jit_radix_body(JitContext *ctx, const RadixOrEmojiIns *ins,
                           const int32_t removeSuffix) noexcept {
  if (removeSuffix) {
    StringRef ref(ctx->input->getIter(), ctx->bts->getRadixState());
    unsafeRemoveSuffixUtf8(ref);
    ctx->bts->updateRadixState(ref.size());
  }
  const unsigned int strSize = ctx->bts->getRadixState();
  if (!strSize || !ctx->input->available()) {
    return JIT_ACTION_BACKTRACK;
  }
  const StringRef ref(ctx->input->getIter(), strSize);
  unsigned int consumedSize = 0;
  if (ins->hasEmoji()) {
    auto [s, p] = findLongestMatched(ucp::getEmojiTrie(), ref, *ctx->foldBuf, ins->ignoreCase());
    if (p && hasFlag(toUnderlying(ins->emoji), p)) {
      consumedSize = s;
    }
  }
  if (ins->hasRadix) {
    auto [s, p] = findLongestMatched(ctx->matchers[ins->getIndex()].asRadixTree(), ref,
                                     *ctx->foldBuf, ins->ignoreCase());
    if (p) {
      consumedSize = std::max<unsigned int>(consumedSize, s);
    }
  }
  if (consumedSize) {
    ctx->bts->updateRadixState(consumedSize);
    const auto target =
        static_cast<uint32_t>(reinterpret_cast<const char *>(ins) - ctx->instSeqBase);
    if (!arsh_jit_push_set_ins(ctx->bts, ctx->input, target)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    ctx->input->setIter(ctx->input->getIter() + consumedSize);
    return JIT_ACTION_FIRST;
  }
  if (ins->nextOffset) {
    return JIT_ACTION_SECOND; // try next
  }
  return JIT_ACTION_BACKTRACK;
}

int32_t arsh_jit_lb_radix_body(JitContext *ctx, const LBRadixOrEmojiIns *ins,
                              const int32_t removePrefix) noexcept {
  if (removePrefix) {
    StringRef ref(ctx->input->getIter() - ctx->bts->getRadixState(), ctx->bts->getRadixState());
    unsafeRemovePrefixUtf8(ref);
    ctx->bts->updateRadixState(ref.size());
  }
  const unsigned int strSize = ctx->bts->getRadixState();
  if (!strSize || !ctx->input->availableBackward()) {
    return JIT_ACTION_BACKTRACK;
  }
  const StringRef ref(ctx->input->getIter() - strSize, strSize);
  unsigned int consumedSize = 0;
  if (ins->hasEmoji()) {
    auto [s, p] =
        findBackwardLongestMatched(ucp::getEmojiTrie(), ref, *ctx->foldBuf, ins->ignoreCase());
    if (p && hasFlag(toUnderlying(ins->emoji), p)) {
      consumedSize = s;
    }
  }
  if (ins->hasRadix) {
    auto [s, p] = findBackwardLongestMatched(ctx->matchers[ins->getIndex()].asRadixTree(), ref,
                                             *ctx->foldBuf, ins->ignoreCase());
    if (p) {
      consumedSize = std::max<unsigned int>(consumedSize, s);
    }
  }
  if (consumedSize) {
    ctx->bts->updateRadixState(consumedSize);
    const auto target =
        static_cast<uint32_t>(reinterpret_cast<const char *>(ins) - ctx->instSeqBase);
    if (!arsh_jit_push_set_ins(ctx->bts, ctx->input, target)) {
      return JIT_ACTION_STACK_LIMIT;
    }
    ctx->input->setIter(ctx->input->getIter() - consumedSize);
    return JIT_ACTION_FIRST;
  }
  if (ins->nextOffset) {
    return JIT_ACTION_SECOND; // try next
  }
  return JIT_ACTION_BACKTRACK;
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
