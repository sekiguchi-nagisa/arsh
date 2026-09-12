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

#include "jit_match.h"

#include <string>

#include "../backtrack.h"
#include "../instruction.h"
#include "../regex.h"
#include "jit_code.h"
#include "jit_context.h"

namespace arsh::regex::jit {

/**
 * the control flow trampoline referenced by the `jit_goto` holes.
 *
 * the compiler leaves `jit_goto` undefined, so the hole is patched with this address. a block only
 * calls it for a branch target that is not its own successor, which keeps the successor edges direct.
 *
 * the tail call is required: without it a loop back edge would grow the machine stack once per
 * iteration, and a pattern like `(a)*` on a long input would overflow it.
 */
extern "C" JIT_STENCIL_CALL int32_t jit_goto(JitContext &ctx, const Inst *ins) noexcept {
  const auto byteOffset =
      static_cast<size_t>(reinterpret_cast<const char *>(ins) - ctx.instSeqBase);
  const auto fn = reinterpret_cast<JitFn>(const_cast<uint8_t *>(ctx.codeBase) +
                                          ctx.codeOffsets[byteOffset]);
  JIT_STENCIL_TAIL return fn(ctx, ins);
}

/**
 * the control flow trampoline referenced by the `JIT_BACKTRACK()` holes.
 *
 * the driver only runs the backtrack stack when a block returns to it, so a failing block instead
 * tail-calls this trampoline: it runs the backtrack stack and tail-calls the resolved block. the
 * whole failure/backtrack cycle therefore stays inside the JIT code, which is the point of the
 * copy-and-patch engine.
 *
 * it returns `JIT_BACKTRACK_STATUS` only once the backtrack stack is empty, i.e. when the current
 * search attempt is exhausted and the driver has to advance the search start.
 *
 * `inst` is the failing block's own instruction. it is the target whenever an entry pops without
 * setting one (a `BacktrackOp::None`), which makes the block run once more. that is what the
 * interpreter's initial `Backtrack::dummy()` does, and the re-run fails again, so the attempt then
 * ends. taking `inst` also lets the call from a stencil be a `musttail` one, since the signature
 * then matches a stencil's.
 */
extern "C" JIT_STENCIL_CALL int32_t jit_backtrack(JitContext &ctx, const Inst *inst) noexcept {
  // `inst` seeds `target` so that it can never be left null: a `BacktrackOp::None` entry pops
  // without writing a target, and re-running the failing block is what the driver did for it.
  const Inst *target = inst;
  while (ctx.bts->backtrack(target, *ctx.input, ctx.captures, ctx.loopStates)) {
    if (unlikely(++ctx.btCount == Regex::TIMER_CHECK_INTERVAL)) {
      ctx.btCount = 0;
      if (ctx.timer) {
        switch (ctx.timer->check()) {
        case Timer::Status::None:
          break;
        case Timer::Status::Canceled:
          return JIT_CANCEL_STATUS;
        case Timer::Status::Expired:
          return JIT_TIMEOUT_STATUS;
        }
      }
    }

    // `matchStart` is the start of the current attempt and is not affected by backtracking (the
    // interpreter only updates its counterpart in `START` and when the search start advances), so
    // it is intentionally left alone here.

    const auto byteOffset =
        static_cast<size_t>(reinterpret_cast<const char *>(target) - ctx.instSeqBase);
    const auto fn = reinterpret_cast<JitFn>(const_cast<uint8_t *>(ctx.codeBase) +
                                            ctx.codeOffsets[byteOffset]);
    JIT_STENCIL_TAIL return fn(ctx, target);
  }
  return JIT_BACKTRACK_STATUS;
}

} // namespace arsh::regex::jit

namespace arsh::regex::jit {

MatchStatus jitMatch(MatchContext &ctx, const JitCode &code, const ObserverPtr<Timer> timer) {
  // prepare (mirrors `match()` in vm.cpp)
  Input input = ctx.copyInput();
  const char *oldIter = input.getIter();
  const Inst *const instSeqBegin = ctx.getInst();
  const Inst *inst = instSeqBegin;
  const auto matchers = ctx.getMatchers();
  ctx.clearCaptures();
  Capture *captures = ctx.getCaptures();
  LoopState *loopStates = ctx.getLoops();
  BacktrackStack bts(instSeqBegin);
  std::string foldBuf;
  if (timer) {
    timer->start();
  }

  JitContext jitCtx;
  jitCtx.input = &input;
  jitCtx.captures = captures;
  jitCtx.loopStates = loopStates;
  jitCtx.matchers = matchers.begin();
  jitCtx.bts = &bts;
  jitCtx.foldBuf = &foldBuf;
  jitCtx.ctx = &ctx;
  jitCtx.instSeqBase = reinterpret_cast<const char *>(instSeqBegin);
  jitCtx.codeBase = code.code();
  jitCtx.codeOffsets = code.codeOffsets.data();
  jitCtx.timer = timer.get();

START:
  // search string (mirrors the interpreter's leading-literal fast path). `oldIter` tracks the
  // position the current attempt started at, which is what the `Match` instruction captures.
  if (inst->op == OpCode::Char || inst->op == OpCode::String) {
    char data[4];
    StringRef needle;
    if (inst->op == OpCode::Char) {
      const int codePoint = cast<CharIns>(*inst).getCodePoint();
      const unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data);
      needle = StringRef(data, len);
      inst += sizeof(CharIns);
    } else {
      needle = matchers[cast<StringIns>(*inst).getIndex()].asStrRef();
      inst += sizeof(StringIns);
    }
    if (const auto retPos = input.remainForward().find(needle); retPos == StringRef::npos) {
      oldIter = input.getEnd();
      goto ADVANCE; // the backtrack stack is empty here, so there is nothing to backtrack into
    } else {
      oldIter = input.getIter() + retPos;
      input.setIter(input.getIter() + retPos + needle.size());
    }
  } else if (inst->op == OpCode::CharSet) {
    const auto &ins = cast<CharSetIns>(*inst);
    const auto index = ins.getMatcherIndex();
    const bool invert = ins.invert;
    bool matched = false;
    inst += sizeof(CharSetIns);
    while (input.available()) {
      oldIter = input.getIter();
      if (matchers[index].contains(input.consumeForward()) != invert) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      goto ADVANCE; // the backtrack stack is empty here, so there is nothing to backtrack into
    }
  }

  // match
  //
  // a failing block does not return here: it resumes matching on its own by tail-calling
  // `jit_backtrack`, which runs the backtrack stack. the driver is therefore only re-entered when
  // the block matched, hit the stack limit, exhausted the attempt or the timer fired.
  //
  // note that the interpreter's initial `Backtrack::dummy()` is not pushed: it only existed to make
  // the driver's backtrack loop run its body once, which is done by the direct call below.
  //
  // `matchStart` is (re)assigned just before the entry: the leading-literal fast path above may
  // have moved `oldIter` past the search start.
  jitCtx.matchStart = oldIter;
  {
    const auto byteOffset =
        static_cast<size_t>(reinterpret_cast<const char *>(inst) - jitCtx.instSeqBase);
    const auto fn = reinterpret_cast<JitFn>(const_cast<uint8_t *>(code.code()) +
                                            code.codeOffsets[byteOffset]);
    switch (fn(jitCtx, inst)) {
    case JIT_MATCH_STATUS:
      return MatchStatus::OK;
    case JIT_STACK_LIMIT_STATUS:
      return MatchStatus::STACK_LIMIT;
    case JIT_TIMEOUT_STATUS:
      return MatchStatus::TIMEOUT;
    case JIT_CANCEL_STATUS:
      return MatchStatus::CANCEL;
    case JIT_BACKTRACK_STATUS:
      break; // the current attempt is exhausted: advance the search start
    default:
      return MatchStatus::STACK_LIMIT;
    }
  }

ADVANCE:
  // increment input and redo until end-of-input.
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();
    oldIter = input.getIter();
    inst = instSeqBegin;
    ctx.clearCaptures();
    captures = ctx.getCaptures();
    jitCtx.captures = captures;
    goto START;
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
}

} // namespace arsh::regex
