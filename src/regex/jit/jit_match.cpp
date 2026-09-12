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
  unsigned int btCount = 0;
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
  jitCtx.matchStart = oldIter;
  jitCtx.codeBase = code.code();
  jitCtx.codeOffsets = code.codeOffsets.data();

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
      goto BACKTRACK;
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
      goto BACKTRACK;
    }
  }

  // match
  bts.push(Backtrack::dummy()); // dummy
BACKTRACK:
  while (bts.backtrack(inst, input, captures, loopStates)) {
    if (unlikely(++btCount == Regex::TIMER_CHECK_INTERVAL)) {
      btCount = 0;
      if (timer) {
        switch (timer->check()) {
        case Timer::Status::None:
          break;
        case Timer::Status::Canceled:
          return MatchStatus::CANCEL;
        case Timer::Status::Expired:
          return MatchStatus::TIMEOUT;
        }
      }
    }

    // the `Match` stencil captures the start of the current attempt: it is the counterpart of the
    // interpreter's `oldIter`. keeping it in sync here (instead of at each update site) also covers
    // the `goto BACKTRACK` above.
    jitCtx.matchStart = oldIter;

    const auto byteOffset =
        static_cast<size_t>(reinterpret_cast<const char *>(inst) - jitCtx.instSeqBase);
    const auto fn = reinterpret_cast<JitFn>(const_cast<uint8_t *>(code.code()) +
                                            code.codeOffsets[byteOffset]);
    switch (fn(jitCtx, inst)) {
    case JIT_MATCH_STATUS:
      return MatchStatus::OK;
    case JIT_STACK_LIMIT_STATUS:
      return MatchStatus::STACK_LIMIT;
    case JIT_BACKTRACK_STATUS:
      break; // run the backtrack stack
    default:
      return MatchStatus::STACK_LIMIT;
    }
  }

  // increment input and redo until end-of-input.
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();
    oldIter = input.getIter();
    inst = instSeqBegin;
    ctx.clearCaptures();
    captures = ctx.getCaptures();
    jitCtx.captures = captures;
    jitCtx.matchStart = oldIter;
    goto START;
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
}

} // namespace arsh::regex
