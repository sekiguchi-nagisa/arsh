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
#include "../matcher.h"
#include "../regex.h"
#include "jit_code.h"
#include "jit_context.h"

namespace arsh::regex::jit {

namespace {

/**
 * the block of the instruction at `byteOffset`.
 */
JitFn blockAt(const JitContext &ctx, const size_t byteOffset) {
  return reinterpret_cast<JitFn>(const_cast<uint8_t *>(ctx.codeBase) + ctx.codeOffsets[byteOffset]);
}

} // namespace

/**
 * the failure trampoline referenced by the `JIT_BACKTRACK()` holes.
 *
 * a failing stencil tail-calls this instead of returning to the driver, so the failure / backtrack
 * cycle stays inside the JIT code and the machine stack does not grow: the `musttail` call below is
 * what makes this a loop rather than a recursion (a failing loop would otherwise grow the stack once
 * per backtrack).
 *
 * it returns `JIT_BACKTRACK_STATUS` once the backtrack stack runs out, i.e. when the current search
 * attempt is exhausted and the driver has to advance the search start.
 *
 * the driver does not push the interpreter's `Backtrack::dummy()`: the interpreter needs it only to
 * enter its `while (bts.backtrack(...))` loop, whereas the driver calls the first block directly.
 */
extern "C" JIT_STENCIL_CALL int32_t jit_backtrack(JitContext *ctx, Input *input, BacktrackStack *bts,
                                                  Capture *captures, LoopState *loops,
                                                  const Matcher *matchers) noexcept {
  (void)matchers;
  while (true) {
    // the target lives in `ctx`, not in a local: see `JitContext::btTarget`
    auto &target = ctx->btTarget;
    if (!bts->backtrack(target, *input, captures, loops)) {
      return JIT_BACKTRACK_STATUS;
    }
    if (unlikely(++ctx->btCount == Regex::TIMER_CHECK_INTERVAL)) {
      ctx->btCount = 0;
      if (ctx->timer) {
        switch (ctx->timer->check()) {
        case Timer::Status::None:
          break;
        case Timer::Status::Canceled:
          return JIT_CANCEL_STATUS;
        case Timer::Status::Expired:
          return JIT_TIMEOUT_STATUS;
        }
      }
    }
    const auto byteOffset =
        static_cast<size_t>(reinterpret_cast<const char *>(target) - ctx->instSeqBase);
    JIT_STENCIL_TAIL return blockAt(*ctx, byteOffset)(ctx, input, bts, captures, loops, matchers);
  }
}

MatchStatus jitMatch(MatchContext &ctx, const JitCode &code, const ObserverPtr<Timer> timer) {
  Input input = ctx.copyInput();
  const auto *const instSeqBegin = ctx.getInst();
  ctx.clearCaptures();
  auto *const captures = ctx.getCaptures();
  auto *const loops = ctx.getLoops();
  std::string foldBuf;
  if (timer) {
    timer->start();
  }

  JitContext jitCtx;
  jitCtx.ctx = &ctx;
  jitCtx.foldBuf = &foldBuf;
  jitCtx.instSeqBase = reinterpret_cast<const char *>(instSeqBegin);
  jitCtx.codeBase = code.code();
  jitCtx.codeOffsets = code.codeOffsets.data();
  jitCtx.timer = timer.get();

  const char *attemptStart = input.getIter();
  while (true) {
    // a fresh attempt gets a fresh backtrack stack
    BacktrackStack bts(instSeqBegin);
    const Inst *inst = instSeqBegin;
    bool exhausted = false; // the fast path below found no candidate at all

    // search string (mirrors the interpreter's leading-literal fast path). `attemptStart` tracks
    // where the attempt started, which is what the `Match` instruction captures.
    if (inst->op == OpCode::Char || inst->op == OpCode::String) {
      char data[4];
      StringRef needle;
      if (inst->op == OpCode::Char) {
        const int codePoint = cast<CharIns>(*inst).getCodePoint();
        const unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data);
        needle = StringRef(data, len);
        inst += sizeof(CharIns);
      } else {
        needle = ctx.getMatchers()[cast<StringIns>(*inst).getIndex()].asStrRef();
        inst += sizeof(StringIns);
      }
      const auto retPos = input.remainForward().find(needle);
      if (retPos == StringRef::npos) {
        attemptStart = input.getEnd();
        exhausted = true; // nothing was pushed, so there is nothing to backtrack into
      } else {
        attemptStart = input.getIter() + retPos;
        input.setIter(input.getIter() + retPos + needle.size());
      }
    } else if (inst->op == OpCode::CharSet) {
      const auto &ins = cast<CharSetIns>(*inst);
      const auto index = ins.getMatcherIndex();
      const bool invert = ins.invert;
      bool found = false;
      inst += sizeof(CharSetIns);
      while (input.available()) {
        attemptStart = input.getIter();
        if (ctx.getMatchers()[index].contains(input.consumeForward()) != invert) {
          found = true;
          break;
        }
      }
      exhausted = !found; // nothing was pushed, so there is nothing to backtrack into
    }

    if (!exhausted) {
      // match
      //
      // a failing block never returns here: it resumes matching by tail-calling `jit_backtrack`,
      // which runs the backtrack stack. the driver is only re-entered when a block matched, hit the
      // stack limit, exhausted the attempt, or the timer fired.
      //
      // `matchStart` is assigned only now: the fast path above may have moved the attempt start.
      jitCtx.matchStart = attemptStart;
      const auto byteOffset = static_cast<size_t>(reinterpret_cast<const char *>(inst) -
                                                  reinterpret_cast<const char *>(instSeqBegin));
      switch (blockAt(jitCtx, byteOffset)(&jitCtx, &input, &bts, captures, loops,
                                          ctx.getMatchers().begin())) {
      case JIT_MATCH_STATUS:
        return MatchStatus::OK;
      case JIT_STACK_LIMIT_STATUS:
        return MatchStatus::STACK_LIMIT;
      case JIT_TIMEOUT_STATUS:
        return MatchStatus::TIMEOUT;
      case JIT_CANCEL_STATUS:
        return MatchStatus::CANCEL;
      default:
        break; // the attempt is exhausted: advance the search start
      }
    }

    // advance the search start and retry, until the end of the input
    input.setIter(attemptStart);
    if (!input.available()) {
      break;
    }
    input.consumeForward();
    attemptStart = input.getIter();
    ctx.clearCaptures();
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
}

} // namespace arsh::regex::jit
