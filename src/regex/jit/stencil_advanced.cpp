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
 */

JIT_STENCIL_DEF(stencil_String) {
  const auto index = JIT_INST(StringIns).getIndex();
  if (arsh_jit_expect_forward(ctx.input, ctx.matchers, index)) {
    JIT_NEXT_TYPE(StringIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBString) {
  const auto index = JIT_INST(LBStringIns).getIndex();
  if (arsh_jit_expect_backward(ctx.input, ctx.matchers, index)) {
    JIT_NEXT_TYPE(LBStringIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_BeginCapture) {
  const auto index = JIT_INST(BeginCaptureIns).getCaptureIndex();
  Capture *const captures = ctx.captures;
  captures[index] = {.offset = ctx.input->getOffset(), .size = 0};
  JIT_TRY(arsh_jit_push_set_capture(ctx.bts, index, &CAPTURE_UNSET));
  JIT_NEXT_TYPE(BeginCaptureIns);
}

JIT_STENCIL_DEF(stencil_EndCapture) {
  const auto index = JIT_INST(EndCaptureIns).getCaptureIndex();
  Capture *const captures = ctx.captures;
  auto &capture = captures[index];
  capture.size = ctx.input->getOffset() - capture.offset;
  JIT_NEXT_TYPE(EndCaptureIns);
}

JIT_STENCIL_DEF(stencil_LBEndCapture) {
  const auto index = JIT_INST(LBEndCaptureIns).getCaptureIndex();
  Capture *const captures = ctx.captures;
  auto &capture = captures[index];
  const uint32_t actualEndOffset = capture.endOffset();
  capture.offset = ctx.input->getOffset();
  capture.size = actualEndOffset - capture.offset;
  JIT_NEXT_TYPE(LBEndCaptureIns);
}

JIT_STENCIL_DEF(stencil_ResetCaptures) {
  const auto &ins = JIT_INST(ResetCapturesIns);
  JIT_TRY(arsh_jit_push_reset_captures(ctx.bts, ctx.captures, ins.getFirstIndex(),
                                       ins.getLastIndex()));
  JIT_NEXT_TYPE(ResetCapturesIns);
}

JIT_STENCIL_DEF(stencil_BackRef) {
  const auto &ins = JIT_INST(BackRefIns);
  Capture capture;
  if (ins.named) {
    arsh_jit_resolve_named_backref(ctx.ctx, ins.getRefIndex(), &capture);
  } else {
    capture = ctx.captures[ins.getRefIndex()];
  }
  if (arsh_jit_backref_forward(ctx.input, &capture, ctx.input->getBegin())) {
    JIT_NEXT_TYPE(BackRefIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_IBackRef) {
  const auto &ins = JIT_INST(IBackRefIns);
  Capture capture;
  if (ins.named) {
    arsh_jit_resolve_named_backref(ctx.ctx, ins.getRefIndex(), &capture);
  } else {
    capture = ctx.captures[ins.getRefIndex()];
  }
  if (arsh_jit_ibackref_forward(ctx.input, &capture, ctx.input->getBegin())) {
    JIT_NEXT_TYPE(IBackRefIns);
  }
  JIT_BACKTRACK();
}

JIT_STENCIL_DEF(stencil_LBBackRef) {
  const auto &ins = JIT_INST(LBBackRefIns);
  Capture capture;
  if (ins.named) {
    arsh_jit_resolve_named_backref(ctx.ctx, ins.getRefIndex(), &capture);
  } else {
    capture = ctx.captures[ins.getRefIndex()];
  }
  if (arsh_jit_lbbackref_backward(ctx.input, &capture, ctx.input->getBegin(),
                                  ins.ignoreCase)) {
    JIT_NEXT_TYPE(LBBackRefIns);
  }
  JIT_BACKTRACK();
}

/**
 * `BeginLoop` resets the loop state, then runs the shared loop step.
 */
JIT_STENCIL_DEF(stencil_BeginLoop) {
  const auto &ins = JIT_INST(BeginLoopIns);
  ctx.loopStates[ins.getLoopIndex()] = LoopState();
  const Inst *next = nullptr;
  switch (arsh_jit_loop_step(&ctx, &ins, &next)) {
  case JIT_ACTION_FIRST:
    JIT_GOTO_INST(next);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

/**
 * `EndLoop` jumps back to the associated `BeginLoop` and runs the loop step *without* resetting the
 * loop state (that only happens when the loop is entered from the top). note that this must not jump
 * to the `BeginLoop` stencil itself, since that would reset the counter on every iteration.
 */
JIT_STENCIL_DEF(stencil_EndLoop) {
  const auto &ins = JIT_INST(EndLoopIns);
  const auto *beginIns =
      reinterpret_cast<const BeginLoopIns *>(ctx.instSeqBase + ins.getTarget());
  const Inst *next = nullptr;
  switch (arsh_jit_loop_step(&ctx, beginIns, &next)) {
  case JIT_ACTION_FIRST:
    JIT_GOTO_INST(next);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

/**
 * `[BeginLookAround, EndLookAround)` is run with the look-around state pushed on the backtrack
 * stack. the interpreter jumps straight to `EndLookAround` (skipping the body) at
 * `BeginLookAround`, then runs the body. we do the same.
 */
JIT_STENCIL_DEF(stencil_BeginLookAround) {
  const auto &ins = JIT_INST(BeginLookAroundIns);
  JIT_TRY(arsh_jit_push_lookaround(ctx.bts, ctx.input, ins.getTarget(), ins.negate));
  JIT_NEXT_TYPE(BeginLookAroundIns);
}

JIT_STENCIL_DEF(stencil_EndLookAround) {
  if (arsh_jit_cleanup_lookaround(ctx.bts, ctx.input, ctx.captures)) {
    JIT_NEXT_TYPE(EndLookAroundIns);
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
 * `arsh_jit_radix_body()` reports which edge to take: `FIRST` continues after the match,
 * `SECOND` steps to the next chained radix instruction, `BACKTRACK` fails.
 */

JIT_STENCIL_DEF(stencil_PrepareRadix) {
  const auto *radixIns =
      reinterpret_cast<const RadixOrEmojiIns *>(JIT_ADVANCE(sizeof(PrepareRadixIns)));
  if (arsh_jit_prepare_radix(&ctx, radixIns) != JIT_ACTION_FIRST) {
    JIT_STACK_LIMIT();
  }
  switch (arsh_jit_radix_body(&ctx, radixIns, false)) {
  case JIT_ACTION_FIRST:
    JIT_GOTO_INST(JIT_ADV_FROM(radixIns, sizeof(RadixOrEmojiIns) + radixIns->nextOffset));
  case JIT_ACTION_SECOND:
    JIT_GOTO_INST(JIT_ADV_FROM(radixIns, sizeof(RadixOrEmojiIns)));
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

JIT_STENCIL_DEF(stencil_RadixOrEmoji) {
  const auto &ins = JIT_INST(RadixOrEmojiIns);
  switch (arsh_jit_radix_body(&ctx, &ins, true)) {
  case JIT_ACTION_FIRST:
    JIT_GOTO_ADV(sizeof(RadixOrEmojiIns) + ins.nextOffset);
  case JIT_ACTION_SECOND:
    JIT_NEXT_TYPE(RadixOrEmojiIns);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

JIT_STENCIL_DEF(stencil_PrepareLBRadix) {
  const auto *radixIns =
      reinterpret_cast<const LBRadixOrEmojiIns *>(JIT_ADVANCE(sizeof(PrepareLBRadixIns)));
  if (arsh_jit_prepare_lb_radix(&ctx, radixIns) != JIT_ACTION_FIRST) {
    JIT_STACK_LIMIT();
  }
  switch (arsh_jit_lb_radix_body(&ctx, radixIns, false)) {
  case JIT_ACTION_FIRST:
    JIT_GOTO_INST(JIT_ADV_FROM(radixIns, sizeof(LBRadixOrEmojiIns) + radixIns->nextOffset));
  case JIT_ACTION_SECOND:
    JIT_GOTO_INST(JIT_ADV_FROM(radixIns, sizeof(LBRadixOrEmojiIns)));
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

JIT_STENCIL_DEF(stencil_LBRadixOrEmoji) {
  const auto &ins = JIT_INST(LBRadixOrEmojiIns);
  switch (arsh_jit_lb_radix_body(&ctx, &ins, true)) {
  case JIT_ACTION_FIRST:
    JIT_GOTO_ADV(sizeof(LBRadixOrEmojiIns) + ins.nextOffset);
  case JIT_ACTION_SECOND:
    JIT_NEXT_TYPE(LBRadixOrEmojiIns);
  case JIT_ACTION_BACKTRACK:
    JIT_BACKTRACK();
  default:
    JIT_STACK_LIMIT();
  }
}

} // namespace arsh::regex::jit
