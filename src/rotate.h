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

#ifndef YDSH_ROTATE_H
#define YDSH_ROTATE_H

#include "object.h"

namespace ydsh {

class HistRotator {
private:
  static_assert(SYS_LIMIT_HIST_SIZE < SYS_LIMIT_ARRAY_MAX);
  static_assert(SYS_LIMIT_HIST_SIZE < UINT32_MAX);

  std::unordered_map<unsigned int, DSValue> oldEntries;
  ObjPtr<ArrayObject> history;
  int histIndex{0};
  unsigned int maxSize{SYS_LIMIT_HIST_SIZE};

public:
  enum class Op {
    PREV,
    NEXT,
  };

  explicit HistRotator(ObjPtr<ArrayObject> history);

  ~HistRotator() { this->revertAll(); }

  void setMaxSize(unsigned int size) {
    this->maxSize = std::min(size, static_cast<unsigned int>(SYS_LIMIT_HIST_SIZE));
  }

  unsigned int getMaxSize() const { return this->maxSize; }

  void revertAll();

  explicit operator bool() { return this->history && this->history->size() > 0; }

  /**
   * save current buffer and get next entry
   * @param curLine
   * @param next
   */
  bool rotate(StringRef &curBuf, HistRotator::Op op);

private:
  void truncateUntilLimit(bool beforeAppend = false);

  bool save(ssize_t index, StringRef curBuf);
};

class KillRing {
private:
  static constexpr size_t INIT_SIZE = 16;

  static_assert(INIT_SIZE <= SYS_LIMIT_KILL_RING_MAX);
  static_assert(SYS_LIMIT_KILL_RING_MAX <= UINT32_MAX);

  ObjPtr<ArrayObject> obj; // may be null
  unsigned int rotateIndex{0};
  unsigned int maxSize{INIT_SIZE};

public:
  void setMaxSize(unsigned int v) {
    this->maxSize = std::min(v, static_cast<unsigned int>(SYS_LIMIT_KILL_RING_MAX));
  }

  unsigned int getMaxSize() const { return this->maxSize; }

  const auto &get() const { return this->obj; }

  explicit operator bool() const { return static_cast<bool>(this->get()) && this->obj->size() > 0; }

  void add(StringRef ref);

  /**
   * if no entry, return empty string
   * @return
   */
  StringRef rotate(); // FIXME:
};

} // namespace ydsh

#endif // YDSH_ROTATE_H
