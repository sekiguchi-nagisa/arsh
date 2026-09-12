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
class BacktrackStack;
struct Capture;
struct LoopState;
struct Inst;

namespace jit {

/**
 * status returned by the JIT entry point.
 *
 * a stencil keeps running the straight-line instruction sequence by tail-calling the stencil of the
 * next instruction. it returns to the driver only on the control flow events below.
 */
enum : int32_t {
  /**
   * the current path failed. the driver must run the backtrack stack and re-enter the JIT at the
   * resolved instruction.
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
 * both are intentionally left undefined in the stencil translation units, so references to them
 * become patchable "holes" (absolute 8-byte relocations).
 *
 * - `jit_next`: the compiler patches each occurrence with the address of the stencil for the
 *   successor instruction, which turns the tail call into a direct jump. a stencil only uses it to
 *   advance to its own successor.
 * - `jit_goto`: a fixed trampoline which resolves the target instruction through
 *   `JitContext::codeOffsets`. it is used for branch targets that are not the successor (branch
 *   targets, loop back edges, radix skip edges).
 */
extern "C" JIT_STENCIL_CALL int32_t jit_next(JitContext &ctx, const Inst *inst) noexcept;
extern "C" JIT_STENCIL_CALL int32_t jit_goto(JitContext &ctx, const Inst *inst) noexcept;

} // namespace jit

} // namespace arsh::regex

#endif // ARSH_REGEX_JIT_JIT_CONTEXT_H
