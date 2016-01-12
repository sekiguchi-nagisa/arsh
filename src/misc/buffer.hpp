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

#include "noncopyable.h"

namespace ydsh {
namespace misc {

namespace __detail_flex_buffer {

/**
 * only available primitive type or pointer type.
 */
template <typename T, typename SIZE_T>
class FlexBuffer {
public:
    typedef SIZE_T size_type;
    static const size_type MINIMUM_CAPACITY;
    static const size_type MAXIMUM_CAPACITY;

private:
    static_assert(std::is_unsigned<SIZE_T>::value, "need unsigned type");

    static_assert(std::is_enum<T>::value ||
                  std::is_arithmetic<T>::value ||
                  std::is_pointer<T>::value, "forbidden type");

    size_type maxSize;
    size_type usedSize;

    T *data;

public:
    NON_COPYABLE(FlexBuffer);

    /**
     * default initial size is equivalent to MINIMUM_CAPACITY
     */
    explicit FlexBuffer(size_type initSize) :
            maxSize(initSize < MINIMUM_CAPACITY ? MINIMUM_CAPACITY : initSize),
            usedSize(0), data(new T[this->maxSize]) { }

    /**
     * for lazy allocation
     */
    FlexBuffer() : maxSize(0), usedSize(0), data(nullptr) { }

    FlexBuffer(FlexBuffer<T, SIZE_T> &&buffer) :
            maxSize(buffer.maxSize), usedSize(buffer.usedSize), data(extract(std::move(buffer))) { }

    ~FlexBuffer() {
        delete[] this->data;
    }

    FlexBuffer<T, SIZE_T> &operator+=(T value);

    /**
     * buffer.data is not equivalent to this.data.
     */
    FlexBuffer<T, SIZE_T> &operator+=(const FlexBuffer<T, SIZE_T> &buffer);

    FlexBuffer<T, SIZE_T> &operator=(FlexBuffer<T, SIZE_T> &&buffer) noexcept ;
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

    const T *const get() const {
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

    T &operator[](size_type index) const noexcept {
        return this->data[index];
    }

    T &at(size_type index) const;

    /**
     * extract data. after call it, maxSize and usedSize is 0, and data is null.
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
};

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(FlexBuffer<T, SIZE_T> &&buffer) {
    FlexBuffer<T, SIZE_T> tmp(std::move(buffer));
    *this += tmp;
    return *this;
};

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
        T *newData = new T[this->maxSize];
        memcpy(newData, this->data, sizeof(T) * this->usedSize);
        delete[] this->data;
        this->data = newData;
    }
}

template <typename T, typename SIZE_T>
T &FlexBuffer<T, SIZE_T>::at(size_type index) const {
    if(index >= this->usedSize) {
        std::string str("size is: ");
        str += std::to_string(this->usedSize);
        str += ", but index is: ";
        str += std::to_string(index);
        throw std::out_of_range(str);
    }
    return this->data[index];
}

} // namespace __detail_flex_buffer

/**
 * maximum capacity is 4GB
 */
template <typename T>
using FlexBuffer = __detail_flex_buffer::FlexBuffer<T, unsigned int>;

typedef FlexBuffer<char> ByteBuffer;


} // namespace misc
} // namespace ydsh


#endif //YDSH_MISC_BUFFER_HPP
