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

#ifndef MISC_LIB_ARRAY_REF_HPP
#define MISC_LIB_ARRAY_REF_HPP

#include <cstddef>

BEGIN_MISC_LIB_NAMESPACE_DECL

/*
 * similar to llvm::ArrayRef/std::span
 */
template <typename T>
class ArrayRef {
private:
  const T *ptr_{nullptr};
  size_t size_{0};

public:
  using value_type = T;

  constexpr ArrayRef() = default;

  template <size_t N>
  constexpr explicit ArrayRef(const T (&data)[N]) : ArrayRef(data, N) {}

  constexpr ArrayRef(const T *ptr, size_t size) : ptr_(ptr), size_(size) {}

  const T *begin() const { return this->ptr_; }

  const T *end() const { return this->ptr_ + this->size_; }

  constexpr size_t size() const { return this->size_; }

  constexpr bool empty() const { return this->size() == 0; }

  const T &operator[](size_t index) const { return this->ptr_[index]; }

  const T &front() const { return *this->begin(); }

  const T &back() const { return (*this)[this->size() - 1]; }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_ARRAY_REF_HPP
