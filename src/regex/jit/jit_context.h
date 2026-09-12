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
class MatchContext;
class Matcher;
class Timer;
class BacktrackStack;
struct Capture;
struct LoopState;
struct Inst;

namespace jit {

/**
 * status returned by the JIT entry point.
 *
 * a stencil keeps running the instruction sequence by tail-calling the stencil of the next
 * instruction, and a failing block tail-calls the `jit_backtrack` trampoline instead of returning
 * to the driver. the driver is only re-entered on the events below.
 *
 * `JIT_BACKTRACK_STATUS` is therefore only produced by `jit_backtrack` itself, and only when the
 * backtrack stack is empty, i.e. the current search attempt is exhausted.
 */
enum : int32_t {
  /**
   * the current search attempt failed and the backtrack stack is empty. the driver must advance the
   * search start and re-enter the JIT at the first instruction.
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
   * the match timer expired (produced by `jit_backtrack`, which cannot return to the driver
   * normally once it has taken over the backtracking).
   */
  JIT_TIMEOUT_STATUS = -4,

  /**
   * the match was canceled.
   */
  JIT_CANCEL_STATUS = -5,
};

/**
 * action returned by the composite runtime helpers (radix, loop).
 */
enum : int32_t {
  JIT_ACTION_FIRST = 0,
  JIT_ACTION_SECOND = 1,
  JIT_ACTION_BACKTRACK = 2,
  JIT_ACTION_STACK_LIMIT = 3,
};

/**
 * execution state shared between the JIT code and the driver.
 *
 * the layout is part of the stencil ABI: the generated code accesses these members at fixed
 * offsets, so the order must not change without regenerating the stencils.
 */
struct JitContext {
  // clang-format off
  Input *input{nullptr};                 // vm.cpp local `input`
  Capture *captures{nullptr};            // MatchContext::getCaptures()
  LoopState *loopStates{nullptr};        // MatchContext::getLoops()
  const Matcher *matchers{nullptr};      // Regex::getMatchers().data()
  BacktrackStack *bts{nullptr};          // vm.cpp local `bts`
  std::string *foldBuf{nullptr};         // vm.cpp local `foldBuf`
  MatchContext *ctx{nullptr};            // for syncInput / resolveNamedBackRef

  const char *instSeqBase{nullptr};      // Regex::getInstSeq().data()
  const char *matchStart{nullptr};       // vm.cpp local `oldIter`

  const uint8_t *codeBase{nullptr};      // executable buffer base address
  const uint32_t *codeOffsets{nullptr};  // bytecode byte offset -> offset within the buffer

  Timer *timer{nullptr};                 // vm.cpp `timer`, checked by `jit_backtrack`
  uint32_t btCount{0};                   // backtracks since the last timer check
  // clang-format on
};

/**
 * signature of every stencil.
 *
 * `inst` is the address of the bytecode instruction the stencil was copied from, so the stencil
 * reads its operands directly from the bytecode instead of having them patched into the code.
 */
using JitFn = int32_t(JIT_STENCIL_CALL *)(JitContext &ctx, const Inst *inst) noexcept;

/**
 * control flow placeholders.
 *
 * all are intentionally left undefined in the stencil translation units, so references to them
 * become patchable "holes" (absolute 8-byte relocations).
 *
 * - `jit_next`: the compiler patches each occurrence with the address of the stencil for the
 *   successor instruction, which turns the tail call into a direct jump. a stencil only uses it to
 *   advance to its own successor.
 * - `jit_goto`: a fixed trampoline which resolves the target instruction through
 *   `JitContext::codeOffsets`. it is used for branch targets that are not the successor (branch
 *   targets, loop back edges, radix skip edges).
 * - `jit_backtrack`: a fixed trampoline which runs the backtrack stack, then tail-calls the stencil
 *   of the resolved instruction. this is what makes a failing block resume matching without going
 *   through the driver. it takes the same two arguments as a stencil so that the call can be a
 *   `musttail` one.
 */
extern "C" JIT_STENCIL_CALL int32_t jit_next(JitContext &ctx, const Inst *inst) noexcept;
extern "C" JIT_STENCIL_CALL int32_t jit_goto(JitContext &ctx, const Inst *inst) noexcept;
extern "C" JIT_STENCIL_CALL int32_t jit_backtrack(JitContext &ctx, const Inst *inst) noexcept;

} // namespace jit

} // namespace arsh::regex

#endif // ARSH_REGEX_JIT_JIT_CONTEXT_H
