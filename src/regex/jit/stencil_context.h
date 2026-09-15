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

#ifndef ARSH_REGEX_JIT_STENCIL_CONTEXT_H
#define ARSH_REGEX_JIT_STENCIL_CONTEXT_H

#include "../capture.h"
#include "../input.h"
#include "../instruction.h"
#include "../match_context.h"
#include "../matcher.h"
#include "jit_context.h"
#include "stencil_runtime.h"
#include "unicode/case_fold.h"
#include "unicode/property.h"

/**
 * header included by every `stencil_*.cpp` translation unit.
 *
 * it must not depend on the generated `stencil_data.h`, so that the stencil translation units can be
 * compiled as a standalone build step.
 */
namespace arsh::regex::jit {

/**
 * the "not matched yet" capture, used to restore a capture on backtracking.
 */
inline constexpr Capture CAPTURE_UNSET{};

/**
 * definition of a copy-and-patch stencil.
 *
 * the parameters are part of the ABI in `jit_abi.h`: the cold context and the five pieces of state
 * the matching proper works on. a stencil never receives the bytecode instruction, so it takes no
 * `inst` argument and its body never advances a bytecode pointer; `noinline` keeps it a standalone
 * function so `tools/gen_jit_stencil` can extract it.
 */
#define JIT_STENCIL_DEF(name)                                                                       \
  extern "C" __attribute__((noinline)) JIT_STENCIL_CALL int32_t name(                               \
      JitContext *ctx, Input *input, BacktrackStack *bts, Capture *captures, LoopState *loops,      \
      const Matcher *matchers) noexcept

/**
 * read an operand of the instruction the stencil was copied from.
 *
 * the reference to the placeholder becomes a `movabs` relocation, which the copy-and-patch compiler
 * overwrites with the operand value. the cast is applied to the *address*, not to the placeholder
 * object, so that no memory is ever read for it.
 */
#define JIT_IMM_U64(n) reinterpret_cast<uintptr_t>(n)
#define JIT_IMM_I32(n) static_cast<int32_t>(JIT_IMM_U64(n))
#define JIT_IMM_U32(n) static_cast<uint32_t>(JIT_IMM_U64(n))
#define JIT_IMM_U16(n) static_cast<uint16_t>(JIT_IMM_U64(n))
#define JIT_IMM_BOOL(n) ((JIT_IMM_U64(n) & 1u) != 0)

/**
 * read a *code* address operand, so that the tail call below compiles to a direct jump.
 *
 * a placeholder is a `char[]` (that is what makes a reference to it a relocation instead of a load
 * of an initialized object), but a stencil may have carried it in a `const char *` local. the
 * `const_cast` strips only that; the address is never written through, the copy-and-patch compiler
 * patches the `movabs` that loads it.
 */
#define JIT_TARGET(n) reinterpret_cast<JitFn>(const_cast<char *>(n))

/**
 * continue with the successor instruction.
 *
 * the `jit_next` reference becomes a hole, which the copy-and-patch compiler fills with the address
 * of the successor instruction's stencil, turning the tail call into a direct jump.
 */
#define JIT_NEXT() JIT_STENCIL_TAIL return jit_next(ctx, input, bts, captures, loops, matchers)

/**
 * resume matching without returning to the driver.
 *
 * the failing block tail-calls the `jit_backtrack` trampoline, which runs the backtrack stack and
 * then enters the stencil it resolved. a stencil never resolves a target itself (it has no bytecode
 * pointer anymore), so `jit_backtrack` takes the place of the driver's backtrack loop and the
 * machine stack does not grow.
 */
#define JIT_BACKTRACK()                                                                            \
  JIT_STENCIL_TAIL return jit_backtrack(ctx, input, bts, captures, loops, matchers)

/**
 * run the stencil for the code address `target` (a branch target, already patched into the code).
 */
#define JIT_TAIL(target)                                                                           \
  JIT_STENCIL_TAIL return JIT_TARGET(target)(ctx, input, bts, captures, loops, matchers)

/**
 * return to the driver: the whole pattern matched.
 */
#define JIT_MATCHED() return JIT_MATCH_STATUS

/**
 * return to the driver: the backtrack stack reached its depth limit.
 */
#define JIT_STACK_LIMIT() return JIT_STACK_LIMIT_STATUS

/**
 * turn a failed backtrack stack push into a stack limit.
 */
#define JIT_TRY(expr)                                                                              \
  do {                                                                                             \
    if (!(expr)) {                                                                                 \
      JIT_STACK_LIMIT();                                                                           \
    }                                                                                              \
  } while (false)

} // namespace arsh::regex::jit

#endif // ARSH_REGEX_JIT_STENCIL_CONTEXT_H
