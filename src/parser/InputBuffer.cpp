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

#include <string.h>

#include <parser/InputBuffer.h>

InputBuffer::InputBuffer() :
        InputBuffer(512) {
}

InputBuffer::InputBuffer(unsigned int initSize) :
        usedSize(0), maxSize(initSize), buf(new char[initSize]) {
}

InputBuffer::~InputBuffer() {
    delete[] this->buf;
}

unsigned int InputBuffer::getUsedSize() {
    return this->usedSize;
}

unsigned int InputBuffer::getMaxSize() {
    return this->maxSize;
}

char *InputBuffer::getBuf() {
    return this->buf;
}

void InputBuffer::append(unsigned int size, char *b) {
    unsigned int curSize = this->usedSize + size;
    if(curSize > this->maxSize) {   // expand buffer.
        unsigned int newSize = this->maxSize * 2;
        char *newBuf = new char[newSize];
        memcpy(newBuf, this->buf, this->usedSize);
        delete[] this->buf;
        this->buf = newBuf;
        this->maxSize = newSize;
    }

    for(unsigned int i = 0; i < size; i++) {
        this->buf[this->usedSize + i] = b[i];
    }
    this->usedSize = curSize;
}

