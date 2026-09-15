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

#ifndef ARSH_REGEX_BACKTRACK_H
#define ARSH_REGEX_BACKTRACK_H

#include <cassert>
#include <type_traits>
#include <vector>

#include "capture.h"
#include "input.h"
#include "instruction.h"
#include "match_context.h"
#include "misc/rtti.hpp"

namespace arsh::regex {

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

  static Backtrack newSetIns(const Input &input, uint32_t target) {
    return {.setIns = {.op = BacktrackOp::SetIns, .target = target, .iter = input.getIter()}};
  }

  /**
   * same as `newSetIns()`, but the target is already a bytecode offset.
   *
   * the JIT cannot hand its helpers a `const Inst *` to subtract from: the helpers it calls take
   * plain values so that no stencil has to keep a bytecode pointer alive across them.
   */
  static Backtrack newSetInsFromOffset(uint32_t target, const char *iter) {
    return {.setIns = {.op = BacktrackOp::SetIns, .target = target, .iter = iter}};
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

  static Backtrack newLookAround(const Input &input, uint32_t target, bool negate) {
    return {
        .lookAround =
            {
                .op = BacktrackOp::LookAround,
                .negate = negate,
                .matched = !negate,
                .target = target,
                .iter = input.getIter(),
            },
    };
  }

  static Backtrack newRadixState(uint32_t consumedSize) {
    return {.radixState = {.op = BacktrackOp::RadixState, .consumedSize = consumedSize}};
  }
};

class BacktrackStack {
private:
  static_assert(std::is_trivially_copy_constructible_v<Backtrack>);
  const Inst *const start;
  std::vector<Backtrack> bts;

public:
  explicit BacktrackStack(const Inst *start) : start(start) {}

  const Inst *getStartInst() const { return this->start; }

  bool push(Backtrack bt) {
    if (unlikely(this->bts.size() == Regex::MAX_STACK_DEPTH)) {
      return false;
    }
    this->bts.push_back(bt);
    return true;
  }

  unsigned int getRadixState() const { return this->bts.back().radixState.consumedSize; }

  void updateRadixState(unsigned int newSize) {
    this->bts.back().radixState.consumedSize = newSize;
  }

  bool backtrack(const Inst *&inst, Input &input, Capture *captures, LoopState *loopStates) {
    while (!this->bts.empty()) {
      auto bt = this->bts.back();
      this->bts.pop_back();
      switch (bt.op) {
      case BacktrackOp::None:
        return true; // do nothing
      case BacktrackOp::SetIns:
        inst = this->getStartInst() + bt.setIns.target;
        input.setIter(bt.setIns.iter);
        return true;
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
        inst = this->getStartInst() + extra.setIns.target;
        input.setIter(extra.setIns.iter);
        inst += sizeof(BeginLoopIns);                                   // goto loop body
        this->prepareLoopBody(input, loopIndex, loopStates[loopIndex]); // never fail (already pop)
        return true;
      }
      case BacktrackOp::LookAround:
        inst = this->getStartInst() + bt.lookAround.target;
        bt.lookAround.matched = bt.lookAround.negate; // if negative lookaround, matched
        this->push(bt);                               // never fail (already pop)
        return true;
      case BacktrackOp::RadixState:
        break;
      }
    }
    return false;
  }

  bool prepareLoopBody(const Input &input, uint16_t loopIndex, LoopState &loop) {
    if (!this->push(Backtrack::newSetLoopState(loopIndex, loop))) {
      return false;
    }
    loop.count++;
    loop.inputOffset = input.getOffset();
    return true;
  }

  bool prepareGreedyLoop(const Input &input, const BeginLoopIns &loopIns, LoopState &loop) {
    return this->push(Backtrack::newSetIns(input, loopIns.getOuter())) &&
           this->prepareLoopBody(input, loopIns.getLoopIndex(), loop);
  }

  bool prepareNonGreedyLoop(const Input &input, const Inst *beginInst, const LoopState &loop) {
    return this->push(Backtrack::newSetIns(input, beginInst - this->getStartInst())) &&
           this->push(
               Backtrack::newNonGreedyLoop(cast<BeginLoopIns>(*beginInst).getLoopIndex(), loop));
  }

  /**
   * value-based variants of the two `prepare*Loop()` above, for the copy-and-patch JIT.
   *
   * the JIT no longer hands a `BeginLoopIns` to its helpers. it reads the operands into registers
   * and the two possible continuations (the loop body and the instruction after the loop) into code
   * addresses, so the `outer` address cannot be recovered from the instruction; it is passed as a
   * bytecode offset relative to the instruction sequence instead.
   */
  bool prepareGreedyLoop(const Input &input, uint32_t outerOffset, uint16_t loopIndex,
                         LoopState &loop) {
    return this->push(Backtrack::newSetInsFromOffset(outerOffset, input.getIter())) &&
           this->prepareLoopBody(input, loopIndex, loop);
  }

  bool prepareNonGreedyLoop(const Input &input, uint32_t beginOffset, uint16_t loopIndex,
                            const LoopState &loop) {
    return this->push(Backtrack::newSetInsFromOffset(beginOffset, input.getIter())) &&
           this->push(Backtrack::newNonGreedyLoop(loopIndex, loop));
  }

  bool cleanupLookAround(Input &input, Capture *captures) {
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
    input.setIter(bt.lookAround.iter);
    this->bts.pop_back();
    return bt.lookAround.matched;
  }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_BACKTRACK_H
