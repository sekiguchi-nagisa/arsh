/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef MISC_LIB_RING_BUFFER_HPP
#define MISC_LIB_RING_BUFFER_HPP

#include <algorithm>
#include <memory>

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T>
class RingBuffer {
private:
  struct Storage : protected std::allocator<T> {
    size_t size{0};
    T *ptr{nullptr};

    explicit Storage(size_t n) : size(n), ptr(this->allocate(n)) {}

    ~Storage() { this->deallocate(this->ptr, this->size); }
  };

  unsigned int frontIndex_{0};
  unsigned int backIndex_{0};
  Storage storage_;

public:
  static constexpr unsigned int alignAllocSize(unsigned int cap) {
    if (cap == 0) {
      cap = 1;
    }
    unsigned int count = 0;
    for (; cap; count++) {
      cap = cap >> 1;
    }
    return 1 << std::min(count, 31u);
  }

  explicit RingBuffer(unsigned int allocSize) : storage_(alignAllocSize(allocSize)) {}

  RingBuffer() : RingBuffer(0) {}

  RingBuffer(RingBuffer &&o) noexcept
      : frontIndex_(o.frontIndex_), backIndex_(o.backIndex_), storage_(std::move(o.storage_)) {
    o.frontIndex_ = 0;
    o.backIndex_ = 0;
    o.storage_.size = 0;
    o.storage_.ptr = nullptr;
  }

  ~RingBuffer() {
    while (!this->empty()) {
      this->pop_back();
    }
  }

  RingBuffer &operator=(RingBuffer &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~RingBuffer();
      new (this) RingBuffer(std::move(o));
    }
    return *this;
  }

  unsigned int allocSize() const { return static_cast<unsigned int>(this->storage_.size); }

  unsigned int capacity() const { return this->allocSize() - 1; }

  unsigned int size() const { return this->backIndex_ - this->frontIndex_; }

  bool empty() const { return this->size() == 0; }

  template <typename... Args>
  void emplace_back(Args &&...args) {
    new (&this->values()[this->actualIndex(this->backIndex_)]) T(std::forward<Args>(args)...);
    this->backIndex_++;
    if (this->size() == this->allocSize()) {
      this->pop_front();
    }
  }

  void push_back(T &&v) { this->emplace_back(std::move(v)); }

  void pop_back() {
    this->backIndex_--;
    this->values()[this->actualIndex(this->backIndex_)].~T();
  }

  T &back() { return this->values()[this->actualIndex(this->backIndex_ - 1)]; }

  const T &back() const { return this->values()[this->actualIndex(this->backIndex_ - 1)]; }

  T &front() { return this->values()[this->actualIndex(this->frontIndex_)]; }

  const T &front() const { return this->values()[this->actualIndex(this->frontIndex_)]; }

  void pop_front() {
    this->values()[this->actualIndex(this->frontIndex_)].~T();
    this->frontIndex_++;
  }

  T &operator[](unsigned int index) {
    return this->values()[this->actualIndex(this->frontIndex_ + index)];
  }

  const T &operator[](unsigned int index) const {
    return this->values()[this->actualIndex(this->frontIndex_ + index)];
  }

private:
  T *values() { return this->storage_.ptr; }

  const T *values() const { return this->storage_.ptr; }

  unsigned int actualIndex(unsigned int index) const { return index & this->capacity(); }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_RING_BUFFER_HPP
