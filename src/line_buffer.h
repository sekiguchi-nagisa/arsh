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

#ifndef ARSH_LINE_BUFFER_H
#define ARSH_LINE_BUFFER_H

#include "constant.h"
#include "misc/buffer.hpp"
#include "misc/ring_buffer.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

class LineBuffer {
public:
  struct EditOp {
    bool trackChange;
    bool mergeChange;
  };

  enum class ChangeOp : unsigned char {
    INSERT,      // old_cursor + delta => cursor
    DELETE_TO,   // old_cursor - delta => cursor
    DELETE_FROM, // no cursor changed
  };

  struct Change {
    ChangeOp type;

    bool merge;

    /**
     * cursor after changed
     */
    unsigned int cursor;

    std::string delta;

    Change(ChangeOp op, unsigned int cursor, std::string &&delta, bool merge)
        : type(op), merge(merge), cursor(cursor), delta(std::move(delta)) {}

    /**
     *
     * @param o
     * @return
     * if merge succeed, return true.
     * otherwise, return false (original object is not changed)
     */
    bool tryMerge(const Change &o);
  };

private:
  char *buf;            // user specified buffer. not delete it
  const size_t bufSize; // reserve sentinel null. so actual size is bufSize + 1
  unsigned int cursor{0};
  unsigned int usedSize{0};
  FlexBuffer<unsigned int> newlinePosList;
  RingBuffer<Change> changes;
  unsigned int changeIndex{0};

public:
  NON_COPYABLE(LineBuffer);

  LineBuffer(char *buf, size_t bufSize) : buf(buf), bufSize(bufSize - 1), changes(10) {
    this->buf[0] = '\0'; // always null terminated
  }

  bool isSingleLine() const { return this->newlinePosList.empty(); }

  const auto &getNewlinePosList() const { return this->newlinePosList; }

  void syncNewlinePosList();

  void clearNewlinePosList() { this->newlinePosList.clear(); }

  unsigned int getCursor() const { return this->cursor; }

  void setCursor(unsigned int v) { this->cursor = std::min(v, this->getUsedSize()); }

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

  /**
   * get current line
   * @param wholeLine
   * if true, get whole current line
   * if false, line until current cursor
   * @return
   */
  StringRef getCurLine(bool wholeLine) const {
    auto interval = this->findCurLineInterval(wholeLine);
    return {this->buf + interval.pos, interval.len};
  }

  /**
   * @param inserting
   * @param single
   * @return start pos of inserting prefix
   */
  size_t resolveInsertingSuffix(StringRef &inserting, bool single) const;

  // edit op

  /**
   * insert bytes to cursor position.
   * after insertion, increment cursor by size
   * @param ref
   * @param merge
   * @return
   * if insertion succeed, return true (if ref is empty, return true)
   * otherwise, return false
   */
  bool insertToCursor(StringRef ref, bool merge = false) {
    return this->insertToCursor(ref, EditOp{.trackChange = true, .mergeChange = merge});
  }

  /**
   * delete bytes at the left of cursor
   * after deletion, decrement cursor by size
   * @param size
   * @param capture
   * @param merge
   * @return
   * if deletion succeed, return true
   */
  bool deleteToCursor(size_t size, std::string *capture = nullptr, bool merge = false) {
    return this->deleteToCursor(size, capture, EditOp{.trackChange = true, .mergeChange = merge});
  }

  /**
   * delete bytes at the right of cursor
   * after deletion, not decrement cursor (but still decrement usedSize by size)
   * @param size
   * @param capture
   * @param merge
   * @return
   * if deletion succeed, return true
   */
  bool deleteFromCursor(size_t size, std::string *capture = nullptr, bool merge = false) {
    return this->deleteFromCursor(size, capture, EditOp{.trackChange = true, .mergeChange = merge});
  }

  bool deletePrevChar(std::string *capture, bool merge = false) {
    size_t charBytes = this->prevCharBytes();
    return this->deleteToCursor(charBytes, capture, merge);
  }

  bool deleteNextChar(std::string *capture, bool merge = false) {
    size_t charBytes = this->nextCharBytes();
    return this->deleteFromCursor(charBytes, capture, merge);
  }

  bool deletePrevWord(std::string *capture) {
    size_t wordBytes = this->prevWordBytes();
    return this->deleteToCursor(wordBytes, capture);
  }

  bool deleteNextWord(std::string *capture) {
    size_t wordBytes = this->nextWordBytes();
    return this->deleteFromCursor(wordBytes, capture);
  }

  bool deleteLineToCursor(bool wholeLine, std::string *capture) {
    auto [pos, len] = this->findCurLineInterval(wholeLine);
    this->cursor = pos + len;
    return this->deleteToCursor(len, capture);
  }

  bool deleteLineFromCursor(std::string *capture) {
    const unsigned int oldCursor = this->cursor;
    this->moveCursorToEndOfLine();
    const unsigned int newCursor = this->cursor;
    this->cursor = oldCursor;
    return this->deleteFromCursor(newCursor - oldCursor, capture);
  }

  void deleteAll() {
    this->cursor = this->getUsedSize();
    this->deleteToCursor(this->getUsedSize());
  }

  bool moveCursorToLeftByChar() {
    if (this->getCursor() > 0) {
      this->cursor -= this->prevCharBytes();
      return true;
    }
    return false;
  }

  bool moveCursorToRightByChar() {
    if (this->getCursor() != this->getUsedSize()) {
      this->cursor += this->nextCharBytes();
      return true;
    }
    return false;
  }

  bool moveCursorToLeftByWord() {
    if (this->getCursor() > 0) {
      this->cursor -= this->prevWordBytes();
      return true;
    }
    return false;
  }

  bool moveCursorToRightByWord() {
    if (this->getCursor() != this->getUsedSize()) {
      this->cursor += this->nextWordBytes();
      return true;
    }
    return false;
  }

  bool moveCursorToStartOfLine() {
    unsigned int newCursor;
    if (this->isSingleLine()) { // single-line
      newCursor = 0;
    } else { // multi-line
      unsigned int index = this->findCurNewlineIndex();
      if (index == 0) {
        newCursor = 0;
      } else {
        newCursor = this->newlinePosList[index - 1] + 1;
      }
    }
    if (this->getCursor() != newCursor) {
      this->cursor = newCursor;
      return true;
    }
    return false;
  }

  bool moveCursorToEndOfLine() {
    unsigned int newCursor;
    if (this->isSingleLine()) {
      newCursor = this->getUsedSize();
    } else { // multi-line
      unsigned int index = this->findCurNewlineIndex();
      if (index == this->newlinePosList.size()) {
        newCursor = this->getUsedSize();
      } else {
        newCursor = this->newlinePosList[index];
      }
    }
    if (this->getCursor() != newCursor) {
      this->cursor = newCursor;
      return true;
    }
    return false;
  }

  bool moveCursorUpDown(bool up);

  void commitLastChange() {
    if (this->changeIndex > 0) {
      this->changes[this->changeIndex - 1].merge = false;
    }
  }

  bool undo();

  bool redo();

private:
  bool insertToCursor(StringRef ref, EditOp editOp);

  /**
   * delete bytes at the left of cursor
   * after deletion, decrement cursor by size
   * @param size
   * @param capture
   * may be null
   * @param editOp
   * @return
   * if deletion succeed, return true
   */
  bool deleteToCursor(size_t size, std::string *capture, EditOp editOp);

  /**
   * delete bytes at the right of cursor
   * after deletion, not decrement cursor (but still decrement usedSize by size)
   * @param size
   * @param capture
   * may be null
   * @param editOp
   * @return
   * if deletion succeed, return true
   */
  bool deleteFromCursor(size_t size, std::string *capture, EditOp editOp);

  void trackChange(ChangeOp op, std::string &&delta, bool merge);
};

class KillRing {
private:
  static constexpr size_t INIT_SIZE = 15;

  static_assert(INIT_SIZE <= SYS_LIMIT_KILL_RING_MAX);
  static_assert(SYS_LIMIT_KILL_RING_MAX <= UINT32_MAX);

  RingBuffer<std::string> buf;
  unsigned int rotateIndex{0};

public:
  KillRing() : buf(INIT_SIZE) {}

  const auto &get() const { return this->buf; }

  void add(std::string &&value) {
    if (!value.empty()) {
      this->buf.push_back(std::move(value));
    }
  }

  void reset() { this->rotateIndex = this->buf.size() - 1; }

  explicit operator bool() const { return !this->buf.empty(); }

  const std::string &getCurrent() const { return this->buf[this->rotateIndex]; }

  void rotate() {
    const unsigned int size = this->buf.size();
    if (this->rotateIndex > 0 && this->rotateIndex < size) {
      this->rotateIndex--;
    } else {
      this->rotateIndex = size - 1;
    }
  }

  /**
   * re-allocate internal ring buffer
   * @param afterCap
   */
  void expand(unsigned int afterCap);
};

} // namespace arsh

#endif // ARSH_LINE_BUFFER_H
