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

#ifndef YDSH_LINE_BUFFER_H
#define YDSH_LINE_BUFFER_H

#include "misc/buffer.hpp"
#include "misc/ring_buffer.hpp"
#include "misc/string_ref.hpp"

namespace ydsh {

class LineBuffer {
private:
  char *buf;            // user specified buffer. not delete it
  const size_t bufSize; // reserve sentinel null. so actual size is bufSize + 1
  unsigned int cursor{0};
  unsigned int usedSize{0};
  FlexBuffer<unsigned int> newlinePosList;

public:
  LineBuffer(char *buf, size_t bufSize) : buf(buf), bufSize(bufSize - 1) {
    this->buf[0] = '\0'; // always null terminated
  }

  bool isSingleLine() const { return this->newlinePosList.empty(); }

  const auto &getNewlinePosList() const { return this->newlinePosList; }

  void syncNewlinePosList();

  void clearNewlinePosList() { this->newlinePosList.clear(); }

  const char *getRawBuf() const { return this->buf; }

  unsigned int getCursor() const { return this->cursor; }

  void setCursor(unsigned int v) { this->cursor = v; }

  unsigned int getUsedSize() const { return this->usedSize; }

  StringRef get() const { return {this->buf, this->usedSize}; }

  StringRef getToCursor() const { return {this->buf, this->cursor}; }

  StringRef getFromCursor() const { return this->get().substr(this->cursor); }

  /**
   * get prev grapheme cluster bytes
   * @return
   */
  size_t prevCharBytes() const;

  /**
   * get next grapheme cluster bytes
   * @return
   */
  size_t nextCharBytes() const;

  size_t prevWordBytes() const;

  size_t nextWordBytes() const;

  struct Interval {
    unsigned int pos;
    unsigned int len;
  };

  /**
   * get interval (pos, len) of current cursor line
   * @param wholeLine
   * if true, get whole current line
   * if false, get line until current cursor
   * @return
   * start position of current line, length of current line
   */
  Interval findCurLineInterval(bool wholeLine) const;

  /**
   *
   * @return
   */
  unsigned int findCurNewlineIndex() const;

  // edit op

  /**
   * insert bytes to cursor position.
   * after insertion, increment cursor by size
   * @param data
   * @param size
   * @return
   * if insertion succeed, return true
   * otherwise, return false
   */
  bool insertToCursor(const char *data, size_t size);

  bool insertToCursor(StringRef ref) { return this->insertToCursor(ref.data(), ref.size()); }

  /**
   * delete bytes at the left of cursor
   * after deletion, decrement cursor by size
   * @param size
   * @param capture
   * may be null
   * @return
   * if deletion succeed, return true
   */
  bool deleteToCursor(size_t size, std::string *capture = nullptr);

  /**
   * delete bytes at the right of cursor
   * after deletion, not decrement cursor (but still decrement usedSize by size)
   * @param size
   * @param capture
   * may be null
   * @return
   * if deletion succeed, return true
   */
  bool deleteFromCursor(size_t size, std::string *capture = nullptr);

  void deleteAll() {
    this->cursor = 0;
    this->usedSize = 0;
    this->buf[0] = '\0';
  }
};

} // namespace ydsh

#endif // YDSH_LINE_BUFFER_H
