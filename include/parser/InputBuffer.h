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

#ifndef PARSER_INPUTBUFFER_H_
#define PARSER_INPUTBUFFER_H_

class InputBuffer {
private:
    /**
     * used buffer size.
     * must be usedSize <= maxSize.
     */
    unsigned int usedSize;

    /**
     * maximum size of buffer.
     */
    unsigned int maxSize;

    char *buf;

public:
    InputBuffer();
    InputBuffer(unsigned int initSize);
    ~InputBuffer();

    unsigned int getUsedSize();
    unsigned int getMaxSize();
    char *getBuf();

    /**
     * append additional buffer.
     * if this->usedSize + size > this->maxSize,
     * allocate new buffer, and copy to it.
     */
    void append(unsigned int size, char *b);
};



#endif /* PARSER_INPUTBUFFER_H_ */
