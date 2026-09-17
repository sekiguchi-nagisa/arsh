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

#include <algorithm>

#include "instruction.h"
#include "match_context.h"
#include "matcher.h"
#include "misc/rtti.hpp"
#include "regex.h"
#include "unicode/case_fold.h"
#include "unicode/grapheme.h"

#include "vm_common.h"

#ifdef ARSH_USE_TAILCALL_VM

namespace arsh::regex {

/**
 * tail-call interpreter.
 *
 * each opcode is handled by an independent function and the control flow between them is a
 * mutual tail call, so no stack growth occurs per instruction (guaranteed at compile time by
 * the `musttail` attribute).
 *
 * the `preserve_none` calling convention passes the arguments in registers instead of on the
 * stack. it also never saves the callee-saved registers, which is sound here because the whole
 * dispatch chain uses this convention uniformly, so nothing has to be preserved across a
 * dispatch.
 *
 * NOTE: the attributes must be written in the prefix position (before the return type).
 * writing them after the parameter list makes some compilers silently ignore them.
 *
 * the handler signature passes 6 arguments in registers (see TailCallState for the rest of
 * the VM state). `preserve_none` provides up to 12 registers on x86-64, but GCC only exposes
 * 6 of them (r12-r15 are reserved as scratch) and silently spills the rest to the stack on
 * every dispatch, which is far slower than the switch/threaded-code interpreter. clang does
 * expose all 12, but the argument count must stay within the common denominator of both.
 */
#define ARSH_PRESERVE_NONE __attribute__((preserve_none))
#define ARSH_MUSTTAIL __attribute__((musttail))
#define ARSH_NOINLINE __attribute__((noinline))

/**
 * the VM state which is not passed in a register.
 * it lives in the `match` frame and is shared by all handlers via pointer.
 *
 * `begin`/`end`/`iter` are kept out of here, since the bounds checks are the hottest
 * operation of every handler.
 */
struct TailCallState {
  Input input;                      // mirrors the current position (for the Input based helpers)
  const char *oldIter{nullptr};     // start position of the current match attempt
  Capture *captures{nullptr};       // capture array
  LoopState *loopStates{nullptr};   // loop state array
  BacktrackStack bts;               // backtracking stack
  const Matcher *matchers{nullptr}; // matcher array
  MatchContext *ctx{nullptr};       // for the named backref resolution and the final input sync
  ObserverPtr<Timer> timer;         // for the cancellation/timeout
  std::string foldBuf;              // scratch buffer for the case-folded radix/emoji matching
  uint32_t btCount{0};              // backtrack counter for the timer check interval

  explicit TailCallState(const Inst *start) : bts(start) {}
};

/**
 * the handler signature.
 * `preserve_none` is written on the function pointer type as well, so the calling convention
 * mismatch is diagnosed at compile time.
 */
using Handler = ARSH_PRESERVE_NONE MatchStatus (*)(const Inst *pc, const char *iter,
                                                   const char *begin, const char *end,
                                                   const Matcher *matchers, TailCallState *st);

enum : unsigned {
  kOpCodeCount = 0
#define GEN_OPCODE_COUNT(E) +1
  EACH_RE_OPCODE(GEN_OPCODE_COUNT)
#undef GEN_OPCODE_COUNT
};

extern const Handler kDispatchTable[kOpCodeCount];

// every handler takes the full VM state so that the dispatch table has a uniform signature;
// each handler uses only a subset of it.
#define ARSH_VM_PARAMS                                                                             \
  [[maybe_unused]] const Inst *pc, [[maybe_unused]] const char *iter,                              \
      [[maybe_unused]] const char *begin, [[maybe_unused]] const char *end,                        \
      [[maybe_unused]] const Matcher *matchers, [[maybe_unused]] TailCallState *st

#define ARSH_VM_HANDLER(NAME) static ARSH_PRESERVE_NONE MatchStatus NAME(ARSH_VM_PARAMS)

// dispatch the instruction `PC` with the current position
#define VM_TAILCALL(PC)                                                                            \
  do {                                                                                             \
    const Inst *const vmVmPc = (PC);                                                               \
    ARSH_MUSTTAIL return kDispatchTable[toUnderlying(vmVmPc->op)](vmVmPc, iter, begin, end,        \
                                                                  matchers, st);                   \
  } while (false)

// dispatch the instruction `PC` with the position `ITER`
#define VM_TAILCALL_AT(PC, ITER)                                                                   \
  do {                                                                                             \
    const Inst *const vmVmPc = (PC);                                                               \
    const char *const vmVmIter = (ITER);                                                           \
    ARSH_MUSTTAIL return kDispatchTable[toUnderlying(vmVmPc->op)](vmVmPc, vmVmIter, begin, end,    \
                                                                  matchers, st);                   \
  } while (false)

#define VM_NEXT(TYPE) VM_TAILCALL(pc + sizeof(TYPE))

// equivalent to `goto BACKTRACK` of the switch/threaded-code interpreter
#define VM_BACKTRACK_AT(ITER)                                                                      \
  do {                                                                                             \
    ARSH_MUSTTAIL return dispatchBacktrack(pc, (ITER), begin, end, matchers, st);                  \
  } while (false)

#define VM_BACKTRACK() VM_BACKTRACK_AT(iter)

#define VM_TRY(E)                                                                                  \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return MatchStatus::STACK_LIMIT;                                                             \
    }                                                                                              \
  } while (false)

#define VM_SYNC_INPUT(ITER)                                                                        \
  do {                                                                                             \
    st->input.setIter(ITER);                                                                       \
    st->ctx->syncInput(st->input);                                                                 \
  } while (false)

/**
 * NOTE: the following are exactly equivalent to the corresponding Input methods
 * (e.g. `available()` is `iter != end`), but avoid touching the Input object, since the
 * position is kept in a register by the tail-call interpreter.
 */
[[gnu::always_inline]] inline bool available(const char *iter, const char *end) {
  return iter != end;
}

[[gnu::always_inline]] inline bool availableBackward(const char *iter, const char *begin) {
  return iter != begin;
}

[[gnu::always_inline]] inline int cur(const char *iter) {
  auto i = iter;
  return unsafeNextUtf8(i);
}

[[gnu::always_inline]] inline int prev(const char *iter) {
  auto i = iter;
  return unsafePrevUtf8(i);
}

/**
 * the position just before `iter`.
 * NOTE: GCC warns (`-Wmaybe-musttail-local-addr`) when the address of a local is passed to a
 * function and that local is then used in a `musttail` call, since it cannot prove the address
 * does not escape. the helpers here take and return only values, so the handlers can keep the
 * position in a plain local.
 */
[[gnu::always_inline]] inline const char *prevPos(const char *iter) {
  unsafePrevUtf8(iter);
  return iter;
}

[[gnu::always_inline]] inline const char *nextPos(const char *iter) {
  return iter + UnicodeUtil::utf8ByteSize(*iter);
}

/**
 * the search-string fast path for the leading Char/String/CharSet instruction.
 * this is the `START:` of the switch/threaded-code interpreter.
 *
 * on success, `pc`/`iter` point just after the matched literal and `oldIter` holds the start
 * position of the attempt. if the literal cannot be found, `iter` is left at the end of the
 * input so that the subsequent backtracking moves on to the next attempt (or to FAIL).
 *
 * @return true if the following instruction must be dispatched. false if the caller has to
 * backtrack instead (in this case the dummy entry must not be pushed).
 */
inline bool searchForward(const Inst *&pc, const char *&iter, const char *&oldIter, const char *end,
                          const Matcher *matchers) {
  if (pc->op == OpCode::Char || pc->op == OpCode::String) {
    char data[4];
    StringRef needle;
    if (pc->op == OpCode::Char) {
      int codePoint = cast<CharIns>(*pc).getCodePoint();
      unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data);
      needle = StringRef(data, len);
      pc += sizeof(CharIns);
    } else {
      needle = matchers[cast<StringIns>(*pc).getIndex()].asStrRef();
      pc += sizeof(StringIns);
    }
    const StringRef remain(iter, static_cast<size_t>(end - iter));
    const auto foundPos = remain.find(needle);
    if (foundPos == StringRef::npos) {
      oldIter = end;
      iter = end;
      return false;
    }
    oldIter = iter + foundPos;
    iter = oldIter + needle.size();
    return true;
  }
  if (pc->op == OpCode::CharSet) {
    const auto &charSetIns = cast<CharSetIns>(*pc);
    const unsigned int index = charSetIns.getMatcherIndex();
    const bool invert = charSetIns.invert;
    pc += sizeof(CharSetIns);
    while (available(iter, end)) {
      oldIter = iter;
      if (matchers[index].contains(unsafeNextUtf8(iter)) != invert) {
        return true;
      }
    }
    return false;
  }
  return true;
}

/**
 * run the search-string fast path and push the dummy entry that makes the backtracking return
 * to the current instruction.
 */
inline void prepareMatch(const Inst *&pc, const char *&iter, TailCallState &st, const char *end,
                         const Matcher *matchers) {
  if (searchForward(pc, iter, st.oldIter, end, matchers)) {
    st.bts.push(Backtrack::dummy()); // dummy
  }
}

/**
 * the backtrack stack is exhausted, so move the start position forward and retry.
 *
 * NOTE: every exit of this function is either a tail call to a handler or a plain return.
 * that keeps its caller (`dispatchBacktrack`, which runs on every backtrack) free of any
 * non-tail call, so the caller needs no frame and does not have to spill the instruction
 * pointer and the input position.
 *
 * @return FAIL if the end of the input is reached
 */
[[gnu::cold]] static ARSH_PRESERVE_NONE MatchStatus ARSH_NOINLINE rescanHandler(ARSH_VM_PARAMS);

/**
 * the `BACKTRACK:` of the switch/threaded-code interpreter.
 * all handlers land here via a tail call when the current path fails.
 *
 * NOTE: this is deliberately a straight-line function. `backtrack()` is inlined (it is a
 * single jump table) and every exit is a tail call, so it needs no prologue and does not spill
 * the live values. routing the exhausted case through an ordinary (non-tail) helper instead
 * costs a frame plus the saving of the live registers on every backtrack, which is what
 * dominates the backtrack-heavy patterns.
 *
 * NOTE: it must stay out of line (`ARSH_NOINLINE`), and so must `rescanHandler` above.
 * otherwise the cold rescan (and the container growth path of `push()`) is pulled in, which
 * makes this function allocate a large frame and save the callee-saved registers on entry,
 * i.e. on every single backtrack.
 */
static ARSH_PRESERVE_NONE MatchStatus ARSH_NOINLINE
dispatchBacktrack(const Inst *pcIn, const char *iterIn, const char *begin, const char *end,
                  const Matcher *matchers, TailCallState *st) {
  // `backtrack()` consumes the SetCapture/SetLoopState/RadixState entries internally and
  // returns as soon as it finds a dispatch point.
  const auto ret = st->bts.backtrack(pcIn, begin, iterIn, st->captures, st->loopStates);
  if (unlikely(!ret.found())) {
    ARSH_MUSTTAIL return rescanHandler(pcIn, ret.iter, begin, end, matchers, st);
  }
  const Inst *pc = ret.inst; // NOTE: a NonGreedyLoop entry lands on the loop body, not on pcIn
  const char *iter = ret.iter;
  if (unlikely(++st->btCount == Regex::TIMER_CHECK_INTERVAL)) {
    st->btCount = 0;
    if (st->timer) {
      switch (st->timer->check()) {
      case Timer::Status::None:
        break;
      case Timer::Status::Canceled:
        return MatchStatus::CANCEL;
      case Timer::Status::Expired:
        return MatchStatus::TIMEOUT;
      }
    }
  }
  ARSH_MUSTTAIL return kDispatchTable[toUnderlying(pc->op)](pc, iter, begin, end, matchers, st);
}

[[gnu::cold]] static ARSH_PRESERVE_NONE MatchStatus ARSH_NOINLINE rescanHandler(ARSH_VM_PARAMS) {
  for (;;) {
    const char *iter = st->oldIter;
    if (!available(iter, end)) {
      st->input.setIter(iter);
      st->ctx->syncInput(st->input);
      return MatchStatus::FAIL;
    }
    iter += UnicodeUtil::utf8ByteSize(*iter);
    st->oldIter = iter;
    st->input.setIter(iter);
    const Inst *pc = st->bts.getStartInst();
    st->ctx->clearCaptures();
    st->captures = st->ctx->getCaptures();
    prepareMatch(pc, iter, *st, end, matchers);
    const auto ret = st->bts.backtrack(pc, begin, iter, st->captures, st->loopStates);
    if (ret.found()) {
      VM_TAILCALL_AT(ret.inst, ret.iter);
    }
    // the literal was not found here, or the fresh dummy entry was consumed immediately,
    // so move on to the next start position.
  }
}

//============================================================================//
//     control / assertion / any character                                    //
//============================================================================//

ARSH_VM_HANDLER(hNop) { VM_NEXT(NopIns); }

ARSH_VM_HANDLER(hMatch) {
  st->captures[0].offset = static_cast<uint32_t>(st->oldIter - begin);
  st->captures[0].size = static_cast<uint32_t>(iter - st->oldIter);
  VM_SYNC_INPUT(iter);
  return MatchStatus::OK;
}

ARSH_VM_HANDLER(hJump) { VM_TAILCALL(st->bts.getStartInst() + cast<JumpIns>(*pc).getTarget()); }

ARSH_VM_HANDLER(hAlt) {
  VM_TRY(st->bts.push(Backtrack::newSetIns(iter, cast<AltIns>(*pc).getSecond())));
  VM_NEXT(AltIns);
}

ARSH_VM_HANDLER(hStart) {
  auto &ins = cast<StartIns>(*pc);
  if (iter == begin || (ins.multiline && isLineTerminator(prev(iter)))) {
    VM_NEXT(StartIns);
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hEnd) {
  auto &ins = cast<EndIns>(*pc);
  if (iter == end || (ins.multiline && isLineTerminator(cur(iter)))) {
    VM_NEXT(EndIns);
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hWord) {
  const bool invert = cast<WordIns>(*pc).invert;
  const bool prevIsWord = iter != begin && isWord(prev(iter));
  const bool curIsWord = iter != end && isWord(cur(iter));
  if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
    VM_NEXT(WordIns);
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hIWord) {
  const bool invert = cast<IWordIns>(*pc).invert;
  const bool prevIsWord = iter != begin && isExtendWord(prev(iter));
  const bool curIsWord = iter != end && isExtendWord(cur(iter));
  if (invert ? prevIsWord == curIsWord : prevIsWord != curIsWord) {
    VM_NEXT(IWordIns);
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hAny) {
  if (available(iter, end)) {
    VM_TAILCALL_AT(pc + sizeof(AnyIns), nextPos(iter));
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hAnyExceptNL) {
  if (available(iter, end) && !isLineTerminator(cur(iter))) {
    VM_TAILCALL_AT(pc + sizeof(AnyExceptNLIns), nextPos(iter));
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hLBAny) {
  if (availableBackward(iter, begin)) {
    const int codePoint = prev(iter);
    if (cast<LBAnyIns>(*pc).dotAll || !isLineTerminator(codePoint)) {
      VM_TAILCALL_AT(pc + sizeof(LBAnyIns), prevPos(iter));
    }
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hGrapheme) {
  if (available(iter, end)) {
    const StringRef ref(iter, static_cast<size_t>(end - iter));
    size_t byteSize = 0;
    iterateGraphemeUntil(ref, 1, true, [&byteSize](const GraphemeCluster &grapheme) {
      byteSize = grapheme.getRef().size();
    });
    VM_TAILCALL_AT(pc + sizeof(GraphemeIns), iter + byteSize);
  }
  VM_BACKTRACK();
}

//============================================================================//
//     character / string                                                     //
//============================================================================//

ARSH_VM_HANDLER(hChar) {
  if (available(iter, end) && cur(iter) == cast<CharIns>(*pc).getCodePoint()) {
    VM_TAILCALL_AT(pc + sizeof(CharIns), nextPos(iter));
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hIChar) {
  if (available(iter, end) &&
      doSimpleCaseFolding(cur(iter)) == cast<ICharIns>(*pc).getCodePoint()) {
    VM_TAILCALL_AT(pc + sizeof(ICharIns), nextPos(iter));
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hLBChar) {
  auto &ins = cast<LBCharIns>(*pc);
  if (availableBackward(iter, begin)) {
    int codePoint = prev(iter);
    if (ins.ignoreCase) {
      codePoint = doSimpleCaseFolding(codePoint);
    }
    if (codePoint == ins.getCodePoint()) {
      VM_TAILCALL_AT(pc + sizeof(LBCharIns), prevPos(iter));
    }
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hCharSet) {
  auto &ins = cast<CharSetIns>(*pc);
  if (!available(iter, end)) {
    VM_BACKTRACK();
  }
  /**
   * contain==true, invert==true => false
   * contain==true, invert==false => true
   * contain==false, invert==true => true
   * contain==false, invert==false => false
   */
  if (matchers[ins.getMatcherIndex()].contains(cur(iter)) != ins.invert) {
    VM_TAILCALL_AT(pc + sizeof(CharSetIns), nextPos(iter));
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hICharSet) {
  auto &ins = cast<ICharSetIns>(*pc);
  if (available(iter, end) &&
      matchers[ins.getMatcherIndex()].contains(doSimpleCaseFolding(cur(iter))) != ins.invert) {
    VM_TAILCALL_AT(pc + sizeof(ICharSetIns), nextPos(iter));
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hLBCharSet) {
  auto &ins = cast<LBCharSetIns>(*pc);
  if (availableBackward(iter, begin)) {
    int codePoint = prev(iter);
    if (ins.ignoreCase) {
      codePoint = doSimpleCaseFolding(codePoint);
    }
    if (matchers[ins.getMatcherIndex()].contains(codePoint) != ins.invert) {
      VM_TAILCALL_AT(pc + sizeof(LBCharSetIns), prevPos(iter));
    }
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hString) {
  const auto needle = matchers[cast<StringIns>(*pc).getIndex()].asStrRef();
  if (StringRef(iter, static_cast<size_t>(end - iter)).startsWith(needle)) {
    VM_TAILCALL_AT(pc + sizeof(StringIns), iter + needle.size());
  }
  VM_BACKTRACK();
}

ARSH_VM_HANDLER(hLBString) {
  const auto needle = matchers[cast<LBStringIns>(*pc).getIndex()].asStrRef();
  if (StringRef(begin, static_cast<size_t>(iter - begin)).endsWith(needle)) {
    VM_TAILCALL_AT(pc + sizeof(LBStringIns), iter - needle.size());
  }
  VM_BACKTRACK();
}

//============================================================================//
//     radix tree / emoji sequence                                            //
//============================================================================//

/**
 * handle PrepareRadix and RadixOrEmoji together.
 * PrepareRadix pushes the radix state and then falls through into the RadixOrEmoji body,
 * which is expressed as a phase branch on the opcode.
 */
ARSH_VM_HANDLER(hRadixOrEmoji) {
  if (pc->op == OpCode::PrepareRadix) {
    pc += sizeof(PrepareRadixIns);
    auto &radixIns = cast<RadixOrEmojiIns>(*pc);
    unsigned int codePointCount = 0;
    if (radixIns.hasEmoji()) {
      codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
    }
    if (radixIns.hasRadix) {
      codePointCount = std::max<unsigned int>(
          codePointCount, matchers[radixIns.getIndex()].asRadixTree().getMaxCodePointCount());
    }
    const char *cur_ = iter;
    for (unsigned int count = 0; count < codePointCount && available(cur_, end); count++) {
      cur_ += UnicodeUtil::utf8ByteSize(*cur_);
    }
    VM_TRY(st->bts.push(Backtrack::newRadixState(static_cast<uint32_t>(cur_ - iter))));
  } else {
    StringRef ref(iter, st->bts.getRadixState());
    unsafeRemoveSuffixUtf8(ref);
    st->bts.updateRadixState(ref.size());
  }

  auto &radixIns = cast<RadixOrEmojiIns>(*pc);
  if (const auto strSize = st->bts.getRadixState(); strSize && available(iter, end)) {
    const StringRef ref(iter, strSize);
    const auto nextOffset = radixIns.nextOffset;
    unsigned int consumedSize = 0;
    if (radixIns.hasEmoji()) {
      auto [s, p] =
          findLongestMatched(ucp::getEmojiTrie(), ref, st->foldBuf, radixIns.ignoreCase());
      if (p && hasFlag(toUnderlying(radixIns.emoji), p)) {
        consumedSize = s;
      }
    }
    if (radixIns.hasRadix) {
      auto [s, p] = findLongestMatched(matchers[radixIns.getIndex()].asRadixTree(), ref,
                                       st->foldBuf, radixIns.ignoreCase());
      if (p) {
        consumedSize = std::max<unsigned int>(consumedSize, s);
      }
    }
    if (consumedSize) {
      st->bts.updateRadixState(consumedSize);
      VM_TRY(st->bts.push(
          Backtrack::newSetIns(iter, static_cast<uint32_t>(pc - st->bts.getStartInst()))));
      VM_TAILCALL_AT(pc + sizeof(RadixOrEmojiIns) + nextOffset, iter + consumedSize);
    }
    if (nextOffset) {
      VM_NEXT(RadixOrEmojiIns); // try next
    }
  }
  VM_BACKTRACK();
}

/**
 * handle PrepareLBRadix and LBRadixOrEmoji together (backward version).
 */
ARSH_VM_HANDLER(hLBRadixOrEmoji) {
  if (pc->op == OpCode::PrepareLBRadix) {
    pc += sizeof(PrepareLBRadixIns);
    auto &radixIns = cast<LBRadixOrEmojiIns>(*pc);
    unsigned int codePointCount = 0;
    if (radixIns.hasEmoji()) {
      codePointCount = ucp::getEmojiTrie().getMaxCodePointCount();
    }
    if (radixIns.hasRadix) {
      codePointCount = std::max<unsigned int>(
          codePointCount, matchers[radixIns.getIndex()].asRadixTree().getMaxCodePointCount());
    }
    const char *cur_ = iter;
    for (unsigned int count = 0; count < codePointCount && availableBackward(cur_, begin);
         count++) {
      unsafePrevUtf8(cur_);
    }
    VM_TRY(st->bts.push(Backtrack::newRadixState(static_cast<uint32_t>(iter - cur_))));
  } else {
    StringRef ref(iter - st->bts.getRadixState(), st->bts.getRadixState());
    unsafeRemovePrefixUtf8(ref);
    st->bts.updateRadixState(ref.size());
  }

  auto &radixIns = cast<LBRadixOrEmojiIns>(*pc);
  if (const auto strSize = st->bts.getRadixState(); strSize && availableBackward(iter, begin)) {
    const StringRef ref(iter - strSize, strSize);
    const auto nextOffset = radixIns.nextOffset;
    unsigned int consumedSize = 0;
    if (radixIns.hasEmoji()) {
      auto [s, p] =
          findBackwardLongestMatched(ucp::getEmojiTrie(), ref, st->foldBuf, radixIns.ignoreCase());
      if (p && hasFlag(toUnderlying(radixIns.emoji), p)) {
        consumedSize = s;
      }
    }
    if (radixIns.hasRadix) {
      auto [s, p] = findBackwardLongestMatched(matchers[radixIns.getIndex()].asRadixTree(), ref,
                                               st->foldBuf, radixIns.ignoreCase());
      if (p) {
        consumedSize = std::max<unsigned int>(consumedSize, s);
      }
    }
    if (consumedSize) {
      st->bts.updateRadixState(consumedSize);
      VM_TRY(st->bts.push(
          Backtrack::newSetIns(iter, static_cast<uint32_t>(pc - st->bts.getStartInst()))));
      VM_TAILCALL_AT(pc + sizeof(LBRadixOrEmojiIns) + nextOffset, iter - consumedSize);
    }
    if (nextOffset) {
      VM_NEXT(LBRadixOrEmojiIns); // try next
    }
  }
  VM_BACKTRACK();
}

//============================================================================//
//     capture                                                                //
//============================================================================//

ARSH_VM_HANDLER(hBeginCapture) {
  Capture *const captures = st->captures;
  const unsigned int index = cast<BeginCaptureIns>(*pc).getCaptureIndex();
  captures[index] = {.offset = static_cast<uint32_t>(iter - begin), .size = 0};
  VM_TRY(st->bts.push(Backtrack::newSetCapture(index, Capture())));
  VM_NEXT(BeginCaptureIns);
}

ARSH_VM_HANDLER(hEndCapture) {
  Capture *const captures = st->captures;
  const unsigned int index = cast<EndCaptureIns>(*pc).getCaptureIndex();
  auto &capture = captures[index];
  assert(capture.offset <= static_cast<uint32_t>(iter - begin));
  capture.size = static_cast<uint32_t>(iter - begin) - capture.offset;
  VM_NEXT(EndCaptureIns);
}

ARSH_VM_HANDLER(hLBEndCapture) {
  Capture *const captures = st->captures;
  const unsigned int index = cast<LBEndCaptureIns>(*pc).getCaptureIndex();
  auto &capture = captures[index];
  const unsigned int actualEndOffset = capture.endOffset();
  capture.offset = static_cast<uint32_t>(iter - begin);
  assert(capture.offset <= actualEndOffset);
  capture.size = actualEndOffset - capture.offset;
  VM_NEXT(LBEndCaptureIns);
}

ARSH_VM_HANDLER(hResetCaptures) {
  Capture *const captures = st->captures;
  auto &ins = cast<ResetCapturesIns>(*pc);
  const unsigned int last = ins.getLastIndex();
  for (unsigned int i = ins.getFirstIndex(); i <= last; i++) {
    VM_TRY(st->bts.push(Backtrack::newSetCapture(i, captures[i]))); // save original capture
    captures[i] = Capture();
  }
  VM_NEXT(ResetCapturesIns);
}

ARSH_VM_HANDLER(hBackRef) {
  auto &ins = cast<BackRefIns>(*pc);
  const Capture capture =
      ins.named ? st->ctx->resolveNamedBackRef(ins.getRefIndex()) : st->captures[ins.getRefIndex()];
  if (capture) {
    const StringRef ref(begin + capture.offset, capture.size);
    st->input.setIter(iter);
    if (!st->input.expectForward(ref)) {
      VM_BACKTRACK();
    }
    VM_TAILCALL_AT(pc + sizeof(BackRefIns), st->input.getIter());
  }
  VM_NEXT(BackRefIns);
}

ARSH_VM_HANDLER(hIBackRef) {
  auto &ins = cast<IBackRefIns>(*pc);
  const Capture capture =
      ins.named ? st->ctx->resolveNamedBackRef(ins.getRefIndex()) : st->captures[ins.getRefIndex()];
  if (capture) {
    const StringRef ref(begin + capture.offset, capture.size);
    auto target = iter;
    const char *refIter = ref.begin();
    const char *const refEnd = ref.end();
    for (; refIter != refEnd;) {
      if (available(target, end) && doSimpleCaseFolding(unsafeNextUtf8(refIter)) ==
                                        doSimpleCaseFolding(unsafeNextUtf8(target))) {
        continue;
      }
      VM_BACKTRACK();
    }
    VM_TAILCALL_AT(pc + sizeof(IBackRefIns), target);
  }
  VM_NEXT(IBackRefIns);
}

ARSH_VM_HANDLER(hLBBackRef) {
  auto &ins = cast<LBBackRefIns>(*pc);
  const Capture capture =
      ins.named ? st->ctx->resolveNamedBackRef(ins.getRefIndex()) : st->captures[ins.getRefIndex()];
  if (capture) {
    const StringRef ref(begin + capture.offset, capture.size);
    if (ins.ignoreCase) {
      auto target = iter;
      const char *const refBegin = ref.begin();
      const char *refIter = ref.end();
      for (; refIter != refBegin;) {
        if (availableBackward(target, begin) && doSimpleCaseFolding(unsafePrevUtf8(refIter)) ==
                                                    doSimpleCaseFolding(unsafePrevUtf8(target))) {
          continue;
        }
        VM_BACKTRACK();
      }
      VM_TAILCALL_AT(pc + sizeof(LBBackRefIns), target);
    }
    st->input.setIter(iter);
    if (!st->input.expectBackward(ref)) {
      VM_BACKTRACK();
    }
    VM_TAILCALL_AT(pc + sizeof(LBBackRefIns), st->input.getIter());
  }
  VM_NEXT(LBBackRefIns);
}

//============================================================================//
//     loop                                                                   //
//============================================================================//

/**
 * handle BeginLoop and EndLoop together.
 * EndLoop jumps to its target, which is always the corresponding BeginLoop, and then falls
 * through into the shared loop body. this is expressed as a phase branch on the opcode.
 */
ARSH_VM_HANDLER(hLoop) {
  if (pc->op == OpCode::EndLoop) {
    pc = st->bts.getStartInst() + cast<EndLoopIns>(*pc).getTarget();
  } else {
    st->loopStates[cast<BeginLoopIns>(*pc).getLoopIndex()] = LoopState();
  }

  auto &loopIns = cast<BeginLoopIns>(*pc);
  auto &loop = st->loopStates[loopIns.getLoopIndex()];
  if (loop.inputOffset == static_cast<uint32_t>(iter - begin) && loop.count > loopIns.getMin()) {
    VM_BACKTRACK(); // after minimum repeat, if not consume input, backtrack
  }

  st->input.setIter(iter);
  Input &input = st->input;
  if (const auto count = loop.count; count < loopIns.getMin()) {
    VM_TRY(st->bts.prepareLoopBody(input, loopIns.getLoopIndex(), loop));
    VM_NEXT(BeginLoopIns);
  } else if (count == loopIns.getMax()) {
    VM_TAILCALL(st->bts.getStartInst() + loopIns.getOuter());
  } else if (loopIns.greedy) {
    VM_TRY(st->bts.prepareGreedyLoop(input, loopIns, loop));
    VM_NEXT(BeginLoopIns);
  } else {
    VM_TRY(st->bts.prepareNonGreedyLoop(input, pc, loop));
    VM_TAILCALL(st->bts.getStartInst() + loopIns.getOuter());
  }
}

//============================================================================//
//     look-around                                                            //
//============================================================================//

ARSH_VM_HANDLER(hBeginLookAround) {
  auto &lookAround = cast<BeginLookAroundIns>(*pc);
  VM_TRY(st->bts.push(Backtrack::newLookAround(iter, lookAround.getTarget(), lookAround.negate)));
  VM_NEXT(BeginLookAroundIns);
}

ARSH_VM_HANDLER(hEndLookAround) {
  st->input.setIter(iter);
  if (st->bts.cleanupLookAround(st->input, st->captures)) {
    VM_TAILCALL_AT(pc + sizeof(EndLookAroundIns), st->input.getIter());
  }
  VM_BACKTRACK_AT(st->input.getIter());
}

//============================================================================//
//     dispatch                                                               //
//============================================================================//

/**
 * opcode to handler mapping.
 * some opcodes share a handler (see the merged handlers above).
 */
#define RE_DISPATCH_ENTRY_Nop hNop
#define RE_DISPATCH_ENTRY_Match hMatch
#define RE_DISPATCH_ENTRY_Jump hJump
#define RE_DISPATCH_ENTRY_Alt hAlt
#define RE_DISPATCH_ENTRY_Start hStart
#define RE_DISPATCH_ENTRY_End hEnd
#define RE_DISPATCH_ENTRY_Word hWord
#define RE_DISPATCH_ENTRY_IWord hIWord
#define RE_DISPATCH_ENTRY_Any hAny
#define RE_DISPATCH_ENTRY_AnyExceptNL hAnyExceptNL
#define RE_DISPATCH_ENTRY_LBAny hLBAny
#define RE_DISPATCH_ENTRY_Grapheme hGrapheme
#define RE_DISPATCH_ENTRY_Char hChar
#define RE_DISPATCH_ENTRY_IChar hIChar
#define RE_DISPATCH_ENTRY_LBChar hLBChar
#define RE_DISPATCH_ENTRY_CharSet hCharSet
#define RE_DISPATCH_ENTRY_ICharSet hICharSet
#define RE_DISPATCH_ENTRY_LBCharSet hLBCharSet
#define RE_DISPATCH_ENTRY_PrepareRadix hRadixOrEmoji
#define RE_DISPATCH_ENTRY_RadixOrEmoji hRadixOrEmoji
#define RE_DISPATCH_ENTRY_PrepareLBRadix hLBRadixOrEmoji
#define RE_DISPATCH_ENTRY_LBRadixOrEmoji hLBRadixOrEmoji
#define RE_DISPATCH_ENTRY_String hString
#define RE_DISPATCH_ENTRY_LBString hLBString
#define RE_DISPATCH_ENTRY_BeginCapture hBeginCapture
#define RE_DISPATCH_ENTRY_EndCapture hEndCapture
#define RE_DISPATCH_ENTRY_LBEndCapture hLBEndCapture
#define RE_DISPATCH_ENTRY_ResetCaptures hResetCaptures
#define RE_DISPATCH_ENTRY_BackRef hBackRef
#define RE_DISPATCH_ENTRY_IBackRef hIBackRef
#define RE_DISPATCH_ENTRY_LBBackRef hLBBackRef
#define RE_DISPATCH_ENTRY_BeginLoop hLoop
#define RE_DISPATCH_ENTRY_EndLoop hLoop
#define RE_DISPATCH_ENTRY_BeginLookAround hBeginLookAround
#define RE_DISPATCH_ENTRY_EndLookAround hEndLookAround

extern const Handler kDispatchTable[kOpCodeCount] = {
#define GEN_TABLE(E) RE_DISPATCH_ENTRY_##E,
    EACH_RE_OPCODE(GEN_TABLE)
#undef GEN_TABLE
};

MatchStatus match(MatchContext &ctx, ObserverPtr<Timer> timer) {
  // prepare
  const Inst *pc = ctx.getInst();
  LoopState *const loopStates = ctx.getLoops();
  ctx.clearCaptures();
  Capture *captures = ctx.getCaptures();

  TailCallState st(pc);
  st.input = ctx.copyInput();
  st.oldIter = st.input.getIter();
  st.captures = captures;
  st.loopStates = loopStates;
  st.matchers = ctx.getMatchers().begin();
  st.ctx = &ctx;
  st.timer = timer;
  if (timer) {
    timer->start();
  }

  const char *const begin = st.input.getBegin();
  const char *const end = st.input.getEnd();
  const char *iter = st.oldIter;
  prepareMatch(pc, iter, st, end, st.matchers);

  // the whole dispatch chain uses the `preserve_none` convention uniformly, so it is entered
  // with a plain call.
  return dispatchBacktrack(pc, iter, begin, end, st.matchers, &st);
}

} // namespace arsh::regex

#endif // ARSH_USE_TAILCALL_VM
