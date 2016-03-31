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

#include "noncopyable.h"

namespace ydsh {
namespace misc {

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

    FlexBuffer<T, SIZE_T> &operator+=(T value);

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

    size_type capacity() const {
        return this->maxSize;
    }

    size_type size() const {
        return this->usedSize;
    }

    bool empty() const {
        return this->size() == 0;
    }

    const T *get() const {
        return this->data;
    }

    void clear() {
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

    iterator begin() {
        return this->data;
    }

    iterator end() {
        return this->data + this->usedSize;
    }

    const_iterator begin() const {
        return this->data;
    }

    const_iterator end() const {
        return this->data + this->usedSize;
    }

    reference front() {
        return this->operator[](0);
    }

    const_reference front() const {
        return this->operator[](0);
    }

    reference back() {
        return this->operator[](this->usedSize - 1);
    }

    const_reference back() const {
        return this->operator[](this->usedSize - 1);
    }

    void pop_back() {
        this->usedSize--;
    }

    reference operator[](size_type index) {
        return this->data[index];
    }

    const_reference operator[](size_type index) const {
        return this->data[index];
    }

    reference at(size_type index);

    const_reference at(size_type index) const;

    /**
     * pos (begin() <= pos <= end()).
     * return position inserted element.
     */
    iterator insert(const_iterator pos, T value);

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

    /**
     * extract data. after call it, maxSize and usedSize is 0, and data is null.
     * call free() to release returned pointer.
     */
    static T *extract(FlexBuffer<T, SIZE_T> &&buf) noexcept {
        buf.maxSize = 0;
        buf.usedSize = 0;
        T *ptr = buf.data;
        buf.data = nullptr;
        return ptr;
    };
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
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(T value) {
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
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::insert(const_iterator pos, T value) {
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

typedef FlexBuffer<char> ByteBuffer;

} // namespace misc
} // namespace ydsh


#endif //YDSH_MISC_BUFFER_HPP
