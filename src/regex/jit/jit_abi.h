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

#ifndef ARSH_REGEX_JIT_JIT_ABI_H
#define ARSH_REGEX_JIT_JIT_ABI_H

/**
 * copy-and-patch stencil ABI.
 *
 * every stencil has the signature `int32_t(JitContext &, const Inst *) noexcept` and uses the
 * `preserve_none` calling convention. the stencil translation units are compiled standalone (they
 * are never linked into a library) with `-fno-pic -fno-pie -mcmodel=large`, so that
 *
 * - the compiler does not keep any callee-saved register across a call (`preserve_none`), which is
 *   what makes the tail calls below true tail calls,
 * - `musttail` (via `JIT_STENCIL_TAIL`) turns the "run the next instruction" edge into a bare `jmp`,
 * - every reference to a control flow placeholder or to a runtime helper becomes an 8-byte absolute
 *   relocation (`R_X86_64_64`) issued by a `movabs`, which `tools/gen_jit_stencil` records as a
 *   patchable "hole".
 *
 * the stencils never have their instruction operands patched into the machine code. every stencil
 * receives the address of the bytecode instruction it was copied from (`inst`) and reads the
 * operands through the regular `instruction.h` accessors. only control flow is patched, because
 * that is what makes copy-and-patch fast.
 */
#if defined(__clang__)
#define JIT_STENCIL_CALL __attribute__((preserve_none))
#define JIT_STENCIL_TAIL [[clang::musttail]]
#elif defined(__GNUC__)
#define JIT_STENCIL_CALL __attribute__((preserve_none))
#define JIT_STENCIL_TAIL [[gnu::musttail]]
#else
#error "regex JIT requires a compiler supporting preserve_none and musttail"
#endif

/**
 * the `preserve_none` calling convention maps the first two integer arguments to `r12` and `r13`
 * (instead of `rdi` / `rsi`). a stencil therefore receives `ctx` in `r12` and `inst` in `r13`, and a
 * tail call only has to assign those two registers. `r12`/`r13` are also callee-saved under the
 * default convention, so the compiler spills them around a call to a default-convention helper and
 * restores them before the tail call — which is why a stencil may freely call helpers (the unicode
 * properties, the backtrack stack, the radix search) before continuing.
 */
#if !defined(__x86_64__)
#error "regex JIT currently supports x86-64 only"
#endif

#endif // ARSH_REGEX_JIT_JIT_ABI_H
