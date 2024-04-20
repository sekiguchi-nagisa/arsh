/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#ifndef MISC_LIB_INLINED_ARRAY_HPP
#define MISC_LIB_INLINED_ARRAY_HPP

#include <memory>
#include <type_traits>

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T, size_t N>
class InlinedArray {
private:
  static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>, "forbidden type");

  struct Alloc : protected std::allocator<T> {
    const size_t size;

    explicit Alloc(size_t s) : size(s) {}

    T *alloc() { return this->allocate(this->size); }

    void dealloc(T *p) { this->deallocate(p, this->size); }
  };

  Alloc alloc_;
  T *ptr_;
  T data_[N];

public:
  explicit InlinedArray(size_t size) : alloc_(size) {
    this->ptr_ = this->data_;
    if (this->isAllocated()) {
      this->ptr_ = this->alloc_.alloc();
    }
    if constexpr (!std::is_trivial_v<T>) {
      for (size_t i = 0; i < this->size(); i++) {
        new (&this->ptr_[i]) T;
      }
    }
  }

  ~InlinedArray() {
    if (this->isAllocated()) {
      this->alloc_.dealloc(this->ptr_);
    }
  }

  size_t size() const { return this->alloc_.size; }

  T *ptr() { return this->ptr_; }

  const T &operator[](size_t i) const { return this->ptr_[i]; }

  T &operator[](size_t i) { return this->ptr_[i]; }

  bool isAllocated() const { return this->size() > std::size(this->data_); }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_INLINED_ARRAY_HPP
