/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#ifndef YDSH_STATE_H
#define YDSH_STATE_H

#include "object.h"

namespace ydsh {

struct ControlFrame {
  /**
   * currently executed code
   */
  const DSCode *code;

  /**
   * initial value is 0. increment index before push
   */
  unsigned int stackTopIndex;

  /**
   * indicate lower limit of stack top index (bottom <= top)
   */
  unsigned int stackBottomIndex;

  /**
   * offset of current local variable index.
   * initial value is equivalent to globalVarSize.
   */
  unsigned int localVarOffset;

  /**
   * indicate the index of currently evaluating op code.
   */
  unsigned int pc;

  /**
   * interpreter recursive depth
   */
  unsigned int recDepth;
};

class RecursionGuard;

class VMState {
private:
  friend class RecursionGuard;

  ControlFrame frame{};
  FlexBuffer<ControlFrame> frames;

  static constexpr unsigned int MAX_FRAME_SIZE = 2048;

  unsigned int operandsSize;

  /**
   * contains operand, global variable(may be function) or local variable.
   *
   * stack grow ==>
   * +--------+   +--------+-------+   +-------+
   * | gvar 1 | ~ | gvar N | var 1 | ~ | var N | ~
   * +--------+   +--------+-------+   +-------+
   * |   global variable   |   local variable  | operand stack
   *
   *
   * stack layout within callable
   *
   * stack grow ==>
   *   +-----------------+   +-----------------+-----------+   +-------+--------------+
   * ~ | var 1 (param 1) | ~ | var M (param M) | var M + 1 | ~ | var N | ~
   *   +-----------------+   +-----------------+-----------+   +-------+--------------+
   *   |                           local variable                      | operand stack
   */
  DSValue *operands;

  /**
   * for exception handling
   */
  DSValue thrown;

  /**
   * for finally
   */
  DSValue savedThrown;

public:
  VMState() : operandsSize(64), operands(new DSValue[this->operandsSize]) {}

  ~VMState() { delete[] this->operands; }

  // for stack manipulation op
  const DSValue &peek() const { return this->operands[this->frame.stackTopIndex]; }

  const DSValue &peekByOffset(unsigned int offset) const {
    return this->operands[this->frame.stackTopIndex - offset];
  }

  void push(const DSValue &value) { this->push(DSValue(value)); }

  void push(DSValue &&value) { this->operands[++this->frame.stackTopIndex] = std::move(value); }

  DSValue pop() { return std::move(this->operands[this->frame.stackTopIndex--]); }

  void popNoReturn() { this->operands[this->frame.stackTopIndex--].reset(); }

  void dup() {
    auto v = this->peek();
    this->push(std::move(v));
  }

  void dup2() {
    auto v1 = this->peekByOffset(1);
    auto v2 = this->peek();
    this->push(std::move(v1));
    this->push(std::move(v2));
  }

  void swap() {
    this->operands[this->frame.stackTopIndex].swap(this->operands[this->frame.stackTopIndex - 1]);
  }

  void clearOperandsUntilGuard(StackGuardType t) {
    while (this->frame.stackTopIndex > this->frame.stackBottomIndex) {
      auto &top = this->operands[this->frame.stackTopIndex];
      bool stop = top.kind() == DSValueKind::STACK_GUARD && top.asStackGuard() == t;
      this->popNoReturn();
      if (stop) {
        break;
      }
    }
  }

  void reclaimLocals(unsigned char offset, unsigned char size) {
    auto *limit = this->operands + this->frame.localVarOffset + offset;
    auto *cur = limit + size - 1;
    while (cur >= limit) {
      (cur--)->reset();
    }
  }

  // for exception handling
  const DSValue &getThrownObject() const { return this->thrown; }

  void setThrownObject(DSValue &&obj) {
    if (!this->restoreThrownObject()) {
      this->thrown = std::move(obj);
    }
  }

  DSValue takeThrownObject() {
    DSValue tmp;
    std::swap(tmp, this->thrown);
    return tmp;
  }

  bool hasError() const { return static_cast<bool>(this->thrown); }

  void loadThrownObject() { this->push(this->takeThrownObject()); }

  void saveThrownObject() { this->savedThrown = this->takeThrownObject(); }

  bool restoreThrownObject() {
    assert(!this->hasError());
    bool r = static_cast<bool>(this->savedThrown);
    std::swap(this->savedThrown, this->thrown);
    return r;
  }

  void clearThrownObject() {
    this->thrown.reset();
    this->savedThrown.reset();
  }

  // for local variable access
  void setLocal(unsigned char index, const DSValue &obj) { this->setLocal(index, DSValue(obj)); }

  void setLocal(unsigned char index, DSValue &&obj) {
    this->operands[this->frame.localVarOffset + index] = std::move(obj);
  }

  const DSValue &getLocal(unsigned char index) const {
    return this->operands[this->frame.localVarOffset + index];
  }

  DSValue moveLocal(unsigned char index) {
    return std::move(this->operands[this->frame.localVarOffset + index]);
  }

  void storeLocal(unsigned char index) { this->setLocal(index, this->pop()); }

  void loadLocal(unsigned char index) {
    auto v = this->getLocal(index);
    this->push(std::move(v));
  }

  ClosureObject &getCurrentClosure() const {
    return typeAs<ClosureObject>(this->operands[this->frame.localVarOffset - 1]);
  }

  // for field access
  void storeField(unsigned int index) {
    auto value = this->pop();
    typeAs<BaseObject>(this->pop())[index] = std::move(value);
  }

  void loadField(unsigned int index) {
    auto value = typeAs<BaseObject>(this->pop())[index];
    this->push(std::move(value));
  }

  // for recursive depth count
  unsigned int recDepth() const { return this->frame.recDepth; }

  bool checkVMReturn() const {
    return this->frames.empty() || this->recDepth() != this->frames.back().recDepth;
  }

  const ControlFrame &getFrame() const { return this->frame; }

  const FlexBuffer<ControlFrame> &getFrames() const { return this->frames; }

  template <typename Walker>
  static constexpr bool walker_requirement_v =
      std::is_same_v<bool, std::invoke_result_t<Walker, const ControlFrame &>>;

  template <typename Walker, enable_when<walker_requirement_v<Walker>> = nullptr>
  void walkFrames(Walker walker) const {
    auto *cur = &this->frame;
    for (unsigned int callDepth = this->frames.size(); callDepth > 0;
         cur = &this->frames[--callDepth]) {
      if (!walker(*cur)) {
        return;
      }
    }
  }

  const DSCode *code() const { return this->frame.code; }

  unsigned int &pc() noexcept { return this->frame.pc; }

  void reserve(unsigned int add) {
    unsigned int afterSize = this->frame.stackTopIndex + add;
    if (afterSize < this->operandsSize) {
      return;
    }
    this->resize(afterSize);
  }

  void reset() {
    this->frames.clear();
    this->frame = {};
    this->thrown.reset();
  }

  /**
   *
   * @param stackTopOffset
   * @param paramSize
   * @param code
   * @return
   * if current frames size is limit, return false.
   */
  bool wind(unsigned int stackTopOffset, unsigned int paramSize, const DSCode &code);

  void unwind();

  std::tuple<unsigned int, unsigned int, unsigned int> nativeWind(unsigned int paramSize) {
    auto old = std::make_tuple(this->frame.stackTopIndex - paramSize, this->frame.stackBottomIndex,
                               this->frame.localVarOffset);
    this->frame.stackBottomIndex = this->frame.stackTopIndex;
    this->frame.localVarOffset = this->frame.stackTopIndex - paramSize + 1;
    return old;
  }

  void nativeUnwind(const std::tuple<unsigned int, unsigned int, unsigned int> &tuple) {
    unsigned int oldStackTopIndex = std::get<0>(tuple);
    while (this->frame.stackTopIndex > oldStackTopIndex) {
      this->popNoReturn();
    }
    this->frame.stackBottomIndex = std::get<1>(tuple);
    this->frame.localVarOffset = std::get<2>(tuple);
  }

  template <typename Tracer>
  static constexpr bool tracer_requirement_v =
      std::is_same_v<bool, std::invoke_result_t<Tracer, StackTraceElement &&>>;

  template <typename Tracer, enable_when<tracer_requirement_v<Tracer>> = nullptr>
  void fillStackTrace(Tracer tracer) const {
    this->walkFrames([&](const ControlFrame &cur) {
      auto &callable = cur.code;
      if (!callable->is(CodeKind::NATIVE)) {
        return tracer(cast<CompiledCode>(callable)->toTraceElement(cur.pc));
      }
      return true;
    });
  }

private:
  void incRecDepth() { this->frame.recDepth++; }

  void decRecDepth() { this->frame.recDepth--; }

  void resize(unsigned int afterSize);
};

} // namespace ydsh

#endif // YDSH_STATE_H
