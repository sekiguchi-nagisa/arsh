/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef YDSH_INPUTBUFFER_HPP
#define YDSH_INPUTBUFFER_HPP

#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>

namespace ydsh {
namespace parser {
namespace __input_buffer_detail {

/**
 * input buffer for re2c
 */
template <unsigned int DEFAULT_SIZE>
class InputBuffer {
protected:

    static_assert(DEFAULT_SIZE >= 32, "default buffer size must be more than 32");

    /**
     * may be null, if input source is string. not closed it.
     */
    FILE *fp;

    unsigned int bufSize;

    /**
     * must terminate null character.
     */
    unsigned char *buf;

    /**
     * current reading pointer of buf.
     */
    unsigned char *cursor;

    /**
     * limit of buf.
     */
    unsigned char *limit;

    /**
     * for backtracking.
     */
    unsigned char *marker;

    /**
     * for trailing context.
     */
    unsigned char *ctxMarker;

    /**
     * if fp is null or fp reach EOF, it it true.
     */
    bool endOfFile;

    /**
     * if true, reach end of string. nextToken() always return EOS.
     */
    bool endOfString;

private:
    InputBuffer(unsigned int initSize, bool fixed = false) :
            fp(0),
            bufSize((fixed || initSize > DEFAULT_SIZE) ? initSize : DEFAULT_SIZE),
            buf(new unsigned char[this->bufSize]),
            cursor(this->buf), limit(this->buf), marker(0), ctxMarker(0),
            endOfFile(fixed), endOfString(false) {
        this->buf[0] = '\0';    // terminate null character.
    }

    InputBuffer(unsigned int initSize, FILE *fp) :
            InputBuffer(initSize) {
        this->fp = fp;
    }

public:
    /**
     * equivalent to Lexer(DEFAULT_SIZE, fp).
     */
    explicit InputBuffer(FILE *fp) : InputBuffer(DEFAULT_SIZE, fp) {
    }

    /**
     * copy src to this->buf.
     * src must terminate null character.
     */
    explicit InputBuffer(const char *src) : InputBuffer(strlen(src) + 1, true) {
        this->copySrcBuf(src);
    }

    /**
     * not allow copy constructor
     */
    explicit InputBuffer(const InputBuffer<DEFAULT_SIZE> &buffer);

    virtual ~InputBuffer() {
        delete[] this->buf;
        this->buf = 0;
    }

    /**
     * get current reading position.
     */
    unsigned int getPos() const {
        return this->cursor - this->buf;
    }

    /**
     * used size of buf. must be this->getUsedSize() <= this->getBufSize().
     */
    unsigned int getUsedSize() const {
        return this->limit - this->buf + 1;
    }

private:
    /**
     * used for constructor. not use it.
     */
    void copySrcBuf(const void *srcBuf) {
        memcpy(this->buf, srcBuf, this->bufSize);
        this->limit += this->bufSize - 1;
    }

    /**
     * if this->usedSize + needSize > this->maxSize, expand buf.
     */
    void expandBuf(unsigned int needSize);

protected:
    /**
     * fill buffer. called from this->nextToken().
     */
    bool fill(int n);
};

// #########################
// ##     InputBuffer     ##
// #########################

template<unsigned int DEFAULT_SIZE>
void InputBuffer<DEFAULT_SIZE>::expandBuf(unsigned int needSize) {
    unsigned int usedSize = this->getUsedSize();
    unsigned int size = usedSize + needSize;
    if(size > this->bufSize) {
        unsigned int newSize = this->bufSize;
        do {
            newSize *= 2;
        } while(newSize < size);
        unsigned int pos = this->getPos();
        unsigned int markerPos = this->marker - this->buf;
        unsigned int ctxMarkerPos = this->ctxMarker - this->buf;
        unsigned char *newBuf = new unsigned char[newSize];
        memcpy(newBuf, this->buf, usedSize);
        delete[] this->buf;
        this->buf = newBuf;
        this->bufSize = newSize;
        this->cursor = this->buf + pos;
        this->limit = this->buf + usedSize - 1;
        this->marker = this->buf + markerPos;
        this->ctxMarker = this->buf + ctxMarkerPos;
    }
}

template<unsigned int DEFAULT_SIZE>
bool InputBuffer<DEFAULT_SIZE>::fill(int n) {
    if(this->endOfString && this->limit - this->cursor <= 0) {
        return false;
    }

    if(!this->endOfFile) {
        int needSize = n - (this->limit - this->cursor);
        assert(needSize > -1);
        this->expandBuf(needSize);
        int readSize = fread(this->limit, sizeof(unsigned char), needSize, this->fp);
        this->limit += readSize;
        *this->limit = '\0';
        if(readSize < needSize) {
            this->endOfFile = true;
        }
    }
    return true;
}

} // namespace __input_buffer_detail

typedef __input_buffer_detail::InputBuffer<256> InputBuffer;

} // namespace parser
} // namespace ydsh

#endif //YDSH_INPUTBUFFER_HPP
