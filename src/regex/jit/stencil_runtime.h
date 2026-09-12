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

#ifndef ARSH_REGEX_JIT_STENCIL_RUNTIME_H
#define ARSH_REGEX_JIT_STENCIL_RUNTIME_H

#include <cstdint>

#include "../backtrack.h"
#include "../capture.h"
#include "../input.h"
#include "../instruction.h"
#include "../match_context.h"
#include "../matcher.h"
#include "jit_context.h"

namespace arsh::regex::jit {

/**
 * helpers called from the stencils.
 *
 * they are compiled as ordinary functions (not as stencils) and are therefore not extracted into
 * `stencil_data.h`. `tools/gen_jit_stencil` records the referenced symbol name for every relocation
 * and the JIT resolves those names through `lookupJitRuntimeSymbol()`, which is generated from this
 * same list, so the helpers never have to be exported from the final binary.
 *
 * a stencil delegates to a helper whenever the operation manipulates a container (the backtrack
 * stack, the capture array) or consults a table (matcher, unicode property). everything else stays
 * inline in the stencil.
 *
 * the return convention is `int32_t`: `1` means "the condition held", `0` means "it did not",
 * unless documented otherwise. the helpers that return a `JIT_ACTION_*` value are documented at
 * their definition.
 *
 * the `int32_t` parameter list here is part of the ABI between the stencils and this file, so the
 * two must be kept in sync. the declarations below are generated from the same list to guarantee
 * that.
 */
// clang-format off
#define EACH_JIT_RUNTIME_FN(F)                                                                     \
  F(arsh_jit_case_fold,             int32_t,  (int32_t codePoint))                                 \
  F(arsh_jit_is_extend_word,        int32_t,  (int32_t codePoint))                                 \
  F(arsh_jit_grapheme_size,         uint32_t, (const char *data, uint32_t size))                   \
  F(arsh_jit_matcher_contains,      int32_t,  (const Matcher *matchers, uint16_t index,            \
                                                int32_t codePoint))                                \
  F(arsh_jit_expect_forward,        int32_t,  (Input * input, const Matcher *matchers,             \
                                                uint16_t index))                                   \
  F(arsh_jit_expect_backward,       int32_t,  (Input * input, const Matcher *matchers,             \
                                                uint16_t index))                                   \
  F(arsh_jit_resolve_named_backref, void,     (const MatchContext *ctx, uint16_t refIndex,         \
                                                Capture *out))                                     \
  F(arsh_jit_backref_forward,      int32_t,  (Input * input, const Capture *capture,              \
                                                const char *begin))                                \
  F(arsh_jit_ibackref_forward,      int32_t,  (Input * input, const Capture *capture,              \
                                                const char *begin))                                \
  F(arsh_jit_lbbackref_backward,    int32_t,  (Input * input, const Capture *capture,              \
                                                const char *begin, int32_t ignoreCase))            \
  F(arsh_jit_push_set_ins,          int32_t,  (BacktrackStack * bts, const Input *input,           \
                                                uint32_t target))                                  \
  F(arsh_jit_push_set_capture,      int32_t,  (BacktrackStack * bts, uint32_t index,               \
                                                const Capture *capture))                           \
  F(arsh_jit_push_reset_captures,   int32_t,  (BacktrackStack * bts, Capture *captures,            \
                                                uint32_t first, uint32_t last))                    \
  F(arsh_jit_push_lookaround,       int32_t,  (BacktrackStack * bts, const Input *input,           \
                                                uint32_t target, int32_t negate))                  \
  F(arsh_jit_cleanup_lookaround,    int32_t,  (BacktrackStack * bts, Input *input,                 \
                                                Capture *captures))                                \
  F(arsh_jit_finish,                void,     (JitContext * ctx))                                  \
  F(arsh_jit_loop_step,             int32_t,  (JitContext * ctx, const BeginLoopIns *loopIns,      \
                                                const Inst **next))                                \
  F(arsh_jit_prepare_radix,         int32_t,  (JitContext * ctx,                                   \
                                                const RadixOrEmojiIns *ins))                       \
  F(arsh_jit_prepare_lb_radix,      int32_t,  (JitContext * ctx,                                   \
                                                const LBRadixOrEmojiIns *ins))                     \
  F(arsh_jit_radix_body,            int32_t,  (JitContext * ctx, const RadixOrEmojiIns *ins,       \
                                                int32_t removeSuffix))                             \
  F(arsh_jit_lb_radix_body,         int32_t,  (JitContext * ctx,                                   \
                                                const LBRadixOrEmojiIns *ins,                      \
                                                int32_t removePrefix))
// clang-format on

extern "C" {
#define GEN_JIT_RUNTIME_DECL(name, ret, args) ret name args noexcept;
EACH_JIT_RUNTIME_FN(GEN_JIT_RUNTIME_DECL)
#undef GEN_JIT_RUNTIME_DECL
} // extern "C"

/**
 * resolve a helper symbol name (as recorded by `tools/gen_jit_stencil`) to its run-time address.
 *
 * returns nullptr for an unknown name. the control flow placeholders (`jit_next` / `jit_goto`) are
 * handled directly by the compiler, so they are not in the table.
 */
void *lookupJitRuntimeSymbol(const char *name);

} // namespace arsh::regex::jit

#endif // ARSH_REGEX_JIT_STENCIL_RUNTIME_H
