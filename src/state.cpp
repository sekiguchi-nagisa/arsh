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

#include "state.h"

namespace ydsh {

bool VMState::wind(unsigned int stackTopOffset, unsigned int paramSize, const DSCode &code) {
  const unsigned int maxVarSize = code.getLocalVarNum();
  const unsigned int localVarOffset = this->frame.stackTopIndex - paramSize + 1;
  const unsigned int operandSize = code.getStackDepth();

  if (unlikely(this->frames.size() == MAX_FRAME_SIZE)) {
    return false;
  }

  // save current control frame
  this->frame.stackTopIndex -= stackTopOffset;
  this->frames.push_back(this->getFrame());
  this->frame.stackTopIndex += stackTopOffset;

  // reallocate stack
  this->reserve(maxVarSize - paramSize + operandSize);

  // prepare control frame
  this->frame.code = &code;
  this->frame.stackTopIndex += maxVarSize - paramSize;
  this->frame.stackBottomIndex = this->frame.stackTopIndex;
  this->frame.localVarOffset = localVarOffset;
  this->frame.pc = 0;
  return true;
}

void VMState::unwind() {
  auto savedFrame = this->frames.back();

  this->frame.code = savedFrame.code;
  this->frame.stackBottomIndex = savedFrame.stackBottomIndex;
  this->frame.localVarOffset = savedFrame.localVarOffset;
  this->frame.pc = savedFrame.pc;

  unsigned int oldStackTopIndex = savedFrame.stackTopIndex;
  while (this->frame.stackTopIndex > oldStackTopIndex) {
    this->popNoReturn();
  }
  this->frames.pop_back();
}

void VMState::resize(unsigned int afterSize) {
  unsigned int newSize = this->operandsSize;
  do {
    newSize += (newSize >> 1u);
  } while (newSize < afterSize);

  auto newStack = new DSValue[newSize];
  for (unsigned int i = 0; i < this->frame.stackTopIndex + 1; i++) {
    newStack[i] = std::move(this->operands[i]);
  }
  delete[] this->operands;
  this->operands = newStack;
  this->operandsSize = newSize;
}

} // namespace ydsh