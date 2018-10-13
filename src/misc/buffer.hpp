/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include <cassert>
#include <initializer_list>

#include "noncopyable.h"
#include "fatal.h"

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

    static constexpr size_type MINIMUM_CAPACITY = 8;
    static constexpr size_type MAXIMUM_CAPACITY = static_cast<SIZE_T>(-1);

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
    static T *allocArray(T *old, SIZE_T size) noexcept {
        auto *ptr = static_cast<T *>(realloc(old, sizeof(T) * size));
        if(ptr == nullptr) {
            fatal("memory allocation failed\n");
        }
        return ptr;
    }

    void moveElements(iterator src, iterator dest) noexcept {
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
     * if out of range, abort
     */
    void checkRange(size_type index) const noexcept;

    FlexBuffer &push_back_impl(const T &value) noexcept;

public:
    NON_COPYABLE(FlexBuffer);

    /**
     * default initial size is equivalent to MINIMUM_CAPACITY
     */
    explicit FlexBuffer(size_type initSize) noexcept :
            maxSize(initSize < MINIMUM_CAPACITY ? MINIMUM_CAPACITY : initSize),
            usedSize(0),
            data(allocArray(nullptr, this->maxSize)) { }

    FlexBuffer(std::initializer_list<T> list) noexcept : FlexBuffer(list.size()) {
        for(auto iter = list.begin(); iter != list.end(); ++iter) {
            this->data[this->usedSize++] = *iter;
        }
    }

    /**
     * for lazy allocation
     */
    FlexBuffer() noexcept : maxSize(0), usedSize(0), data(nullptr) { }

    FlexBuffer(FlexBuffer &&buffer) noexcept :
            maxSize(buffer.maxSize), usedSize(buffer.usedSize), data(extract(std::move(buffer))) { }

    ~FlexBuffer() {
        free(this->data);
    }

    FlexBuffer &operator=(FlexBuffer &&buffer) noexcept {
        this->swap(buffer);
        return *this;
    }

    FlexBuffer &operator+=(const T &value) noexcept {
        return this->push_back_impl(value);
    }

    FlexBuffer &operator+=(T &&value) noexcept {
        return this->push_back_impl(value);
    }

    /**
     * buffer.data is not equivalent to this.data.
     */
    FlexBuffer &operator+=(const FlexBuffer &buffer) noexcept;

    FlexBuffer &operator+=(FlexBuffer &&buffer) noexcept;

    template <std::size_t N>
    FlexBuffer &operator+=(const T (&value)[N]) noexcept {
        return this->append(value, N);
    }

    /**
     * value is not equivalent to this.data.
     */
    FlexBuffer &append(const T *value, size_type size) noexcept;

    template <typename Func>
    FlexBuffer &appendBy(size_type maxSize, Func func) noexcept {
        this->reserve(this->size() + maxSize);
        size_type actualSize = func(this->get() + this->size());
        this->usedSize += actualSize;
        return *this;
    }

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

    T *get() noexcept {
        return this->data;
    }

    void clear() noexcept {
        this->usedSize = 0;
    }

    void swap(FlexBuffer &buf) noexcept {
        std::swap(this->usedSize, buf.usedSize);
        std::swap(this->maxSize, buf.maxSize);
        std::swap(this->data, buf.data);
    }

    /**
     * capacity will be at least reservingSize.
     */
    void reserve(size_type reservingSize) noexcept;

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

    void push_back(const T &value) noexcept {
        this->push_back_impl(value);
    }

    void push_back(T &&value) noexcept {
        this->push_back_impl(value);
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

    reference at(size_type index) noexcept;

    const_reference at(size_type index) const noexcept;

    /**
     * pos (begin() <= pos <= end()).
     * return position inserted element.
     */
    iterator insert(const_iterator pos, const T &value) noexcept;

    /**
     * pos must not equivalent to this->end().
     */
    iterator erase(const_iterator pos) noexcept;

    /**
     * first must be last or less. (first <= last).
     * last must be this->end() or less. (last <= this->end())
     * first is inclusive, last is exclusive.
     */
    iterator erase(const_iterator first, const_iterator last) noexcept;

    void assign(size_type n, const T &value) noexcept;

    bool operator==(const FlexBuffer &v) const noexcept {
        return this->size() == v.size() && memcmp(this->data, v.data, sizeof(T) * this->size()) == 0;
    }

    bool operator!=(const FlexBuffer &v) const noexcept {
        return !(*this == v);
    }

    /**
     * extract data. after call it, maxSize and usedSize is 0, and data is null.
     * call free() to release returned pointer.
     */
    friend T *extract(FlexBuffer &&buf) noexcept {
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
constexpr typename FlexBuffer<T, SIZE_T>::size_type FlexBuffer<T, SIZE_T>::MINIMUM_CAPACITY;

template <typename T, typename SIZE_T>
constexpr typename FlexBuffer<T, SIZE_T>::size_type FlexBuffer<T, SIZE_T>::MAXIMUM_CAPACITY;

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::checkRange(size_type index) const noexcept {
    if(index >= this->usedSize) {
        fatal("size is: %d, but index is: %d\n", this->usedSize, index);
    }
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::push_back_impl(const T &value) noexcept {
    this->reserve(this->usedSize + 1);
    this->data[this->usedSize++] = value;
    return *this;
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(const FlexBuffer<T, SIZE_T> &buffer) noexcept {
    return this->append(buffer.get(), buffer.size());
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(FlexBuffer<T, SIZE_T> &&buffer) noexcept {
    FlexBuffer<T, SIZE_T> tmp(std::move(buffer));
    *this += tmp;
    return *this;
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::append(const T *value, size_type size) noexcept {
    if(this->data == value) {
        fatal("appending own buffer\n");
    }
    this->reserve(this->usedSize + size);
    memcpy(this->data + this->usedSize, value, sizeof(T) * size);
    this->usedSize += size;
    return *this;
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::reserve(size_type reservingSize) noexcept {
    if(reservingSize > this->maxSize) {
        std::size_t newSize = (this->maxSize == 0 ? MINIMUM_CAPACITY : this->maxSize);
        while(newSize < reservingSize) {
            newSize += (newSize >> 1);
        }

        if(newSize > MAXIMUM_CAPACITY) {
            fatal("reach maximum capacity\n");
        }

        this->maxSize = newSize;
        this->data = allocArray(this->data, newSize);
    }
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::reference FlexBuffer<T, SIZE_T>::at(size_type index) noexcept {
    this->checkRange(index);
    return this->data[index];
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::const_reference FlexBuffer<T, SIZE_T>::at(size_type index) const noexcept {
    this->checkRange(index);
    return this->data[index];
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::insert(const_iterator pos, const T &value) noexcept {
    assert(pos >= this->begin() && pos <= this->end());

    const size_type index = pos - this->begin();
    this->reserve(this->size() + 1);
    iterator iter = this->begin() + index;

    this->moveElements(iter, iter + 1);
    this->data[index] = value;

    return iter;
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::erase(const_iterator pos) noexcept {
    assert(pos < this->end());

    const size_type index = pos - this->begin();
    iterator iter = this->begin() + index;

    this->moveElements(iter + 1, iter);

    return iter;
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator FlexBuffer<T, SIZE_T>::erase(const_iterator first, const_iterator last) noexcept {
    assert(last <= this->end());
    assert(first <= last);

    const size_type index = first - this->begin();
    iterator iter = this->begin() + index;

    this->moveElements(iter + (last - first), iter);

    return iter;
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::assign(size_type n, const T &value) noexcept {
    this->reserve(this->usedSize + n);
    for(unsigned int i = 0; i < n; i++) {
        this->data[this->usedSize++] = value;
    }
}

using ByteBuffer = FlexBuffer<char>;

// for byte reading
template <unsigned int N>
inline unsigned long readN(const unsigned char *ptr, unsigned int index) noexcept {
    static_assert(N > 0 && N < 9, "out of range");

    ptr += index;
    unsigned long v = 0;
    for(int i = N; i > 0; i--) {
        v |= static_cast<unsigned long>(*(ptr++)) << ((i - 1) * 8);
    }
    return v;
}

inline unsigned char read8(const unsigned char *const code, unsigned int index) noexcept {
    return readN<1>(code, index);
}

inline unsigned short read16(const unsigned char *const code, unsigned int index) noexcept {
    return readN<2>(code, index);
}

inline unsigned int read24(const unsigned char *const code, unsigned int index) noexcept {
    return readN<3>(code, index);
}

inline unsigned int read32(const unsigned char *const code, unsigned int index) noexcept {
    return readN<4>(code, index);
}

inline unsigned long read64(const unsigned char *const code, unsigned int index) noexcept {
    return readN<8>(code, index);
}

// for byte writing
template <unsigned int N>
inline void writeN(unsigned char *ptr, unsigned long b) noexcept {
    static_assert(N > 0 && N < 9, "out of range");

    for(unsigned int i = N; i > 0; --i) {
        const unsigned long mask = static_cast<unsigned long>(0xFF) << ((i - 1) * 8);
        *(ptr++) = (b & mask) >> ((i - 1) * 8);
    }
}

inline void write8(unsigned char *ptr, unsigned char b) noexcept {
    writeN<1>(ptr, b);
}

inline void write16(unsigned char *ptr, unsigned short b) noexcept {
    writeN<2>(ptr, b);
}

inline void write24(unsigned char *ptr, unsigned int b) noexcept {
    writeN<3>(ptr, b);
}

inline void write32(unsigned char *ptr, unsigned int b) noexcept {
    writeN<4>(ptr, b);
}

inline void write64(unsigned char *ptr, unsigned long b) noexcept {
    writeN<8>(ptr, b);
}

} // namespace ydsh


#endif //YDSH_MISC_BUFFER_HPP
