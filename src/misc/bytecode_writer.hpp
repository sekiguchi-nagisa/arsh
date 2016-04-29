/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_BYTECODE_WRITER_HPP
#define YDSH_MISC_BYTECODE_WRITER_HPP

#include <unordered_map>
#include <vector>
#include <cmath>

#include "buffer.hpp"

namespace ydsh {
namespace misc {

class Label {
private:
    struct LabelImpl {
        unsigned int refCount;

        /**
         * indicate byte code index.
         */
        unsigned int index;

        LabelImpl() : refCount(1), index(0) {}
    };

    LabelImpl *body;

    void swap(Label &l) {
        std::swap(this->body, l.body);
    }

public:
    Label() : body(new LabelImpl()) { }

    Label(const Label &label) noexcept : body(label.body) {
        this->body->refCount++;
    }

    Label(Label &&label) noexcept : body(label.body) {
        label.body = nullptr;
    }

    ~Label() {
        if(this->body != nullptr && --this->body->refCount == 0) {
            delete this->body;
        }
        this->body = nullptr;
    }

    Label &operator=(const Label &label) noexcept {
        Label tmp(label);
        this->swap(tmp);
        return *this;
    }

    Label &operator=(Label &&label) noexcept {
        Label tmp(std::move(label));
        this->swap(tmp);
        return *this;
    }

    std::size_t hash() const noexcept {
        return std::hash<long>()(reinterpret_cast<long>(this->body));
    }

    bool operator==(const Label &l) const noexcept {
        return this->body == l.body;
    }

    void setIndex(unsigned int index) {
        this->body->index = index;
    }

    unsigned int getIndex() const {
        return this->body->index;
    }
};

using ByteCode = FlexBuffer<unsigned char>;

namespace __detail_writer {

template <bool T>
struct ByteCodeWriter {
    static_assert(true, "not allow instantiation");

    struct GenHash {
        bool operator()(const Label &l) const noexcept {
            return l.hash();
        }
    };

    std::unordered_map<Label, std::vector<unsigned int>, GenHash> labelMap;

    ByteCode code;

    void write8(unsigned int index, unsigned char b) {
        this->code[index] = b;
    }

    void write16(unsigned int index, unsigned short b) {
        this->write8(index, (b & 0xFF00) >> 8);
        this->write8(index + 1, b & 0xFF);
    }

    void write32(unsigned int index, unsigned int b) {
        this->write8(index, (b & 0xFF000000) >> 24);
        this->write8(index + 1, (b & 0xFF0000) >> 16);
        this->write8(index + 2, (b & 0xFF00) >> 8);
        this->write8(index + 3, b & 0xFF);
    }

    void write(unsigned int index, unsigned int b) {
        if(b <= UINT8_MAX) {
            this->write8(index, static_cast<unsigned char>(b));
        } else if(b <= UINT16_MAX) {
            this->write16(index, static_cast<unsigned short>(b));
        } else {
            this->write32(index, b);
        }
    }

    unsigned char read8(unsigned int index) const noexcept {
        return this->code[index];
    }

    unsigned short read16(unsigned int index) const noexcept {
        return this->read8(index) << 8 | this->read8(index + 1);
    }

    unsigned int read32(unsigned int index) const noexcept {
        return this->read8(index) << 24 | this->read8(index + 1) << 16
               | this->read8(index + 2) << 8 | this->read8(index + 3);
    }


    void markLabel(unsigned int index, Label &label) {
        label.setIndex(index);
        this->labelMap[label];
    }

    void writeLabel(unsigned int index, const Label &label) {
        this->labelMap[label].push_back(index);
    }

    void finalize() {
        // write labeled index.
        for(auto &e : this->labelMap) {
            const unsigned int location = e.first.getIndex();
            for(auto &u : e.second) {
                this->write(u, location);
            }
        }
        this->labelMap.clear();
    }
};

} // namespace __detail_writer

using ByteCodeWriter = __detail_writer::ByteCodeWriter<true>;

} // namespace misc
} // namespace ydsh

#endif //YDSH_MISC_BYTECODE_WRITER_HPP
