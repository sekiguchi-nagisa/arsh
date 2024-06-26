/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef MISC_LIB_RESULT_HPP
#define MISC_LIB_RESULT_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "noncopyable.h"

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T>
struct TypeHolder {
  using type = T;
};

namespace detail {

constexpr bool andAll(bool b) { return b; }

template <typename... T>
constexpr bool andAll(bool b, T &&...t) {
  return b && andAll(std::forward<T>(t)...);
}

template <typename T>
constexpr int toTypeIndex(int) {
  return -1;
}

template <typename T, typename F, typename... R>
constexpr int toTypeIndex(int index) {
  return std::is_same_v<T, F> ? index : toTypeIndex<T, R...>(index + 1);
}

template <std::size_t I, std::size_t N, typename F, typename... R>
struct TypeByIndex_ : TypeByIndex_<I + 1, N, R...> {};

template <std::size_t N, typename F, typename... R>
struct TypeByIndex_<N, N, F, R...> {
  using type = F;
};

template <typename... T>
struct OverloadResolver;

template <typename F, typename... T>
struct OverloadResolver<F, T...> : OverloadResolver<T...> {
  using OverloadResolver<T...>::operator();

  TypeHolder<F> operator()(F) const;
};

template <>
struct OverloadResolver<> {
  void operator()() const;
};

template <typename F, typename... T>
using resolvedType = typename std::invoke_result_t<OverloadResolver<T...>, F>::type;

template <int N>
struct index_holder {};

} // namespace detail

template <typename U, typename... T>
struct TypeTag {
  static constexpr int value = detail::toTypeIndex<U, T...>(0);
};

template <std::size_t N, typename T0, typename... T>
struct TypeByIndex {
  static_assert(N < sizeof...(T) + 1, "out of range");
  using type = typename detail::TypeByIndex_<0, N, T0, T...>::type;
};

// #####################
// ##     Storage     ##
// #####################

template <typename... T>
class Storage {
private:
  alignas(T...) unsigned char data_[std::max({sizeof(T)...})];

public:
  template <typename U, typename F = detail::resolvedType<U, T...>>
  void obtain(U &&value) {
    static_assert(TypeTag<F, T...>::value > -1, "invalid type");

    using Decayed = std::decay_t<F>;
    new (&this->data_) Decayed(std::forward<U>(value));
  }

  template <typename F>
  F *data() {
    static_assert(TypeTag<F, T...>::value > -1, "invalid type");
#ifdef __cpp_lib_launder
    return std::launder(reinterpret_cast<F *>(&this->data_));
#else
    return reinterpret_cast<F *>(&this->data_);
#endif
  }

  template <typename F>
  const F *data() const {
    static_assert(TypeTag<F, T...>::value > -1, "invalid type");
#ifdef __cpp_lib_launder
    return std::launder(reinterpret_cast<const F *>(&this->data_));
#else
    return reinterpret_cast<const F *>(&this->data_);
#endif
  }
};

template <typename T, typename... R>
inline T &get(Storage<R...> &storage) {
  static_assert(TypeTag<T, R...>::value > -1, "invalid type");
  return *storage.template data<T>();
}

template <typename T, typename... R>
inline const T &get(const Storage<R...> &storage) {
  static_assert(TypeTag<T, R...>::value > -1, "invalid type");
  return *storage.template data<T>();
}

template <typename T, typename... R>
inline void destroy(Storage<R...> &storage) {
  get<T>(storage).~T();
}

/**
 *
 * @tparam T
 * @tparam R
 * @param src
 * @param dest
 * must be uninitialized
 */
template <typename T, typename... R>
inline void move(Storage<R...> &src, Storage<R...> &dest) {
  dest.obtain(std::move(get<T>(src)));
}

template <typename T, typename... R>
inline void copy(const Storage<R...> &src, Storage<R...> &dest) {
  dest.obtain(get<T>(src));
}

namespace detail {

template <int N, typename... R>
inline void destroy(Storage<R...> &storage, int tag, index_holder<N>) {
  if constexpr (N == -1) {
  } // do nothing
  else if (tag == N) {
    using T = typename TypeByIndex<N, R...>::type;
    get<T>(storage).~T();
  } else {
    destroy(storage, tag, index_holder<N - 1>{});
  }
}

template <typename... R>
inline void destroy(Storage<R...> &storage, int tag) {
  destroy(storage, tag, index_holder<sizeof...(R) - 1>{});
}

template <int N, typename... R>
inline void move(Storage<R...> &src, int srcTag, index_holder<N>, Storage<R...> &dest) {
  if constexpr (N == -1) {
  } // do nothing
  else if (srcTag == N) {
    using T = typename TypeByIndex<N, R...>::type;
    dest.obtain(std::move(get<T>(src)));
  } else {
    move(src, srcTag, index_holder<N - 1>{}, dest);
  }
}

template <typename... R>
inline void move(Storage<R...> &src, int srcTag, Storage<R...> &dest) {
  move(src, srcTag, index_holder<sizeof...(R) - 1>{}, dest);
}

template <int N, typename... R>
inline void copy(const Storage<R...> &src, int srcTag, index_holder<N>, Storage<R...> &dest) {
  if constexpr (N == -1) {
  } // do nothing
  else if (srcTag == N) {
    using T = typename TypeByIndex<N, R...>::type;
    dest.obtain(get<T>(src));
  } else {
    copy(src, srcTag, index_holder<N - 1>{}, dest);
  }
}

template <typename... R>
inline void copy(const Storage<R...> &src, int srcTag, Storage<R...> &dest) {
  return copy(src, srcTag, index_holder<sizeof...(R) - 1>{}, dest);
}

} // namespace detail

// ###################
// ##     Union     ##
// ###################

template <typename... T>
class Union {
private:
  static_assert(detail::andAll(std::is_move_constructible_v<T>...), "must be move-constructible");

  using StorageType = Storage<T...>;
  StorageType value_;
  int tag_;

public:
  template <typename R>
  static constexpr int TAG = TypeTag<R, T...>::value;

  Union() noexcept : tag_(-1) {}

  template <typename U, typename F = detail::resolvedType<U, T...>>
  Union(U &&value) noexcept : tag_(TAG<F>) { // NOLINT
    this->value_.obtain(std::forward<U>(value));
  }

  Union(Union &&value) noexcept : tag_(value.tag()) {
    detail::move(value.value(), this->tag(), this->value());
    value.tag_ = -1;
  }

  Union(const Union &value) : tag_(value.tag()) {
    detail::copy(value.value(), this->tag(), this->value());
  }

  ~Union() { detail::destroy(this->value(), this->tag()); }

  Union &operator=(Union &&value) noexcept {
    if (this != std::addressof(value)) {
      this->moveAssign(std::move(value));
    }
    return *this;
  }

  Union &operator=(const Union &value) {
    if (this != std::addressof(value)) {
      this->copyAssign(value);
    }
    return *this;
  }

  StorageType &value() { return this->value_; }

  const StorageType &value() const { return this->value_; }

  int tag() const { return this->tag_; }

  bool hasValue() const { return this->tag() > -1; }

private:
  void moveAssign(Union &&value) noexcept {
    detail::destroy(this->value(), this->tag());
    detail::move(value.value(), value.tag(), this->value());
    this->tag_ = value.tag();
    value.tag_ = -1;
  }

  void copyAssign(const Union &value) {
    detail::destroy(this->value(), this->tag());
    detail::copy(value.value(), value.tag(), this->value());
    this->tag_ = value.tag();
  }
};

template <typename T, typename... R>
inline bool is(const Union<R...> &value) {
  using Tag = TypeTag<T, R...>;
  static_assert(Tag::value > -1, "invalid type");
  return value.tag() == Tag::value;
}

template <typename T, typename... R>
inline T &get(Union<R...> &value) {
  assert(is<T>(value));
  return get<T>(value.value());
}

template <typename T, typename... R>
inline const T &get(const Union<R...> &value) {
  assert(is<T>(value));
  return get<T>(value.value());
}

// ######################
// ##     Optional     ##
// ######################

template <typename T>
class OptionalBase : public Union<T> {
public:
  using base_type = Union<T>;

  using value_type = T;

  OptionalBase() noexcept : Union<T>() {}

  template <typename U>
  OptionalBase(U &&value) noexcept : Union<T>(std::forward<U>(value)) {} // NOLINT

  T &unwrap() noexcept { return get<T>(*this); }

  const T &unwrap() const noexcept { return get<T>(*this); }
};

template <typename... T>
class OptionalBase<Union<T...>> : public Union<T...> {
public:
  using base_type = Union<T...>;

  OptionalBase() noexcept : Union<T...>() {}

  template <typename U>
  OptionalBase(U &&value) noexcept : Union<T...>(std::forward<U>(value)) {} // NOLINT

  Union<T...> &unwrap() noexcept { return static_cast<base_type &>(*this); }

  const Union<T...> &unwrap() const noexcept { return static_cast<const base_type &>(*this); }
};

template <typename T>
struct OptFlattener {
  using type = T;
};

template <typename T>
struct OptFlattener<OptionalBase<T>> : OptFlattener<T> {};

template <typename T>
using Optional = OptionalBase<typename OptFlattener<T>::type>;

// ####################
// ##     Result     ##
// ####################

template <typename T>
struct OkHolder {
  T value;

  explicit OkHolder(T &&value) : value(std::move(value)) {}
  explicit OkHolder(const T &value) : value(value) {}
};

template <typename E>
struct ErrHolder {
  E value;

  explicit ErrHolder(E &&value) : value(std::move(value)) {}
  explicit ErrHolder(const E &value) : value(value) {}
};

template <typename T, typename Decayed = std::decay_t<T>>
OkHolder<Decayed> Ok(T &&value) {
  return OkHolder<Decayed>(std::forward<T>(value));
}

template <typename E, typename Decayed = std::decay_t<E>>
ErrHolder<Decayed> Err(E &&value) {
  return ErrHolder<Decayed>(std::forward<E>(value));
}

template <typename T, typename E>
class Result : public Union<T, E> {
public:
  NON_COPYABLE(Result);

  Result() = delete;

  template <typename T0>
  Result(OkHolder<T0> &&okHolder) noexcept : Union<T, E>(std::move(okHolder.value)) {} // NOLINT

  Result(ErrHolder<E> &&errHolder) noexcept : Union<T, E>(std::move(errHolder.value)) {} // NOLINT

  Result(Result &&result) noexcept = default;

  ~Result() = default;

  Result &operator=(Result &&result) noexcept = default;

  explicit operator bool() const { return is<T>(*this); }

  T &asOk() { return get<T>(*this); }

  E &asErr() { return get<E>(*this); }

  const T &asOk() const { return get<T>(*this); }

  const E &asErr() const { return get<E>(*this); }

  T &&take() && { return std::move(this->asOk()); }

  E &&takeError() && { return std::move(this->asErr()); }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_RESULT_HPP
