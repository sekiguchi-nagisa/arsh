/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_BUFFER_HPP
#define YDSH_MISC_BUFFER_HPP

#include <cstring>
#include <type_traits>
#include <exception>
#include <cassert>
#include <cmath>

#include "noncopyable.h"

namespace ydsh {

/**
 * only available POD type.
 * default maximum capacity is 4GB
 */
template <typename T, typename SIZE_T = unsigned int>
class FlexBuffer {
public:
    using size_type = SIZE_T;
    using iterator = T *;
    using const_iterator = const T *;
    using reference = T &;
    using const_reference = const T &;

    static const size_type MINIMUM_CAPACITY;
    static const size_type MAXIMUM_CAPACITY;

private:
    static_assert(std::is_unsigned<SIZE_T>::value, "need unsigned type");

    static_assert(std::is_pod<T>::value, "forbidden type");

    size_type maxSize;
    size_type usedSize;

    T *data;

    /**
     * expand memory of old.
     * if old is null, only allocate.
     */
    static T *allocArray(T *old, SIZE_T size) {
        T *ptr = static_cast<T *>(realloc(old, sizeof(T) * size));
        if(ptr == nullptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    void moveElements(iterator src, iterator dest) {
        if(src == dest) {
            return;
        }

        memmove(dest, src, sizeof(T) * (this->end() - src));
        if(src < dest) {
            this->usedSize += (dest - src);
        } else {
            this->usedSize -= (src - dest);
        }
    }

    /**
     * if out of range, throw exception
     */
    void checkRange(size_type index) const;

public:
    NON_COPYABLE(FlexBuffer);

    /**
     * default initial size is equivalent to MINIMUM_CAPACITY
     */
    explicit FlexBuffer(size_type initSize) :
            maxSize(initSize < MINIMUM_CAPACITY ? MINIMUM_CAPACITY : initSize),
            usedSize(0),
            data(allocArray(nullptr, this->maxSize)) { }

    /**
     * for lazy allocation
     */
    FlexBuffer() : maxSize(0), usedSize(0), data(nullptr) { }

    FlexBuffer(FlexBuffer<T, SIZE_T> &&buffer) :
            maxSize(buffer.maxSize), usedSize(buffer.usedSize), data(extract(std::move(buffer))) { }

    ~FlexBuffer() {
        free(this->data);
    }

    FlexBuffer<T, SIZE_T> &operator+=(const T &value);

    /**
     * buffer.data is not equivalent to this.data.
     */
    FlexBuffer<T, SIZE_T> &operator+=(const FlexBuffer<T, SIZE_T> &buffer);

    FlexBuffer<T, SIZE_T> &operator=(FlexBuffer<T, SIZE_T> &&buffer) noexcept;
    FlexBuffer<T, SIZE_T> &operator+=(FlexBuffer<T, SIZE_T> &&buffer);

    /**
     * value is not equivalent to this.data.
     */
    FlexBuffer<T, SIZE_T> &append(const T *value, size_type size);

    size_type capacity() const noexcept {
        return this->maxSize;
    }

    size_type size() const noexcept {
        return this->usedSize;
    }

    bool empty() const noexcept {
        return this->size() == 0;
    }

    const T *get() const noexcept {
        return this->data;
    }

    void clear() noexcept {
        this->usedSize = 0;
    }

    void swap(FlexBuffer<T, SIZE_T> &buf) noexcept {
        std::swap(this->usedSize, buf.usedSize);
        std::swap(this->maxSize, buf.maxSize);
        std::swap(this->data, buf.data);
    }

    /**
     * capacity will be at least reservingSize.
     */
    void reserve(size_type reservingSize);

    iterator begin() noexcept {
        return this->data;
    }

    iterator end() noexcept {
        return this->data + this->usedSize;
    }

    const_iterator begin() const noexcept {
        return this->data;
    }

    const_iterator end() const noexcept {
        return this->data + this->usedSize;
    }

    reference front() noexcept {
        return this->operator[](0);
    }

    const_reference front() const noexcept {
        return this->operator[](0);
    }

    reference back() noexcept {
        return this->operator[](this->usedSize - 1);
    }

    const_reference back() const noexcept {
        return this->operator[](this->usedSize - 1);
    }

    void pop_back() noexcept {
        this->usedSize--;
    }

    reference operator[](size_type index) noexcept {
        return this->data[index];
    }

    const_reference operator[](size_type index) const noexcept {
        return this->data[index];
    }

    reference at(size_type index);

    const_reference at(size_type index) const;

    /**
     * pos (begin() <= pos <= end()).
     * return position inserted element.
     */
    iterator insert(const_iterator pos, const T &value);

    /**
     * pos must not equivalent to this->end().
     */
    iterator erase(const_iterator pos);

    /**
     * first must be last or less. (first <= last).
     * last must be this->end() or less. (last <= this->end())
     * first is inclusive, last is exclusive.
     */
    iterator erase(const_iterator first, const_iterator last);

    void assign(size_type n, const T &value);

    /**
     * extract data. after call it, maxSize and usedSize is 0, and data is null.
     * call free() to release returned pointer.
     */
    friend T *extract(FlexBuffer<T, SIZE_T> &&buf) noexcept {
        buf.maxSize = 0;
        buf.usedSize = 0;
        T *ptr = buf.data;
        buf.data = nullptr;
        return ptr;
    }
};

// ########################
// ##     FlexBuffer     ##
// ########################

template <typename T, typename SIZE_T>
const typename FlexBuffer<T, SIZE_T>::size_type FlexBuffer<T, SIZE_T>::MINIMUM_CAPACITY = 8;

template <typename T, typename SIZE_T>
const typename FlexBuffer<T, SIZE_T>::size_type FlexBuffer<T, SIZE_T>::MAXIMUM_CAPACITY = static_cast<SIZE_T>(-1);

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::checkRange(size_type index) const {
    if(index >= this->usedSize) {
        std::string str("size is: ");
        str += std::to_string(this->usedSize);
        str += ", but index is: ";
        str += std::to_string(index);
        throw std::out_of_range(str);
    }
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(const T &value) {
    this->reserve(this->usedSize + 1);
    this->data[this->usedSize++] = value;
    return *this;
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(const FlexBuffer<T, SIZE_T> &buffer) {
    return this->append(buffer.get(), buffer.size());
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator=(FlexBuffer<T, SIZE_T> &&buffer) noexcept {
    FlexBuffer<T, SIZE_T> tmp(std::move(buffer));
    this->swap(tmp);
    return *this;
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(FlexBuffer<T, SIZE_T> &&buffer) {
    FlexBuffer<T, SIZE_T> tmp(std::move(buffer));
    *this += tmp;
    return *this;
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::append(const T *value, size_type size) {
    if(this->data == value) {
        throw std::invalid_argument("appending own buffer");
    }
    this->reserve(this->usedSize + size);
    memcpy(this->data + this->usedSize, value, sizeof(T) * size);
    this->usedSize += size;
    return *this;
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::reserve(size_type reservingSize) {
    if(reservingSize > this->maxSize) {
        std::size_t newSize = (this->maxSize == 0 ? MINIMUM_CAPACITY : this->maxSize);
        while(newSize < reservingSize) {
            newSize += (newSize >> 1);
        }

        if(newSize > MAXIMUM_CAPACITY) {
            throw std::length_error("reach maximum capacity");
        }

        this->maxSize = newSize;
        this->data = allocArray(this->data, newSize);
    }
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::reference FlexBuffer<T, SIZE_T>::at(size_type index) {
    this->checkRange(index);
    return this->data[index];
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::const_reference FlexBuffer<T, SIZE_T>::at(size_type index) const {
    this->checkRange(index);
    return this->data[index];
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::insert(const_iterator pos, const T &value) {
    assert(pos >= this->begin() && pos <= this->end());

    const size_type index = pos - this->begin();
    this->reserve(this->size() + 1);
    iterator iter = this->begin() + index;

    this->moveElements(iter, iter + 1);
    this->data[index] = value;

    return iter;
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::erase(const_iterator pos) {
    assert(pos < this->end());

    const size_type index = pos - this->begin();
    iterator iter = this->begin() + index;

    this->moveElements(iter + 1, iter);

    return iter;
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::erase(const_iterator first, const_iterator last) {
    assert(last <= this->end());
    assert(first <= last);

    const size_type index = first - this->begin();
    iterator iter = this->begin() + index;

    this->moveElements(iter + (last - first), iter);

    return iter;
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::assign(size_type n, const T &value) {
    this->reserve(this->usedSize + n);
    for(unsigned int i = 0; i < n; i++) {
        this->data[this->usedSize++] = value;
    }
}

typedef FlexBuffer<char> ByteBuffer;

// for byte reading
inline unsigned char read8(const unsigned char *const code, unsigned int index) noexcept {
    return code[index];
}

inline unsigned short read16(const unsigned char *const code, unsigned int index) noexcept {
    return read8(code, index) << 8 | read8(code, index + 1);
}

inline unsigned int read32(const unsigned char *const code, unsigned int index) noexcept {
    return read8(code, index) << 24 | read8(code, index + 1) << 16
           | read8(code, index + 2) << 8 | read8(code, index + 3);
}

inline unsigned long read64(const unsigned char *const code, unsigned int index) noexcept {
    unsigned long v = 0;
    for(unsigned int i = 0; i < 8; i++) {
        v |= static_cast<unsigned long>(read8(code, index + i)) << ((7 - i) * 8);
    }
    return v;
}

// for byte writing
inline void write8(unsigned char *ptr, unsigned char b) noexcept {
    *ptr = b;
}

inline void write16(unsigned char *ptr, unsigned short b) noexcept {
    write8(ptr, (b & 0xFF00) >> 8);
    write8(ptr + 1, b & 0xFF);
}

inline void write32(unsigned char *ptr, unsigned int b) noexcept {
    write8(ptr,     (b & 0xFF000000) >> 24);
    write8(ptr + 1, (b & 0xFF0000) >> 16);
    write8(ptr + 2, (b & 0xFF00) >> 8);
    write8(ptr + 3,  b & 0xFF);
}

inline void write64(unsigned char *ptr, unsigned long b) noexcept {
    for(unsigned int i = 0; i < 8; i++) {
        const unsigned long mask = 0xFF * static_cast<unsigned long>(pow(0x100, 7 - i));
        write8(ptr + i, (b & mask) >> ((7 - i) * 8));
    }
}

} // namespace ydsh


#endif //YDSH_MISC_BUFFER_HPP
