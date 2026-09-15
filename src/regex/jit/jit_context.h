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

#ifndef ARSH_REGEX_JIT_JIT_CONTEXT_H
#define ARSH_REGEX_JIT_JIT_CONTEXT_H

#include <cstdint>
#include <string>

#include "jit_abi.h"

namespace arsh::regex {

class Input;
class Matcher;
class MatchContext;
class Timer;
class BacktrackStack;
struct Capture;
struct Inst;
struct LoopState;

namespace jit {

/**
 * how the JIT entry point reports the outcome of a match attempt.
 *
 * a stencil advances by tail-calling the stencil of the next instruction, and a failing block
 * tail-calls the `jit_backtrack` trampoline instead of returning to the driver. the driver is
 * therefore only re-entered on the events below.
 */
enum : int32_t {
  /**
   * the current search attempt is exhausted (the backtrack stack is empty), so the driver has to
   * advance the search start and re-enter the JIT at the first instruction.
   *
   * only `jit_backtrack` produces this.
   */
  JIT_BACKTRACK_STATUS = -1,

  /**
   * the whole pattern matched.
   */
  JIT_MATCH_STATUS = -2,

  /**
   * the backtrack stack reached `Regex::MAX_STACK_DEPTH`.
   */
  JIT_STACK_LIMIT_STATUS = -3,

  /**
   * the match timer expired. only `jit_backtrack` produces this (it runs the timer check and cannot
   * unwind to the driver once it has taken over the backtracking).
   */
  JIT_TIMEOUT_STATUS = -4,

  /**
   * the match was canceled. produced like `JIT_TIMEOUT_STATUS`.
   */
  JIT_CANCEL_STATUS = -5,
};

/**
 * what a composite runtime helper selected for its caller (the loop step, the radix body).
 *
 * the helper pushes the backtrack entry that leads to its `SECOND` alternative, so a `SECOND`
 * always means "jump to the second target and let backtracking find the first one again".
 */
enum : int32_t {
  JIT_ACTION_FIRST = 0,  // enter the loop body / continue after the radix match
  JIT_ACTION_SECOND = 1, // the instruction after the loop / the next chained radix instruction
  JIT_ACTION_BACKTRACK = 2,
  JIT_ACTION_STACK_LIMIT = 3,
};

/**
 * the *cold* execution state, reached through the `ctx` register.
 *
 * the state a stencil touches on the hot path travels in the argument registers instead (see
 * `jit_abi.h`). what remains here is read only when control flow leaves the straight line: on a
 * branch target, on a backtrack, or when the driver re-enters.
 */
struct JitContext {
  // clang-format off
  MatchContext *ctx{nullptr};            // for syncInput / resolveNamedBackRef
  std::string *foldBuf{nullptr};         // the caller's local `foldBuf`, used by the radix stencils

  const char *instSeqBase{nullptr};      // the bytecode base, to turn a target offset into an address
  const char *matchStart{nullptr};       // where the current attempt started, captured by `Match`

  const uint8_t *codeBase{nullptr};      // the executable buffer base
  const uint32_t *codeOffsets{nullptr};  // bytecode byte offset -> block offset within the buffer

  Timer *timer{nullptr};                 // checked by `jit_backtrack`
  uint32_t btCount{0};                   // backtracks since the last timer check

  // the backtrack target, kept here rather than in a local of `jit_backtrack`: gcc rejects a
  // `musttail` call whose arguments could have leaked the address of an automatic variable
  // (`-Werror=maybe-musttail-local-addr`), and `BacktrackStack::backtrack()` writes the target
  // through a reference. this is also what the interpreter's local `inst` holds.
  const Inst *btTarget{nullptr};
  // clang-format on
};

/**
 * signature of every stencil, and of the two trampolines.
 *
 * the parameters are the state carried in the *argument registers* (see `jit_abi.h`), which is what
 * lets a tail call into the next stencil need no register shuffling at all.
 *
 * the list stops at six parameters on purpose: that is the part of `preserve_none` gcc and clang
 * agree on. a stencil is therefore not handed the instruction it was copied from -- it reads the
 * operands and the branch targets its instruction needs from the placeholders below, and
 * `jit_code.cpp` resolves each placeholder against that instruction.
 */
using JitFn = int32_t(JIT_STENCIL_CALL *)(JitContext *ctx, Input *input, BacktrackStack *bts,
                                         Capture *captures, LoopState *loops,
                                         const Matcher *matchers) noexcept;

/**
 * control flow placeholders.
 *
 * they carry the stencil signature, so a call to them can be a `musttail` one and compiles to a bare
 * `jmp`.
 *
 * - `jit_next` is patched, per occurrence, with the successor instruction's block. a stencil only
 *   ever continues with its own successor, so a direct jump is always correct for it.
 * - `jit_backtrack` is patched with a trampoline that runs the backtrack stack and then tail-calls
 *   the block the backtrack resolved. that is what keeps a failing block from unwinding to the
 *   driver, and so what keeps the failure/backtrack cycle inside the JIT code.
 *
 * a target that is known at compile time is *not* a trampoline: `jit_target_*` is patched with the
 * block address itself, so a branch to it is a plain indirect jump.
 */
extern "C" JIT_STENCIL_CALL int32_t jit_next(JitContext *ctx, Input *input, BacktrackStack *bts,
                                             Capture *captures, LoopState *loops,
                                             const Matcher *matchers) noexcept;
extern "C" JIT_STENCIL_CALL int32_t jit_backtrack(JitContext *ctx, Input *input,
                                                  BacktrackStack *bts, Capture *captures,
                                                  LoopState *loops,
                                                  const Matcher *matchers) noexcept;

/**
 * operand placeholders.
 *
 * a stencil reads the operands of its instruction through these instead of walking the bytecode. the
 * `movabs` that loads one is the hole, so the value is patched straight into the machine code.
 *
 * a `jit_imm_*` carries a *value*: an operand of the instruction, or -- where a jump is involved --
 * the bytecode offset of the target, because a target reached through the backtrack stack has to be
 * resolved by the trampoline rather than patched in. a `jit_target_*` carries a *block address*
 * that the compiler resolved, for the branches whose target it knows statically.
 *
 * `jit_code.cpp` holds the mapping; the comment next to each declaration names the instructions that
 * resolve it.
 */
// clang-format off
// --- operands read from the instruction itself ---
extern "C" char jit_imm_code_point[];          // Char, IChar, LBChar
extern "C" char jit_imm_ignore_case[];         // LBChar, LBCharSet, LBBackRef
extern "C" char jit_imm_dot_all[];             // LBAny
extern "C" char jit_imm_multiline[];           // Start, End
extern "C" char jit_imm_invert[];              // Word, IWord, CharSet, ICharSet, LBCharSet
extern "C" char jit_imm_matcher_index[];       // CharSet, ICharSet, LBCharSet, String, LBString
extern "C" char jit_imm_capture_index[];       // BeginCapture, EndCapture, LBEndCapture
extern "C" char jit_imm_first_index[];         // ResetCaptures
extern "C" char jit_imm_last_index[];          // ResetCaptures
extern "C" char jit_imm_ref_index[];           // BackRef, IBackRef, LBBackRef
extern "C" char jit_imm_named[];               // BackRef, IBackRef, LBBackRef
extern "C" char jit_imm_negate[];              // BeginLookAround
extern "C" char jit_imm_alt_second[];          // Alt (bytecode offset of the second branch)
extern "C" char jit_imm_lookaround_target[];   // BeginLookAround (bytecode offset of the body end)
// --- loop operands, taken from the `BeginLoop` that both loop stencils refer to ---
extern "C" char jit_imm_loop_index[];          // BeginLoop, EndLoop
extern "C" char jit_imm_loop_min[];            // BeginLoop, EndLoop
extern "C" char jit_imm_loop_max[];            // BeginLoop, EndLoop
extern "C" char jit_imm_loop_greedy[];         // BeginLoop, EndLoop
extern "C" char jit_imm_loop_begin[];          // BeginLoop, EndLoop (offset of the `BeginLoop`)
extern "C" char jit_imm_loop_outer[];          // BeginLoop, EndLoop (offset after the loop)
// --- radix operands, taken from the radix instruction the `Prepare*` one is followed by ---
extern "C" char jit_imm_radix_index[];         // *Radix, Prepare*Radix
extern "C" char jit_imm_radix_emoji[];         // *Radix, Prepare*Radix (raw `RGIEmojiSeq` flags)
extern "C" char jit_imm_radix_has[];           // *Radix, Prepare*Radix (the `hasRadix` bit)
extern "C" char jit_imm_radix_offset[];        // *Radix, Prepare*Radix (offset to re-enter on backtrack)
extern "C" char jit_imm_radix_next[];          // *Radix, Prepare*Radix (is another radix chained?)
// --- branch targets, patched as block addresses ---
extern "C" char jit_target_jump[];             // Jump
extern "C" char jit_target_loop_body[];        // BeginLoop, EndLoop (the loop body)
extern "C" char jit_target_loop_outer[];       // BeginLoop, EndLoop (the instruction after the loop)
extern "C" char jit_target_radix_match[];      // *Radix, Prepare*Radix (after the longest match)
extern "C" char jit_target_radix_next[];       // Prepare*Radix (the next radix in the chain)
// clang-format on

} // namespace jit

} // namespace arsh::regex

#endif // ARSH_REGEX_JIT_JIT_CONTEXT_H
