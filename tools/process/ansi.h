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

#ifndef ARSH_TOOLS_PROCESS_ANSI_H
#define ARSH_TOOLS_PROCESS_ANSI_H

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
class Screen : public arsh::LexerBase {
private:
  unsigned int maxRows; // y
  unsigned int maxCols; // x

  unsigned int maxUsedRows{1};

  unsigned int row{0};
  unsigned int col{0};

  std::vector<arsh::FlexBuffer<int>> bufs;

  std::function<void(std::string &&)> reporter;

  arsh::AmbiguousCharWidth eaw{arsh::AmbiguousCharWidth::HALF};

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

  explicit Screen(Pos pos);

  Screen() : Screen(Pos::defaultSize()) {}

  void setReporter(std::function<void(std::string &&)> func) { this->reporter = std::move(func); }

  void setEAW(arsh::AmbiguousCharWidth v) { this->eaw = v; }

  void resize(Pos pos);

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
   * set cursor position (1-based)
   * @param pos
   */
  void setCursor(Pos pos) {
    this->row = std::min(pos.row - 1, this->maxRows - 1);
    this->col = std::min(pos.col - 1, this->maxCols - 1);
    this->updateMaxUsedRows();
  }

  /**
   * set cursor to home position
   */
  void setCursor() { this->setCursor(Pos{1, 1}); }

  /**
   * get 1-based cursor
   * @return
   */
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

  /**
   *
   * @param offset
   * 0-based
   */
  void left(unsigned int offset) {
    unsigned int pos = offset < this->col ? this->col - offset : 0;
    this->col = pos;
  }

  /**
   *
   * @param offset
   * 0-based
   */
  void right(unsigned int offset) {
    unsigned int pos = this->col + offset;
    this->col = pos < this->maxCols ? pos : this->maxCols - 1;
  }

  void up(unsigned int offset) {
    if (this->row > offset) {
      this->row -= offset;
    } else {
      this->row = 0;
    }
  }

  void down(unsigned int offset) {
    if (this->row + offset < this->maxRows) {
      this->row += offset;
    } else {
      this->row = this->maxRows - 1;
    }
    this->updateMaxUsedRows();
  }

  std::string toString() const;

private:
  void updateMaxUsedRows() {
    if (this->row + 1 > this->maxUsedRows) {
      this->maxUsedRows = this->row + 1;
    }
  }

  void setChar(int ch) { this->bufs.at(this->row).at(this->col) = ch; }

  void appendToBuf(const char *data, unsigned int size) {
    unsigned int old = this->start - this->buf.data();
    LexerBase::appendToBuf(data, size, false);
    this->start = this->buf.data() + old;
  }
};

} // namespace process

#endif // ARSH_TOOLS_PROCESS_ANSI_H
