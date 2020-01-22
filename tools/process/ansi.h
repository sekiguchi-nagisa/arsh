/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_ANSI_H
#define YDSH_TOOLS_ANSI_H

#include <vector>
#include <string>
#include <functional>

#include "misc/buffer.hpp"

namespace process {

/**
 * for VT100 escape sequence handling.
 * currently implement small subset.
 * see. http://bkclass.web.fc2.com/doc_vt100.html
 *      https://vt100.net/docs/vt100-ug/chapter3.html
 */
class Screen {
private:
    unsigned int maxRow; // y
    unsigned int maxCol; // x

    unsigned int row{0};
    unsigned int col{0};

    std::vector<ydsh::FlexBuffer<int>> bufs;

    std::function<void(std::string &&)> reporter;

    unsigned int eaw{1};

public:
    Screen(unsigned int row, unsigned int col) : maxRow(row), maxCol(col) {
        this->bufs.reserve(this->maxRow);
        for(unsigned int i = 0; i < this->maxRow; i++) {
            this->bufs.emplace_back();
            this->bufs.back().assign(this->maxCol, '\0');
        }
    }

    explicit Screen(std::pair<unsigned short, unsigned short> winsize) :
                Screen(winsize.first, winsize.second) {}

    Screen() : Screen(24, 80) {}

    void setReporter(std::function<void(std::string &&)> func) {
        this->reporter = std::move(func);
    }

    void setEAW(unsigned int v) {
        this->eaw = v;
    }

    /**
     * entry point
     * @param data
     * @param size
     * size of data
     * @return
     * if data has invalid UTF8 sequence, return false
     */
    bool interpret(const char *data, unsigned int size);

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
     * @param row
     * @param col
     */
    void setCursor(unsigned int row, unsigned int col) {    //FIXME: position ()
        this->row = row < this->maxRow ? row : this->maxRow - 1;
        this->col = col < this->maxCol ? col : this->maxCol - 1;
    }

    /**
     * set curosr to home position
     */
    void setCursor() {
        this->setCursor(0, 0);  //FIXME: home position is equivalent to (1,1) ?
    }

    std::pair<unsigned int, unsigned int> getPos() const {
        return {this->row + 1, this->col + 1};
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
        offset = offset == 0 ? 1 : offset;
        unsigned int pos = offset < this->col ? this->col - offset : 0;
        this->col = pos;
    }

    void right(unsigned int offset) {
        unsigned int pos = this->col + (offset == 0 ? 1 : offset);
        this->col = pos > this->maxCol ? this->maxCol : pos;
    }

    //FIXME:
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
    void setChar(int ch) {
        this->bufs.at(this->row).at(this->col) = ch;
    }
};


} // namespace process

#endif //YDSH_TOOLS_ANSI_H
