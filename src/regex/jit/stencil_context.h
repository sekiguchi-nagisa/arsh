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
 * it must not depend on the generated `stencil_data.h`, so that the stencil translation units can
 * be compiled as a standalone build step.
 */
namespace arsh::regex::jit {

/**
 * the "not matched yet" capture, used to restore a capture on backtracking.
 */
inline constexpr Capture CAPTURE_UNSET{};

/**
 * definition of a copy-and-patch stencil.
 *
 * `noinline` keeps each stencil a standalone function so `tools/gen_jit_stencil` can extract it.
 * `inst` points at the bytecode instruction the stencil was copied from; every stencil reads its
 * operands through the `instruction.h` accessors.
 */
#define JIT_STENCIL_DEF(name)                                                                      \
  __attribute__((noinline)) JIT_STENCIL_CALL int32_t name(JitContext & ctx,                        \
                                                          const Inst *inst) noexcept

/**
 * fetch the instruction at `inst` as its concrete type.
 */
#define JIT_INST(T) (*reinterpret_cast<const T *>(inst))

/**
 * compute an instruction address `n` bytes after `p`.
 */
#define JIT_ADV_FROM(p, n)                                                                         \
  reinterpret_cast<const Inst *>(reinterpret_cast<const char *>(p) + (n))

/**
 * advance the current bytecode pointer by `n` bytes.
 */
#define JIT_ADVANCE(n) JIT_ADV_FROM(inst, n)

/**
 * run the successor instruction.
 *
 * the `jit_next` reference becomes a hole, which the copy-and-patch compiler fills with the address
 * of the successor instruction's stencil, turning the tail call into a direct jump.
 */
#define JIT_NEXT_TYPE(T) JIT_STENCIL_TAIL return jit_next(ctx, JIT_ADVANCE(sizeof(T)))

/**
 * run the instruction at the absolute bytecode byte offset `off`.
 */
#define JIT_GOTO_OFF(off)                                                                          \
  JIT_STENCIL_TAIL return jit_goto(                                                                \
      ctx, reinterpret_cast<const Inst *>(reinterpret_cast<const char *>(ctx.instSeqBase) + (off)))

/**
 * run the instruction at the given bytecode address.
 */
#define JIT_GOTO_INST(p) JIT_STENCIL_TAIL return jit_goto(ctx, (p))

/**
 * run the instruction `n` bytes after the current one.
 */
#define JIT_GOTO_ADV(n) JIT_GOTO_INST(JIT_ADVANCE(n))

/**
 * resume matching without returning to the driver.
 *
 * the failing block tail-calls the `jit_backtrack` trampoline, which runs the backtrack stack and
 * then tail-calls the stencil of the resolved instruction. the machine stack therefore does not
 * grow, and the driver is only re-entered once the attempt is exhausted.
 */
#define JIT_BACKTRACK() JIT_STENCIL_TAIL return jit_backtrack(ctx, inst)

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
