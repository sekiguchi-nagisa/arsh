/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#ifndef MISC_LIB_INLINED_STACK_HPP
#define MISC_LIB_INLINED_STACK_HPP

#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>

#include "fatal.h"
#include "noncopyable.h"

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T, size_t N, typename SIZE_T = unsigned int>
class InlinedStack {
private:
  static_assert(std::is_default_constructible_v<T>);
  static_assert(N <= std::numeric_limits<SIZE_T>::max());

  SIZE_T usedSize{0};
  SIZE_T cap{0};
  T *ptr{nullptr};
  T data[N]; // for small data

public:
  using size_type = SIZE_T;

  NON_COPYABLE(InlinedStack);

  InlinedStack() { this->ptr = this->data; }

  ~InlinedStack() {
    if constexpr (std::is_trivially_destructible_v<T>) {
      if (!this->isStackAlloc()) {
        free(this->ptr);
      }
    } else {
      if (!this->isStackAlloc()) {
        delete[] this->ptr;
      }
    }
  }

  bool isStackAlloc() const { return this->ptr == this->data; }

  size_type size() const { return this->usedSize; }

  size_type capacity() const { return this->isStackAlloc() ? N : this->cap; }

  void reserve(size_type newCap) {
    if (this->capacity() >= newCap) {
      return;
    }
    T *newPtr = this->isStackAlloc() ? nullptr : this->ptr;
    if constexpr (std::is_trivially_copyable_v<T>) {
      newPtr = static_cast<T *>(realloc(newPtr, sizeof(T) * newCap));
      if (!newPtr) {
        fatal_perror("memory allocation failed");
      }
      if (this->isStackAlloc()) {
        memcpy(newPtr, this->data, sizeof(T) * this->size());
      }
    } else {
      newPtr = new T[newCap];
      for (size_t i = 0; i < this->size(); i++) {
        newPtr[i] = std::move(this->ptr[i]);
      }
      if (!this->isStackAlloc()) {
        delete[] this->ptr;
      }
    }
    this->ptr = newPtr;
    this->cap = newCap;
  }

  void push(T value) {
    if (this->size() == this->capacity()) {
      size_type newCap = this->capacity();
      newCap += newCap >> 1u;
      this->reserve(newCap);
    }
    if constexpr (std::is_trivially_move_assignable_v<T>) {
      this->ptr[this->usedSize++] = value;
    } else {
      this->ptr[this->usedSize++] = std::move(value);
    }
  }

  void pop() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      this->ptr[this->usedSize].~T();
    }
    --this->usedSize;
  }

  void clear() {
    while (this->size()) {
      this->pop();
    }
  }

  T &back() { return this->ptr[this->usedSize - 1]; }

  T &front() { return this->ptr[0]; }

  const T &back() const { return this->ptr[this->usedSize - 1]; }

  const T &front() const { return this->ptr[0]; }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_INLINED_STACK_HPP
