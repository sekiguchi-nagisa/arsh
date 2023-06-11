/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "rotate.h"

namespace ydsh {

// #########################
// ##     HistRotator     ##
// #########################

HistRotator::HistRotator(ObjPtr<ArrayObject> history) : history(std::move(history)) {
  if (this->history) {
    this->truncateUntilLimit(true);
    this->history->append(DSValue::createStr());
  }
}

void HistRotator::revertAll() {
  if (this->history) {
    if (this->history->size() > 0) {
      this->history->refValues().pop_back();
    }
    this->truncateUntilLimit();
    for (auto &e : this->oldEntries) { // revert modified entry
      if (e.first < this->history->size()) {
        this->history->refValues()[e.first] = std::move(e.second);
      }
    }
    this->history = nullptr;
  }
}

bool HistRotator::rotate(StringRef &curBuf, HistRotator::Op op) {
  this->truncateUntilLimit();

  // save current buffer content to current history entry
  auto histSize = static_cast<ssize_t>(this->history->size());
  ssize_t bufIndex = histSize - 1 - this->histIndex;
  if (!this->save(bufIndex, curBuf)) {
    this->histIndex = 0; // reset index
    return false;
  }

  this->histIndex += op == Op::PREV ? 1 : -1;
  if (this->histIndex < 0) {
    this->histIndex = 0;
    return false;
  } else if (this->histIndex >= histSize) {
    this->histIndex = static_cast<int>(histSize) - 1;
    return false;
  } else {
    bufIndex = histSize - 1 - this->histIndex;
    curBuf = this->history->getValues()[bufIndex].asStrRef();
    return true;
  }
}

void HistRotator::truncateUntilLimit(bool beforeAppend) {
  const unsigned int offset = beforeAppend ? 1 : 0;
  if (this->history->size() + offset > this->getMaxSize()) {
    unsigned int delSize = this->history->size() + offset - this->getMaxSize();
    auto &values = this->history->refValues();
    values.erase(values.begin(), values.begin() + delSize);
    assert(values.size() == this->getMaxSize() - offset);
  }
}

bool HistRotator::save(ssize_t index, StringRef curBuf) {
  if (index < static_cast<ssize_t>(this->history->size()) && index > -1) {
    auto actualIndex = static_cast<unsigned int>(index);
    auto org = this->history->getValues()[actualIndex];
    this->oldEntries.emplace(actualIndex, std::move(org));
    this->history->refValues()[actualIndex] = DSValue::createStr(curBuf);
    return true;
  }
  return false;
}

// ######################
// ##     KillRing     ##
// ######################

void KillRing::add(StringRef ref) {
  if (ref.empty()) {
    return;
  }
  if (!this->obj) {
    auto value = DSValue::create<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray),
                                              std::vector<DSValue>());
    this->obj = toObjPtr<ArrayObject>(value);
  }
  if (this->obj->size() + 1 > this->getMaxSize()) {
    unsigned int delSize = this->obj->size() + 1 - this->getMaxSize();
    auto &values = this->obj->refValues();
    values.erase(values.begin(), values.begin() + delSize);
    assert(values.size() == this->getMaxSize() - 1);
  }
  this->obj->append(DSValue::createStr(ref));
}

StringRef KillRing::rotate() {
  (void)this->rotateIndex;
  return "";
}

} // namespace ydsh