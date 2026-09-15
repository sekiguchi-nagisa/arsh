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
 * a stencil takes the state it works on in *argument registers* and leaves by tail-calling the
 * stencil of the next instruction. the stencil translation units are compiled standalone (they are
 * never linked into a library) with `-fno-pic -fno-pie -mcmodel=large`, so that
 *
 * - the argument registers are caller-saved under `preserve_none`, so a tail call never has to
 *   restore a callee-saved register,
 * - `musttail` (via `JIT_STENCIL_TAIL`) turns the "run the next instruction" edge into a bare `jmp`,
 * - every reference to a control flow placeholder or to a runtime helper becomes an 8-byte absolute
 *   relocation (`R_X86_64_64`) issued by a `movabs`, which `tools/gen_jit_stencil` records as a
 *   patchable "hole".
 *
 * a stencil never receives the bytecode instruction it was copied from, and never walks the
 * bytecode: its operands and its branch targets are read from placeholders, which the copy-and-patch
 * compiler patches straight into the machine code. see `jit_context.h` for the placeholder list.
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
 * the `preserve_none` convention passes its integer arguments in `r12`, `r13`, `r14`, `r15`, `rdi`
 * and `rsi` (instead of `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`), and both gcc and clang save those
 * registers around a call, so a stencil may freely call a helper and then continue with a tail call.
 *
 * a stencil signature therefore uses exactly six arguments: the `JitContext *` plus the five pieces
 * of state an instruction touches (`Input`, `BacktrackStack`, `Capture`, `LoopState`, `Matcher`).
 * the hot loop never loads them from memory, and a tail call into the next stencil needs no register
 * shuffling at all, because the state is already in the registers that stencil expects.
 *
 * the list stops at six on purpose: gcc and clang disagree about the seventh `preserve_none`
 * argument (gcc passes it on the stack, clang in `rdx`), which would make the ABI compiler-dependent
 * and would break the tail call, since a `musttail` call may not pass extra stack arguments.
 */
#if !defined(__x86_64__)
#error "regex JIT currently supports x86-64 only"
#endif

#endif // ARSH_REGEX_JIT_JIT_ABI_H
