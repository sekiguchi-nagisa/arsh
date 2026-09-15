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

#include "stencil_context.h"

namespace arsh::regex::jit {

/**
 * string, capture, back reference, loop and look-around instructions.
 *
 * these are the instructions whose successor is not simply the next one, or whose operand is not a
 * plain value. each reads what it needs from a placeholder (patched into the code as a `jit_imm_*`
 * or `jit_target_*` hole), so none of them walks the bytecode; the state a stencil does not itself
 * use is left in its register for the successor.
 */

JIT_STENCIL_DEF(stencil_String) {
  const auto index = JIT_IMM_U16(jit_imm_matcher_index);
  if (arsh_jit_expect_forward(input, matchers, index)) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBString) {
  const auto index = JIT_IMM_U16(jit_imm_matcher_index);
  if (arsh_jit_expect_backward(input, matchers, index)) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_BeginCapture) {
  const auto index = JIT_IMM_U16(jit_imm_capture_index);
  captures[index] = {.offset = input->getOffset(), .size = 0};
  JIT_TRY(arsh_jit_push_set_capture(bts, index, &CAPTURE_UNSET));
  JIT_NEXT();
}

JIT_STENCIL_DEF(stencil_EndCapture) {
  const auto index = JIT_IMM_U16(jit_imm_capture_index);
  auto &capture = captures[index];
  capture.size = input->getOffset() - capture.offset;
  JIT_NEXT();
}

JIT_STENCIL_DEF(stencil_LBEndCapture) {
  const auto index = JIT_IMM_U16(jit_imm_capture_index);
  auto &capture = captures[index];
  const uint32_t actualEndOffset = capture.endOffset();
  capture.offset = input->getOffset();
  capture.size = actualEndOffset - capture.offset;
  JIT_NEXT();
}

JIT_STENCIL_DEF(stencil_ResetCaptures) {
  const auto first = JIT_IMM_U16(jit_imm_first_index);
  const auto last = JIT_IMM_U16(jit_imm_last_index);
  JIT_TRY(arsh_jit_push_reset_captures(bts, captures, first, last));
  JIT_NEXT();
}

/**
 * a back reference reads the capture it refers to, then compares it against the input.
 *
 * a named reference is resolved through the match context rather than read from the capture array,
 * so it is a runtime call. resolving it into a local would make that local's address escape into
 * the helper, which `musttail` rejects at the end of the block, so the capture is materialized in a
 * `Capture` on the machine stack and passed by reference; the value is only read, so taking its
 * address is harmless.
 */
#define JIT_BACKREF_DEF(fn, helper)                                                                \
  JIT_STENCIL_DEF(fn) {                                                                            \
    const auto refIndex = JIT_IMM_U16(jit_imm_ref_index);                                          \
    const bool named = JIT_IMM_BOOL(jit_imm_named);                                                \
    Capture capture = CAPTURE_UNSET;                                                               \
    if (named) {                                                                                   \
      arsh_jit_resolve_named_backref(ctx->ctx, refIndex, &capture);                                \
    } else {                                                                                       \
      capture = captures[refIndex];                                                                \
    }                                                                                              \
    if (helper(input, &capture, input->getBegin())) {                                              \
      JIT_NEXT();                                                                                  \
    }                                                                                              \
    JIT_BACKTRACK();                                                                               \
  }

JIT_BACKREF_DEF(stencil_BackRef, arsh_jit_backref_forward)
JIT_BACKREF_DEF(stencil_IBackRef, arsh_jit_ibackref_forward)

#undef JIT_BACKREF_DEF

JIT_STENCIL_DEF(stencil_LBBackRef) {
  const auto refIndex = JIT_IMM_U16(jit_imm_ref_index);
  const bool named = JIT_IMM_BOOL(jit_imm_named);
  const bool ignoreCase = JIT_IMM_BOOL(jit_imm_ignore_case);
  Capture capture = CAPTURE_UNSET;
  if (named) {
    arsh_jit_resolve_named_backref(ctx->ctx, refIndex, &capture);
  } else {
    capture = captures[refIndex];
  }
  if (arsh_jit_lbbackref_backward(input, &capture, input->getBegin(), ignoreCase)) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

/**
 * `BeginLoop` resets the loop state, then runs the shared loop step.
 *
 * the step reports where to go as a *code address* resolved by the runtime helper, since only the
 * helper knows whether the loop is entered (the body), skipped (the instruction after the loop) or
 * exhausted. those targets are patched in as `jit_target_loop_body` / `jit_target_loop_outer`, so
 * the jump is a direct one.
 */
JIT_STENCIL_DEF(stencil_BeginLoop) {
  const auto loopIndex = JIT_IMM_U16(jit_imm_loop_index);
  const auto min = JIT_IMM_U16(jit_imm_loop_min);
  const auto max = JIT_IMM_U32(jit_imm_loop_max);
  const bool greedy = JIT_IMM_BOOL(jit_imm_loop_greedy);
  const auto beginOffset = JIT_IMM_U32(jit_imm_loop_begin);
  const auto outerOffset = JIT_IMM_U32(jit_imm_loop_outer);
  loops[loopIndex] = LoopState();
  switch (arsh_jit_loop_step(input, loops, bts, loopIndex, min, max, greedy, beginOffset,
                             outerOffset)) {
  case JIT_ACTION_FIRST:
    JIT_TAIL(jit_target_loop_body);
  case JIT_ACTION_SECOND:
    JIT_TAIL(jit_target_loop_outer);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

/**
 * `EndLoop` jumps back to the associated `BeginLoop` and runs the loop step *without* resetting the
 * loop state (that only happens when the loop is entered from the top). note that this must not jump
 * to the `BeginLoop` stencil itself, since that would reset the counter on every iteration, so the
 * stencil for the loop step is a separate `jit_target_loop_body` hole.
 */
JIT_STENCIL_DEF(stencil_EndLoop) {
  const auto loopIndex = JIT_IMM_U16(jit_imm_loop_index);
  const auto min = JIT_IMM_U16(jit_imm_loop_min);
  const auto max = JIT_IMM_U32(jit_imm_loop_max);
  const bool greedy = JIT_IMM_BOOL(jit_imm_loop_greedy);
  const auto beginOffset = JIT_IMM_U32(jit_imm_loop_begin);
  const auto outerOffset = JIT_IMM_U32(jit_imm_loop_outer);
  switch (arsh_jit_loop_step(input, loops, bts, loopIndex, min, max, greedy, beginOffset,
                             outerOffset)) {
  case JIT_ACTION_FIRST:
    JIT_TAIL(jit_target_loop_body);
  case JIT_ACTION_SECOND:
    JIT_TAIL(jit_target_loop_outer);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

/**
 * `[BeginLookAround, EndLookAround)` is run with the look-around state pushed on the backtrack
 * stack. the interpreter jumps straight to `EndLookAround` (skipping the body) at
 * `BeginLookAround`, then runs the body, so the pushed target is the end of the body.
 */
JIT_STENCIL_DEF(stencil_BeginLookAround) {
  const bool negate = JIT_IMM_BOOL(jit_imm_negate);
  JIT_TRY(arsh_jit_push_lookaround(bts, input, JIT_IMM_U32(jit_imm_lookaround_target), negate));
  JIT_NEXT();
}

JIT_STENCIL_DEF(stencil_EndLookAround) {
  if (arsh_jit_cleanup_lookaround(bts, input, captures)) {
    JIT_NEXT();
  }
  JIT_BACKTRACK();
}

/**
 * radix / emoji.
 *
 * `PrepareRadix` pushes the initial radix state and enters the body *without* suffix removal, while
 * a `RadixOrEmoji` reached by backtracking first shrinks the window by one code point. that is what
 * enumerates the progressively shorter prefix matches.
 *
 * `arsh_jit_radix_body()` reports which edge to take: `FIRST` continues after the match, `SECOND`
 * steps to the next chained radix instruction, `BACKTRACK` fails. the `FIRST` edge is the
 * instruction `nextOffset` bytes after the radix instruction, which the compiler patches into the
 * `jit_target_radix_match` hole, so the jump stays direct; the `SECOND` edge is the successor.
 */

JIT_STENCIL_DEF(stencil_PrepareRadix) {
  const auto index = JIT_IMM_U16(jit_imm_radix_index);
  const auto emojiFlags = JIT_IMM_U32(jit_imm_radix_emoji);
  const bool hasRadix = JIT_IMM_BOOL(jit_imm_radix_has);
  const auto radixOffset = JIT_IMM_U32(jit_imm_radix_offset);
  if (arsh_jit_prepare_radix(matchers, input, bts, index, emojiFlags, hasRadix) !=
      JIT_ACTION_FIRST) {
    JIT_STACK_LIMIT();
  }
  switch (arsh_jit_radix_body(matchers, input, bts, ctx->foldBuf, index, emojiFlags, hasRadix,
                              radixOffset, JIT_IMM_BOOL(jit_imm_radix_next), false)) {
  case JIT_ACTION_FIRST:
    JIT_TAIL(jit_target_radix_match);
  case JIT_ACTION_SECOND:
    JIT_TAIL(jit_target_radix_next);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

/**
 * `RadixOrEmoji` is reached by backtracking, so the window is shrunk by one code point first. its
 * `SECOND` edge is its own successor, the next chained radix instruction.
 */
JIT_STENCIL_DEF(stencil_RadixOrEmoji) {
  const auto index = JIT_IMM_U16(jit_imm_radix_index);
  const auto emojiFlags = JIT_IMM_U32(jit_imm_radix_emoji);
  const bool hasRadix = JIT_IMM_BOOL(jit_imm_radix_has);
  const auto radixOffset = JIT_IMM_U32(jit_imm_radix_offset);
  switch (arsh_jit_radix_body(matchers, input, bts, ctx->foldBuf, index, emojiFlags, hasRadix,
                             radixOffset, JIT_IMM_BOOL(jit_imm_radix_next), true)) {
  case JIT_ACTION_FIRST:
    JIT_TAIL(jit_target_radix_match);
  case JIT_ACTION_SECOND:
    JIT_NEXT();
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

JIT_STENCIL_DEF(stencil_PrepareLBRadix) {
  const auto index = JIT_IMM_U16(jit_imm_radix_index);
  const auto emojiFlags = JIT_IMM_U32(jit_imm_radix_emoji);
  const bool hasRadix = JIT_IMM_BOOL(jit_imm_radix_has);
  const auto radixOffset = JIT_IMM_U32(jit_imm_radix_offset);
  if (arsh_jit_prepare_lb_radix(matchers, input, bts, index, emojiFlags, hasRadix) !=
      JIT_ACTION_FIRST) {
    JIT_STACK_LIMIT();
  }
  switch (arsh_jit_lb_radix_body(matchers, input, bts, ctx->foldBuf, index, emojiFlags, hasRadix,
                                 radixOffset, JIT_IMM_BOOL(jit_imm_radix_next), false)) {
  case JIT_ACTION_FIRST:
    JIT_TAIL(jit_target_radix_match);
  case JIT_ACTION_SECOND:
    JIT_TAIL(jit_target_radix_next);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

/**
 * the look-behind counterpart of `RadixOrEmoji`.
 */
JIT_STENCIL_DEF(stencil_LBRadixOrEmoji) {
  const auto index = JIT_IMM_U16(jit_imm_radix_index);
  const auto emojiFlags = JIT_IMM_U32(jit_imm_radix_emoji);
  const bool hasRadix = JIT_IMM_BOOL(jit_imm_radix_has);
  const auto radixOffset = JIT_IMM_U32(jit_imm_radix_offset);
  switch (arsh_jit_lb_radix_body(matchers, input, bts, ctx->foldBuf, index, emojiFlags, hasRadix,
                                 radixOffset, JIT_IMM_BOOL(jit_imm_radix_next), true)) {
  case JIT_ACTION_FIRST:
    JIT_TAIL(jit_target_radix_match);
  case JIT_ACTION_SECOND:
    JIT_NEXT();
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

} // namespace arsh::regex::jit
