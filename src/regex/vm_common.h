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

#ifndef ARSH_REGEX_VM_COMMON_H
#define ARSH_REGEX_VM_COMMON_H

#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "input.h"
#include "instruction.h"
#include "match_context.h"
#include "matcher.h"
#include "misc/string_ref.hpp"
#include "misc/unicode.hpp"
#include "regex.h"
#include "unicode/case_fold.h"

namespace arsh::regex {

/**
 * enable the tail-call interpreter backend.
 *
 * it requires the `musttail` and `preserve_none` attributes, which are only available in
 * limited compilers/architectures. if they are not available, the switch/threaded-code
 * interpreter (see vm.cpp) is used instead.
 *
 * the attributes must be written in the prefix position (before the return type).
 * otherwise some compilers silently ignore them.
 *
 * NOTE: this also requires NDEBUG. `musttail` rejects a function whose local address may
 * escape, and `assert` does exactly that (it stringifies/uses them), so an assertion enabled
 * build cannot use this backend at all. debug builds keep the switch/threaded-code
 * interpreter, which is also the more debuggable one.
 */
#if defined(ARSH_HAVE_MUSTTAIL_VM) && defined(__x86_64__) && defined(__GNUC__) &&                  \
    defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__) &&          \
    !defined(__SANITIZE_MEMORY__) && !defined(ARSH_TAILCALL_VM_DISABLED)
#define ARSH_USE_TAILCALL_VM 1
#endif

/**
 * calling convention of the hot helpers of the backtracking stack (see vm_tailcall.cpp).
 *
 * a helper called from a handler must use the same convention, otherwise the call is not
 * homogeneous: under `preserve_none` no register is callee-saved, so a differently-conventioned
 * callee makes the compiler spill the live VM state around the call on every single dispatch,
 * which is much slower than the switch/threaded-code interpreter.
 */
#ifdef ARSH_USE_TAILCALL_VM
#define ARSH_VM_CALL_CONV __attribute__((preserve_none))
#else
#define ARSH_VM_CALL_CONV
#endif

/** force-inlined, so that the handlers stay leaf-like */
#define ARSH_VM_HOT ARSH_VM_CALL_CONV [[gnu::always_inline]] inline

enum class BacktrackOp : unsigned char {
  None,
  SetIns,
  SetCapture,
  SetLoopState,
  NonGreedyLoop,
  LookAround,
  RadixState,
};

union Backtrack {
  BacktrackOp op;

  struct {
    BacktrackOp op; // NOLINT
    uint32_t target;
    const char *iter;
  } setIns;

  struct {
    BacktrackOp op; // NOLINT
    uint32_t index;
    Capture capture;
  } setCapture;

  struct {
    BacktrackOp op; // NOLINT
    uint16_t loopIndex;
    LoopState state;
  } setLoopState;

  struct {
    BacktrackOp op; // NOLINT
    uint16_t loopIndex;
    LoopState state;
  } nonGreedyLoop;

  struct {
    BacktrackOp op; // NOLINT
    bool negate;
    bool matched;
    uint32_t target;
    const char *iter;
  } lookAround;

  struct {
    BacktrackOp op; // NOLINT
    uint32_t consumedSize;
  } radixState;

  static Backtrack dummy() { return {.op = BacktrackOp::None}; }

  static Backtrack newSetIns(const char *iter, uint32_t target) {
    return {.setIns = {.op = BacktrackOp::SetIns, .target = target, .iter = iter}};
  }

  static Backtrack newSetIns(const Input &input, uint32_t target) {
    return newSetIns(input.getIter(), target);
  }

  static Backtrack newSetCapture(uint32_t index, Capture capture) {
    return {.setCapture = {.op = BacktrackOp::SetCapture, .index = index, .capture = capture}};
  }

  static Backtrack newSetLoopState(uint16_t loopIndex, LoopState state) {
    return {
        .setLoopState = {.op = BacktrackOp::SetLoopState, .loopIndex = loopIndex, .state = state},
    };
  }

  static Backtrack newNonGreedyLoop(uint16_t loopIndex, LoopState state) {
    return {
        .nonGreedyLoop = {.op = BacktrackOp::NonGreedyLoop, .loopIndex = loopIndex, .state = state},
    };
  }

  static Backtrack newLookAround(const char *iter, uint32_t target, bool negate) {
    return {
        .lookAround =
            {
                .op = BacktrackOp::LookAround,
                .negate = negate,
                .matched = !negate,
                .target = target,
                .iter = iter,
            },
    };
  }

  static Backtrack newLookAround(const Input &input, uint32_t target, bool negate) {
    return newLookAround(input.getIter(), target, negate);
  }

  static Backtrack newRadixState(uint32_t consumedSize) {
    return {.radixState = {.op = BacktrackOp::RadixState, .consumedSize = consumedSize}};
  }
};

/**
 * the outcome of `BacktrackStack::backtrack`.
 *
 * it is returned by value (instead of via reference out-parameters) so that the tail-call
 * interpreter can keep the instruction pointer and the input position in registers. with
 * out-parameters the compiler has to keep them in memory, which spills them to the stack on
 * every dispatch.
 *
 * NOTE: it must be exactly 16 bytes (two pointers) so that it is returned in the registers.
 * a struct larger than 16 bytes is returned through a hidden pointer, which forces the callers
 * (i.e. every handler) to allocate a frame and to keep the input position in memory, which
 * defeats the purpose of the tail-call design. the "not found" state is therefore encoded by
 * `iter` being null (it is never null otherwise) instead of a separate flag.
 */
struct BacktrackResult {
  const Inst *inst;
  const char *iter; // null if the backtrack stack is exhausted

  bool found() const { return this->iter != nullptr; }
};

/**
 * the backtracking stack.
 *
 * the primary API works on the raw input position (`const char *`) so that the tail-call
 * interpreter (see vm_tailcall.cpp) can keep the position in a register. the `Input`-based
 * overloads are thin wrappers for the switch/threaded-code interpreter.
 */
class BacktrackStack {
private:
  static_assert(std::is_trivially_copy_constructible_v<Backtrack>);
  const Inst *const start;
  std::vector<Backtrack> bts;

public:
  explicit BacktrackStack(const Inst *start) : start(start) {}

  const Inst *getStartInst() const { return this->start; }

  ARSH_VM_HOT bool push(Backtrack bt) {
    if (unlikely(this->bts.size() == Regex::MAX_STACK_DEPTH)) {
      return false;
    }
    this->bts.push_back(bt);
    return true;
  }

  ARSH_VM_HOT unsigned int getRadixState() const {
    return this->bts.back().radixState.consumedSize;
  }

  ARSH_VM_HOT void updateRadixState(unsigned int newSize) {
    this->bts.back().radixState.consumedSize = newSize;
  }

  /**
   * pop an entry and prepare the next dispatch point.
   *
   * NOTE: a `NonGreedyLoop` entry does *not* return to the caller. it lands on the `BeginLoop`
   * body that the corresponding `EndLoop` (whose target is the caller) jumps from, so `inst`
   * must be handed back and the caller must dispatch it.
   *
   * NOTE: this is force-inlined. the callers reached from the tail-call handlers run under the
   * `preserve_none` convention, under which no register is callee-saved, so an out-of-line call
   * would force them to spill their live state around it on every single backtrack.
   */
  ARSH_VM_HOT BacktrackResult backtrack([[maybe_unused]] const Inst *inst, const char *begin,
                                        const char *iter, Capture *captures,
                                        LoopState *loopStates) {
    while (!this->bts.empty()) {
      auto bt = this->bts.back();
      this->bts.pop_back();
      switch (bt.op) {
      case BacktrackOp::None:
        return {inst, iter}; // do nothing
      case BacktrackOp::SetIns:
        return {this->getStartInst() + bt.setIns.target, bt.setIns.iter};
      case BacktrackOp::SetCapture:
        captures[bt.setCapture.index] = bt.setCapture.capture;
        break;
      case BacktrackOp::SetLoopState:
        loopStates[bt.setLoopState.loopIndex] = bt.setLoopState.state;
        break;
      case BacktrackOp::NonGreedyLoop: {
        const auto loopIndex = bt.nonGreedyLoop.loopIndex;
        loopStates[loopIndex] = bt.nonGreedyLoop.state;
        assert(!this->bts.empty());
        const auto extra = this->bts.back();
        this->bts.pop_back();
        assert(extra.op == BacktrackOp::SetIns);
        const Inst *target = this->getStartInst() + extra.setIns.target;
        iter = extra.setIns.iter;
        target += sizeof(BeginLoopIns);                                       // goto loop body
        this->prepareLoopBody(begin, iter, loopIndex, loopStates[loopIndex]); // never fail
        return {target, iter};
      }
      case BacktrackOp::LookAround: {
        const Inst *target = this->getStartInst() + bt.lookAround.target;
        bt.lookAround.matched = bt.lookAround.negate; // if negative lookaround, matched
        this->push(bt);                               // never fail (already pop)
        return {target, iter};
      }
      case BacktrackOp::RadixState:
        break;
      }
    }
    return {nullptr, nullptr};
  }

  ARSH_VM_HOT bool prepareLoopBody(const char *begin, const char *iter, uint16_t loopIndex,
                                   LoopState &loop) {
    if (!this->push(Backtrack::newSetLoopState(loopIndex, loop))) {
      return false;
    }
    loop.count++;
    loop.inputOffset = static_cast<uint32_t>(iter - begin);
    return true;
  }

  [[gnu::always_inline]] bool prepareGreedyLoop(const char *begin, const char *iter,
                                                const BeginLoopIns &loopIns, LoopState &loop) {
    return this->push(Backtrack::newSetIns(iter, loopIns.getOuter())) &&
           this->prepareLoopBody(begin, iter, loopIns.getLoopIndex(), loop);
  }

  [[gnu::always_inline]] bool prepareNonGreedyLoop(const char *iter, const Inst *beginInst,
                                                   const LoopState &loop) {
    return this->push(Backtrack::newSetIns(
               iter, static_cast<uint32_t>(beginInst - this->getStartInst()))) &&
           this->push(
               Backtrack::newNonGreedyLoop(cast<BeginLoopIns>(*beginInst).getLoopIndex(), loop));
  }

  [[gnu::always_inline]] bool cleanupLookAround(const char *&iter, Capture *captures) {
    // find original lookaround state
    bool negate = false;
    for (ssize_t i = static_cast<ssize_t>(this->bts.size()) - 1; i > -1; i--) {
      if (auto &bt = this->bts[i]; bt.op == BacktrackOp::LookAround) {
        negate = bt.lookAround.negate;
        break;
      }
    }

    while (!this->bts.empty() && this->bts.back().op != BacktrackOp::LookAround) {
      if (negate && this->bts.back().op == BacktrackOp::SetCapture) {
        auto bt = this->bts.back();
        captures[bt.setCapture.index] = bt.setCapture.capture; // force reset capture
      }
      this->bts.pop_back();
    }
    assert(!this->bts.empty());
    auto bt = this->bts.back();
    iter = bt.lookAround.iter;
    this->bts.pop_back();
    return bt.lookAround.matched;
  }

  //=== Input based wrappers (used by the switch/threaded-code interpreter) ===//

  bool backtrack(const Inst *&inst, Input &input, Capture *captures, LoopState *loopStates) {
    const auto ret = this->backtrack(inst, input.getBegin(), input.getIter(), captures, loopStates);
    if (!ret.found()) {
      return false;
    }
    inst = ret.inst;
    input.setIter(ret.iter);
    return true;
  }

  bool prepareLoopBody(const Input &input, uint16_t loopIndex, LoopState &loop) {
    return this->prepareLoopBody(input.getBegin(), input.getIter(), loopIndex, loop);
  }

  bool prepareGreedyLoop(const Input &input, const BeginLoopIns &loopIns, LoopState &loop) {
    return this->prepareGreedyLoop(input.getBegin(), input.getIter(), loopIns, loop);
  }

  bool prepareNonGreedyLoop(const Input &input, const Inst *beginInst, const LoopState &loop) {
    return this->prepareNonGreedyLoop(input.getIter(), beginInst, loop);
  }

  bool cleanupLookAround(Input &input, Capture *captures) {
    const char *iter = input.getIter();
    const bool ret = this->cleanupLookAround(iter, captures);
    input.setIter(iter);
    return ret;
  }
};

inline std::pair<unsigned short, unsigned char>
findLongestMatched(const PackedRadixTree tree, StringRef ref, std::string &foldBuf, bool caseFold) {
  const auto old = ref;
  if (caseFold) {
    foldBuf.clear();
    const char *iter = ref.begin();
    const char *end = ref.end();
    while (iter != end) {
      int codePoint = doSimpleCaseFolding(unsafeNextUtf8(iter));
      char data[4];
      const auto len = UnicodeUtil::codePointToUtf8(codePoint, data);
      assert(len);
      foldBuf.append(data, len);
    }
    ref = foldBuf;
  }
  auto [s, p] = tree.findLongestMatched(ref);
  if (caseFold && p) { // remap to original byte size
    ref = ref.substr(0, s);
    const char *begin = ref.begin();
    const char *const end = ref.end();
    const char *oldBegin = old.begin();
    const char *const oldEnd = old.end();
    while (begin != end && oldBegin != oldEnd) {
      unsafeNextUtf8Noreturn(begin);
      unsafeNextUtf8Noreturn(oldBegin);
    }
    s = oldBegin - old.begin();
  }
  return {s, p};
}

inline std::pair<unsigned short, unsigned char>
findBackwardLongestMatched(const PackedRadixTree tree, StringRef ref, std::string &foldBuf,
                           const bool caseFold) {
  auto old = ref;
  if (caseFold) {
    foldBuf.clear();
    const char *iter = ref.begin();
    const char *end = ref.end();
    while (iter != end) {
      int codePoint = doSimpleCaseFolding(unsafeNextUtf8(iter));
      char data[4];
      const auto len = UnicodeUtil::codePointToUtf8(codePoint, data);
      assert(len);
      foldBuf.append(data, len);
    }
    ref = foldBuf;
  }
  while (!ref.empty()) {
    auto [s, p] = tree.findLongestMatched(ref);
    if (caseFold && p) { // remap to original byte size
      StringRef sub = ref.substr(0, s);
      const char *begin = sub.begin();
      const char *const end = sub.end();
      const char *oldBegin = old.begin();
      const char *const oldEnd = old.end();
      while (begin != end && oldBegin != oldEnd) {
        unsafeNextUtf8Noreturn(begin);
        unsafeNextUtf8Noreturn(oldBegin);
      }
      s = oldBegin - old.begin();
    }
    if (s == old.size()) {
      return {s, p};
    }
    unsafeRemovePrefixUtf8(ref);
    if (caseFold) {
      unsafeRemovePrefixUtf8(old);
    } else {
      old = ref;
    }
  }
  return {0, 0};
}

} // namespace arsh::regex

#endif // ARSH_REGEX_VM_COMMON_H
