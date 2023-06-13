/*
 * Copyright (C) 2018-2020 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_PROCESS_ANSI_H
#define YDSH_TOOLS_PROCESS_ANSI_H

#include <functional>
#include <string>
#include <vector>

#include "misc/lexer_base.hpp"

namespace process {

/**
 * for VT100 escape sequence handling.
 * currently implement small subset.
 * see. http://bkclass.web.fc2.com/doc_vt100.html
 *      https://vt100.net/docs/vt100-ug/chapter3.html
 */
class Screen : public ydsh::LexerBase {
private:
  unsigned int maxRow; // y
  unsigned int maxCol; // x

  unsigned int row{0};
  unsigned int col{0};

  std::vector<ydsh::FlexBuffer<int>> bufs;

  std::function<void(std::string &&)> reporter;

  unsigned int eaw{1};

  unsigned char yych{0};
  unsigned int yyaccept{0};
  int state{-1};
  const char *start{nullptr};

public:
  enum Result {
    NEED_MORE,
    REACH_EOS,
    INVALID,
  };

  struct Pos {
    unsigned int row;
    unsigned int col;

    static Pos defaultSize() { return {24, 80}; }
  };

  explicit Screen(Pos pos) : LexerBase("<screen>"), maxRow(pos.row), maxCol(pos.col) {
    this->bufs.reserve(this->maxRow);
    for (unsigned int i = 0; i < this->maxRow; i++) {
      this->bufs.emplace_back();
      this->bufs.back().insert(this->bufs.back().end(), this->maxCol, '\0');
    }
  }

  Screen() : Screen(Pos::defaultSize()) {}

  void setReporter(std::function<void(std::string &&)> func) { this->reporter = std::move(func); }

  void setEAW(unsigned int v) { this->eaw = v; }

  /**
   * entry point
   * @param data
   * @param size
   * size of data
   * @return
   * if data has invalid UTF8 sequence, return INVALID.
   */
  Result interpret(const char *data, unsigned int size);

  /**
   *
   * @param ch
   * must be ascii
   */
  void addChar(int ch);

  /**
   * currently not support combining character
   * @param begin
   * @param end
   */
  void addCodePoint(const char *begin, const char *end);

  /**
   * FIXME:
   * @param r
   * @param c
   */
  void setCursor(unsigned int r, unsigned int c) { // FIXME: position ()
    this->row = r < this->maxRow ? r : this->maxRow - 1;
    this->col = c < this->maxCol ? c : this->maxCol - 1;
  }

  /**
   * set cursor to home position
   */
  void setCursor() {
    this->setCursor(0, 0); // FIXME: home position is equivalent to (1,1) ?
  }

  Pos getCursor() const {
    return {
        .row = this->row + 1,
        .col = this->col + 1,
    };
  }

  void reportPos();

  // clear screen ops
  void clear();

  /**
   * clear line from current cursor.
   */
  void clearLineFrom();

  void clearLine();

  // move cursor ops
  void left(unsigned int offset) {
    offset = offset == 0 ? 1 : offset; // FIXME:
    unsigned int pos = offset < this->col ? this->col - offset : 0;
    this->col = pos;
  }

  void right(unsigned int offset) {
    unsigned int pos = this->col + (offset == 0 ? 1 : offset); // FIXME: 0-based or 1-based ?
    this->col = pos > this->maxCol ? this->maxCol : pos;
  }

  // FIXME:
  //    void up(unsigned int offset) {
  //        offset = offset == 0 ? 1 : offset;
  //        for(unsigned int i = 0; i < offset && this->row >= 0; i++) {
  //            this->row--;
  //        }
  //    }

  //    void down(unsigned int offset) {
  //        offset = offset == 0 ? 1 : offset;
  //        for(unsigned int i = 0; i < offset && this->row < this->maxRow; i++) {
  //            this->row++;
  //        }
  //    }

  std::string toString() const;

private:
  void setChar(int ch) { this->bufs.at(this->row).at(this->col) = ch; }

  void appendToBuf(const char *data, unsigned int size) {
    unsigned int old = this->start - this->buf.data();
    LexerBase::appendToBuf(data, size, false);
    this->start = this->buf.data() + old;
  }
};

} // namespace process

#endif // YDSH_TOOLS_PROCESS_ANSI_H
