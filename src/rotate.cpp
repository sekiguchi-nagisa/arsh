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
#include "type_pool.h"

namespace arsh {

// #########################
// ##     HistRotator     ##
// #########################

HistRotator::HistRotator(ObjPtr<ArrayObject> history) : history(std::move(history)) {
  if (this->history) {
    this->truncateUntilLimit(true);
    this->history->append(Value::createStr()); // not check iterator invalidation
    this->history->lock(ArrayObject::LockType::HISTORY);
  }
}

void HistRotator::revertAll() {
  if (this->history) {
    if (this->history->size() > 0) {
      this->history->refValues().pop_back(); // not check iterator invalidation
    }
    this->truncateUntilLimit();
    for (auto &e : this->oldEntries) { // revert modified entry
      if (e.first < this->history->size()) {
        this->history->refValues()[e.first] =
            std::move(e.second); // not check iterator invalidation
      }
    }
    this->history->unlock();
    this->history = nullptr;
  }
}

bool HistRotator::rotate(StringRef &curBuf, HistRotator::Op op) {
  this->truncateUntilLimit();

  const auto histSize = static_cast<ssize_t>(this->history->size());
  const int newHistIndex = this->histIndex + (op == Op::PREV ? 1 : -1);
  if (newHistIndex < 0) {
    this->histIndex = 0;
    return false;
  } else if (newHistIndex >= histSize) {
    this->histIndex = static_cast<int>(histSize) - 1;
    return false;
  } else {
    ssize_t bufIndex = histSize - 1 - this->histIndex;
    if (!this->save(bufIndex, curBuf)) { // save current buffer content to current history entry
      this->histIndex = 0;
      return false;
    }
    this->histIndex = newHistIndex;
    bufIndex = histSize - 1 - this->histIndex;
    curBuf = this->history->getValues()[bufIndex].asStrRef();
    return true;
  }
}

void HistRotator::truncateUntilLimit(bool beforeAppend) {
  const unsigned int offset = beforeAppend ? 1 : 0;
  if (this->history->size() + offset > this->getMaxSize()) {
    unsigned int delSize = this->history->size() + offset - this->getMaxSize();
    auto &values = this->history->refValues(); // not check iterator invalidation
    values.erase(values.begin(), values.begin() + delSize);
    assert(values.size() == this->getMaxSize() - offset);
  }
}

bool HistRotator::save(ssize_t index, StringRef curBuf) {
  if (index < static_cast<ssize_t>(this->history->size()) && index > -1) {
    auto actualIndex = static_cast<unsigned int>(index);
    auto org = this->history->getValues()[actualIndex];
    this->oldEntries.emplace(actualIndex, std::move(org));
    this->history->refValues()[actualIndex] =
        Value::createStr(curBuf); // not check iterator invalidation
    return true;
  }
  return false;
}

// ######################
// ##     KillRing     ##
// ######################

void KillRing::expand(unsigned int afterCap) {
  if (this->buf.allocSize() == RingBuffer<std::string>::alignAllocSize(afterCap)) {
    return; // do nothing
  }

  RingBuffer<std::string> newBuf(afterCap);
  while (!this->buf.empty()) {
    newBuf.push_back(std::move(this->buf.front()));
    this->buf.pop_front();
  }
  this->buf = std::move(newBuf);
  if (*this) {
    this->reset();
  }
}

ObjPtr<ArrayObject> KillRing::toObj(const TypePool &pool) const {
  auto obj = toObjPtr<ArrayObject>(Value::create<ArrayObject>(pool.get(TYPE::StringArray)));
  unsigned int size = this->buf.size();
  for (unsigned int i = 0; i < size; i++) {
    obj->append(Value::createStr(this->buf[i])); // not check iterator invalidation
  }
  return obj;
}

} // namespace arsh