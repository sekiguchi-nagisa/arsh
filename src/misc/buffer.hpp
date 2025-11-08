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

#ifndef MISC_LIB_BUFFER_HPP
#define MISC_LIB_BUFFER_HPP

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <utility>

#include "fatal.h"
#include "noncopyable.h"

BEGIN_MISC_LIB_NAMESPACE_DECL

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

  static constexpr size_type MAX_SIZE = static_cast<size_type>(-1);

private:
  static_assert(std::is_unsigned_v<SIZE_T>, "need unsigned type");

  static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>, "forbidden type");

  size_type cap_;
  size_type size_;

  T *data_;

  void moveElements(iterator src, iterator dest) noexcept {
    if (src == dest) {
      return;
    }

    memmove(dest, src, sizeof(T) * (this->end() - src));
    if (src < dest) {
      this->size_ += (dest - src);
    } else {
      this->size_ -= (src - dest);
    }
  }

  /**
   * if out of range, abort
   */
  void checkRange(size_type index) const noexcept;

  FlexBuffer &push_back_impl(const T &value) noexcept {
    this->insert(this->end(), value);
    return *this;
  }

  void needMoreCap(size_type addSize) noexcept;

public:
  NON_COPYABLE(FlexBuffer);

  explicit FlexBuffer(size_type initSize) noexcept : FlexBuffer() { this->reserve(initSize); }

  FlexBuffer(std::initializer_list<T> list) noexcept : FlexBuffer(list.size()) {
    for (auto iter = list.begin(); iter != list.end(); ++iter) {
      this->data_[this->size_++] = *iter;
    }
  }

  FlexBuffer(size_type n, const T &value) noexcept : FlexBuffer(n) {
    while (this->size_ < n) {
      this->data_[this->size_++] = value;
    }
  }

  template <size_type N>
  FlexBuffer(const T (&value)[N]) noexcept : FlexBuffer(N) { // NOLINT
    this->append(value, N);
  }

  FlexBuffer(const T *begin, const T *end) noexcept : FlexBuffer(end - begin) {
    for (; begin != end; ++begin) {
      this->data_[this->size_++] = *begin;
    }
  }

  /**
   * for lazy allocation
   */
  FlexBuffer() noexcept : cap_(0), size_(0), data_(nullptr) {}

  FlexBuffer(FlexBuffer &&buffer) noexcept
      : cap_(buffer.cap_), size_(buffer.size_), data_(buffer.take()) {}

  ~FlexBuffer() { free(this->data_); }

  FlexBuffer &operator=(FlexBuffer &&buffer) noexcept {
    if (this != std::addressof(buffer)) {
      this->~FlexBuffer();
      new (this) FlexBuffer(std::move(buffer));
    }
    return *this;
  }

  FlexBuffer &operator+=(const T &value) noexcept { return this->push_back_impl(value); }

  FlexBuffer &operator+=(T &&value) noexcept { return this->push_back_impl(value); }

  /**
   * buffer.data is not equivalent to this.data.
   */
  FlexBuffer &operator+=(const FlexBuffer &buffer) noexcept;

  FlexBuffer &operator+=(FlexBuffer &&buffer) noexcept;

  template <size_type N>
  FlexBuffer &operator+=(const T (&value)[N]) noexcept {
    return this->append(value, N);
  }

  /**
   * value is not equivalent to this.data.
   */
  FlexBuffer &append(const T *value, size_type size) noexcept;

  size_type capacity() const noexcept { return this->cap_; }

  size_type size() const noexcept { return this->size_; }

  size_type max_size() const noexcept { return MAX_SIZE; }

  bool empty() const noexcept { return this->size() == 0; }

  const T *data() const noexcept { return this->data_; }

  T *data() noexcept { return this->data_; }

  void clear() noexcept { this->size_ = 0; }

  void swap(FlexBuffer &buf) noexcept {
    std::swap(this->size_, buf.size_);
    std::swap(this->cap_, buf.cap_);
    std::swap(this->data_, buf.data_);
  }

  /**
   * capacity will be at least reservingSize.
   */
  void reserve(size_type reservingSize) noexcept;

  void resize(size_type afterSize, T v) noexcept;

  void resize(size_type afterSize) noexcept { this->resize(afterSize, T{}); }

  iterator begin() noexcept { return this->data_; }

  iterator end() noexcept { return this->data_ + this->size_; }

  const_iterator begin() const noexcept { return this->data_; }

  const_iterator end() const noexcept { return this->data_ + this->size_; }

  reference front() noexcept { return this->operator[](0); }

  const_reference front() const noexcept { return this->operator[](0); }

  reference back() noexcept { return this->operator[](this->size_ - 1); }

  const_reference back() const noexcept { return this->operator[](this->size_ - 1); }

  void push_back(const T &value) noexcept { this->push_back_impl(value); }

  void push_back(T &&value) noexcept { this->push_back_impl(value); }

  void pop_back() noexcept { --this->size_; }

  reference operator[](size_type index) noexcept {
    assert(index < this->size());
    return this->data_[index];
  }

  const_reference operator[](size_type index) const noexcept {
    assert(index < this->size());
    return this->data_[index];
  }

  reference at(size_type index) noexcept;

  const_reference at(size_type index) const noexcept;

  /**
   * pos (begin() <= pos <= end()).
   * return position inserted element.
   */
  iterator insert(const_iterator pos, const T &value) noexcept {
    return this->insert(pos, 1, value);
  }

  iterator insert(const_iterator pos, size_type count, const T &value) noexcept;

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

  bool operator==(const FlexBuffer &v) const noexcept {
    return this->size() == v.size() && memcmp(this->data_, v.data_, sizeof(T) * this->size()) == 0;
  }

  bool operator!=(const FlexBuffer &v) const noexcept { return !(*this == v); }

  /**
   * after called it, maxSize and usedSize is 0, and data is null.
   * call free() to release returned pointer.
   * @return
   */
  T *take() noexcept {
    this->cap_ = 0;
    this->size_ = 0;
    auto *ptr = this->data_;
    this->data_ = nullptr;
    return ptr;
  }
};

// ########################
// ##     FlexBuffer     ##
// ########################

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::checkRange(size_type index) const noexcept {
  if (index >= this->size_) {
    fatal("size is: %d, but index is: %d\n", this->size_, index);
  }
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(const FlexBuffer &buffer) noexcept {
  return this->append(buffer.data(), buffer.size());
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::operator+=(FlexBuffer &&buffer) noexcept {
  if (this != std::addressof(buffer)) {
    const auto &tmp = buffer;
    *this += tmp;
  }
  return *this;
}

template <typename T, typename SIZE_T>
FlexBuffer<T, SIZE_T> &FlexBuffer<T, SIZE_T>::append(const T *value, size_type size) noexcept {
  if (this->data_ != value) {
    this->needMoreCap(size);
    memcpy(this->data_ + this->size_, value, sizeof(T) * size);
    this->size_ += size;
  }
  return *this;
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::reserve(size_type reservingSize) noexcept {
  reservingSize = std::min(reservingSize, this->max_size());
  if (reservingSize > this->cap_) {
    this->cap_ = reservingSize;
    this->data_ = static_cast<T *>(realloc(this->data(), sizeof(T) * this->capacity()));
    if (!this->data_) {
      fatal_perror("memory allocation failed");
    }
  }
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::needMoreCap(size_type addSize) noexcept {
  if (this->size() > this->max_size() - addSize) {
    fatal("reach max size\n");
  }
  size_type newCap = this->size() + addSize;
  if (newCap > this->capacity()) {
    size_type delta = newCap >> 1u;
    if (newCap <= this->max_size() - delta) {
      newCap += delta;
    }
    this->reserve(newCap);
  }
}

template <typename T, typename SIZE_T>
void FlexBuffer<T, SIZE_T>::resize(size_type afterSize, T v) noexcept {
  if (afterSize <= this->size()) {
    this->size_ = afterSize;
  } else {
    this->needMoreCap(afterSize - this->size());
    while (this->size_ < afterSize) {
      this->data_[this->size_++] = v;
    }
  }
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::reference FlexBuffer<T, SIZE_T>::at(size_type index) noexcept {
  this->checkRange(index);
  return this->data_[index];
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::const_reference
FlexBuffer<T, SIZE_T>::at(size_type index) const noexcept {
  this->checkRange(index);
  return this->data_[index];
}

template <typename T, typename SIZE_T>
typename FlexBuffer<T, SIZE_T>::iterator
FlexBuffer<T, SIZE_T>::insert(const_iterator pos, size_type count, const T &value) noexcept {
  assert(pos >= this->begin() && pos <= this->end());

  const size_type index = pos - this->begin();
  this->needMoreCap(count);
  iterator iter = this->begin() + index;

  this->moveElements(iter, iter + count);
  for (size_type i = 0; i < count; i++) {
    this->data_[index + i] = value;
  }
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
typename FlexBuffer<T, SIZE_T>::iterator
FlexBuffer<T, SIZE_T>::erase(const_iterator first, const_iterator last) noexcept {
  assert(last <= this->end());
  assert(first <= last);

  const size_type index = first - this->begin();
  iterator iter = this->begin() + index;

  this->moveElements(iter + (last - first), iter);

  return iter;
}

using ByteBuffer = FlexBuffer<char>;

// for byte reading
template <unsigned int N>
inline uint64_t readN(const unsigned char *ptr) noexcept {
  static_assert(N > 0 && N < 9, "out of range");

  uint64_t v = 0;
  for (int i = N; i > 0; i--) {
    v |= static_cast<uint64_t>(*(ptr++)) << (static_cast<unsigned int>(i - 1) * 8);
  }
  return v;
}

template <unsigned int N>
inline uint64_t consumeN(const unsigned char *&ptr) noexcept {
  static_assert(N > 0 && N < 9, "out of range");

  uint64_t v = 0;
  for (int i = N; i > 0; i--) {
    v |= static_cast<uint64_t>(*(ptr++)) << (static_cast<unsigned int>(i - 1) * 8);
  }
  return v;
}

template <unsigned int N>
inline uint64_t readN(const unsigned char *ptr, unsigned int index) noexcept {
  return readN<N>(ptr + index);
}

inline unsigned char read8(const unsigned char *const code, unsigned int index) noexcept {
  return readN<1>(code, index);
}

inline unsigned char read8(const unsigned char *const ptr) noexcept { return readN<1>(ptr); }

inline unsigned char consume8(const unsigned char *&ptr) noexcept { return consumeN<1>(ptr); }

inline unsigned short read16(const unsigned char *const code, unsigned int index) noexcept {
  return readN<2>(code, index);
}

inline unsigned short read16(const unsigned char *const ptr) noexcept { return readN<2>(ptr); }

inline unsigned short consume16(const unsigned char *&ptr) noexcept { return consumeN<2>(ptr); }

inline unsigned int read24(const unsigned char *const code, unsigned int index) noexcept {
  return readN<3>(code, index);
}

inline unsigned int consume24(const unsigned char *&ptr) noexcept { return consumeN<3>(ptr); }

inline unsigned int read32(const unsigned char *const code, unsigned int index) noexcept {
  return readN<4>(code, index);
}

inline unsigned int read32(const unsigned char *const ptr) noexcept { return readN<4>(ptr); }

inline unsigned int consume32(const unsigned char *&ptr) noexcept { return consumeN<4>(ptr); }

inline uint64_t read64(const unsigned char *const code, unsigned int index) noexcept {
  return readN<8>(code, index);
}

// for byte writing
template <unsigned int N>
inline void writeN(unsigned char *ptr, uint64_t b) noexcept {
  static_assert(N > 0 && N < 9, "out of range");

  for (unsigned int i = N; i > 0; --i) {
    const uint64_t mask = static_cast<uint64_t>(0xFF) << ((i - 1) * 8);
    *(ptr++) = (b & mask) >> ((i - 1) * 8);
  }
}

inline void write8(unsigned char *ptr, unsigned char b) noexcept { writeN<1>(ptr, b); }

inline void write16(unsigned char *ptr, unsigned short b) noexcept { writeN<2>(ptr, b); }

inline void write24(unsigned char *ptr, unsigned int b) noexcept { writeN<3>(ptr, b); }

inline void write32(unsigned char *ptr, unsigned int b) noexcept { writeN<4>(ptr, b); }

inline void write64(unsigned char *ptr, uint64_t b) noexcept { writeN<8>(ptr, b); }

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_BUFFER_HPP
