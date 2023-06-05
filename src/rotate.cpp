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

// ########################
// ##     HistRotate     ##
// ########################

HistRotate::HistRotate(ObjPtr<ArrayObject> history) : history(std::move(history)) {
  if (this->history) {
    this->truncateUntilLimit();
    this->history->append(DSValue::createStr());
  }
}

void HistRotate::revertAll() {
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

bool HistRotate::rotate(StringRef &curBuf, HistRotateOp op) {
  this->truncateUntilLimit();

  // save current buffer content to current history entry
  auto histSize = static_cast<ssize_t>(this->history->size());
  ssize_t bufIndex = histSize - 1 - this->histIndex;
  if (!this->save(bufIndex, curBuf)) {
    this->histIndex = 0; // reset index
    return false;
  }

  this->histIndex += op == HistRotateOp::PREV ? 1 : -1;
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

void HistRotate::truncateUntilLimit() {
  static_assert(SYS_LIMIT_HIST_SIZE < SYS_LIMIT_ARRAY_MAX);
  if (this->history->size() >= SYS_LIMIT_HIST_SIZE) {
    auto &values = this->history->refValues();
    values.erase(values.begin(),
                 values.begin() + static_cast<ssize_t>(values.size() - SYS_LIMIT_HIST_SIZE - 1));
    assert(values.size() == SYS_LIMIT_HIST_SIZE - 1);
  }
}

bool HistRotate::save(ssize_t index, StringRef curBuf) {
  if (index < static_cast<ssize_t>(this->history->size()) && index > -1) {
    auto actualIndex = static_cast<unsigned int>(index);
    auto org = this->history->getValues()[actualIndex];
    this->oldEntries.emplace(actualIndex, std::move(org));
    this->history->refValues()[actualIndex] = DSValue::createStr(curBuf);
    return true;
  }
  return false;
}

} // namespace ydsh