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

#include <memory>

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T>
class RingBuffer {
private:
  unsigned int frontIndex_{0};
  unsigned int backIndex_{0};
  unsigned int allocSize_{0};
  std::unique_ptr<T[]> values_;

public:
  explicit RingBuffer(unsigned int cap)
      : allocSize_(adjustCap(cap)), values_(std::make_unique<T[]>(this->allocSize_)) {}

  RingBuffer() : RingBuffer(0) {}

  RingBuffer(RingBuffer &&o) noexcept
      : frontIndex_(o.frontIndex_), backIndex_(o.backIndex_), allocSize_(o.allocSize_),
        values_(std::move(o.values_)) {
    o.frontIndex_ = 0;
    o.backIndex_ = 0;
    o.allocSize_ = 0;
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

  unsigned int capacity() const { return this->allocSize_ - 1; }

  unsigned int size() const { return this->backIndex_ - this->frontIndex_; }

  bool empty() const { return this->size() == 0; }

  void push_back(T &&v) {
    this->values_[this->actualIndex(this->backIndex_)] = std::move(v);
    this->backIndex_++;
    if (this->size() == this->allocSize_) {
      this->pop_front();
    }
  }

  void pop_back() {
    this->backIndex_--;
    this->values_[this->actualIndex(this->backIndex_)].~T();
  }

  T &back() { return this->values_[this->actualIndex(this->backIndex_ - 1)]; }

  const T &back() const { return this->values_[this->actualIndex(this->backIndex_ - 1)]; }

  T &front() { return this->values_[this->actualIndex(this->frontIndex_)]; }

  const T &front() const { return this->values_[this->actualIndex(this->frontIndex_)]; }

  void pop_front() {
    this->values_[this->actualIndex(this->frontIndex_)].~T();
    this->frontIndex_++;
  }

private:
  static unsigned int adjustCap(unsigned int cap) {
    assert(cap < UINT32_MAX);
    if (cap == 0) {
      cap = 1;
    }
    unsigned int count = 0;
    for (; cap; count++) {
      cap = cap >> 1;
    }
    return 1 << count;
  }

  unsigned int actualIndex(unsigned int index) const { return index & this->capacity(); }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_RING_BUFFER_HPP
