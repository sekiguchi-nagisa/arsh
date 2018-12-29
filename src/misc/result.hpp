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

#ifndef YDSH_RESULT_HPP
#define YDSH_RESULT_HPP

#include <type_traits>

#include "noncopyable.h"

namespace ydsh {
namespace __detail {

template<typename T>
constexpr T max2(T x, T y) {
    return x > y ? x : y;
}

template<typename T>
constexpr T max(T t) {
    return t;
}

template<typename T, typename U, typename ...R>
constexpr T max(T t, U u, R... r) {
    return max2(t, max(u, std::forward<R>(r)...));
}

constexpr bool andAll(bool b) {
    return b;
}

template <typename ...T>
constexpr bool andAll(bool b, T && ...t) {
    return b && andAll(std::forward<T>(t)...);
}

template <typename T>
constexpr int toTypeIndex(int) {
    return -1;
}

template <typename T, typename F, typename ...R>
constexpr int toTypeIndex(int index) {
    return std::is_same<T, F>::value ? index : toTypeIndex<T, R...>(index + 1);
}

template <std::size_t I, std::size_t N, typename F, typename ...R>
struct TypeByIndex : TypeByIndex<I + 1, N, R...> {};

template <std::size_t N, typename F, typename ...R>
struct TypeByIndex<N, N, F, R...> {
    using type = F;
};

template <typename T>
struct typeHolder {
    using type = T;
};

template <typename ...T>
struct OverloadResolver;

template <typename F, typename ...T>
struct OverloadResolver<F, T...> : OverloadResolver<T...> {
    using OverloadResolver<T...>::operator();

    typeHolder<F> operator()(F) const;
};

template <>
struct OverloadResolver<> {
    void operator()() const;
};

template <typename T>
using result_of_t = typename std::result_of<T>::type;

template <typename F, typename ...T>
using resolvedType = typename result_of_t<OverloadResolver<T...>(F)>::type;

} // namespace __detail


template <typename U, typename ...T>
struct TypeTag {
    static constexpr int value = __detail::toTypeIndex<U, T...>(0);
};

template <std::size_t N, typename T0, typename ...T>
struct TypeByIndex {
    static_assert(N < sizeof...(T) + 1, "out of range");
    using type = typename __detail::TypeByIndex<0, N, T0, T...>::type;
};


// #####################
// ##     Storage     ##
// #####################

template <typename ...T>
struct Storage {
    static_assert(sizeof...(T) > 1, "at least 2 type");

    static constexpr auto size = __detail::max(sizeof(T)...);
    static constexpr auto align = __detail::max(alignof(T)...);

    using type = typename std::aligned_storage<size, align>::type;

    type data;

    template <typename U, typename F = __detail::resolvedType<U, T...>>
    void obtain(U &&value) {
        static_assert(TypeTag<F, T...>::value > -1, "invalid type");

        using Decayed = typename std::decay<F>::type;
        new (&this->data) Decayed(std::forward<U>(value));
    }
};

template <typename T, typename ...R>
inline T &get(Storage<R...> &storage) {
    static_assert(TypeTag<T, R...>::value > -1, "invalid type");

    return *reinterpret_cast<T *>(&storage.data);
}

template <typename T, typename ...R>
inline const T &get(const Storage<R...> &storage) {
    static_assert(TypeTag<T, R...>::value > -1, "invalid type");

    return *reinterpret_cast<const T *>(&storage.data);
}

template <typename T, typename ...R>
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
template <typename T, typename ...R>
inline void move(Storage<R...> &src, Storage<R...> &dest) {
    dest.obtain(std::move(get<T>(src)));
    destroy<T>(src);
}

namespace __detail_union {

template <int N, typename ...R>
struct Destroyer {
    void operator()(Storage<R...> &storage, int tag) const {
        if(tag == N) {
            using T = typename TypeByIndex<N, R...>::type;
            destroy<T>(storage);
        } else {
            Destroyer<N - 1, R...>()(storage, tag);
        }
    }
};

template <typename ...R>
struct Destroyer<-1, R...> {
    void operator()(Storage<R...> &, int) const {}
};


template <int N, typename ...R>
struct Mover {
    void operator()(Storage<R...> &src, int srcTag, Storage<R...> &dest) const {
        if(srcTag == N) {
            using T = typename TypeByIndex<N, R...>::type;
            move<T>(src, dest);
        } else {
            Mover<N - 1, R...>()(src, srcTag, dest);
        }
    }
};

template <typename ...R>
struct Mover<-1, R...> {
    void operator()(Storage<R...> &, int, Storage<R...> &) const {}
};

} // namespace __detail_union

template <typename ...R>
inline void polyDestroy(Storage<R...> &storage, int tag) {
    __detail_union::Destroyer<sizeof...(R) - 1, R...>()(storage, tag);
}


/**
 *
 * @tparam N
 * @tparam R
 * @param src
 * @param srcTag
 * @param dest
 * must be uninitialized
 */
template <typename ...R>
inline void polyMove(Storage<R...> &src, int srcTag, Storage<R...> &dest) {
    __detail_union::Mover<sizeof...(R) - 1, R...>()(src, srcTag, dest);
}


// ###################
// ##     Union     ##
// ###################

template <typename ...T>
class Union {
private:
    static_assert(__detail::andAll(std::is_move_constructible<T>::value...), "must be move-constructible");

    using StorageType = Storage<T...>;
    StorageType value_{};
    int tag_;

public:
    template <typename R>
    using Tag = TypeTag<R, T...>;

    NON_COPYABLE(Union);

    Union() noexcept : tag_(-1) {}

    template <typename U, typename F = __detail::resolvedType<U, T...>>
    Union(U &&value) noexcept : tag_(Tag<F>::value) {   //NOLINT
        this->value_.obtain(std::forward<U>(value));
    }

    Union(Union &&value) noexcept : tag_(value.tag()) {
        polyMove(value.value(), this->tag(), this->value());
        value.tag_ = -1;
    }

    ~Union() {
        polyDestroy(this->value(), this->tag());
    }

    Union &operator=(Union && value) noexcept {
        this->moveAssign(value);
        return *this;
    }

    StorageType &value() {
        return this->value_;
    }

    const StorageType &value() const {
        return this->value_;
    }

    int tag() const {
        return this->tag_;
    }

private:
    void moveAssign(Union &value) noexcept {
        polyDestroy(this->value(), this->tag());
        polyMove(value.value(), value.tag(), this->value());
        this->tag_ = value.tag();
        value.tag_ = -1;
    }
};

template <typename T, typename ...R>
inline bool is(const Union<R...> &value) {
    return value.tag() == TypeTag<T, R...>::value;
}

template <typename T, typename ...R>
inline T &get(Union<R...> &value) {
    return get<T>(value.value());
}

template <typename T, typename ...R>
inline const T &get(const Union<R...> &value) {
    return get<T>(value.value());
}


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

template <typename T, typename Decayed = typename std::decay<T>::type>
OkHolder<Decayed> Ok(T &&value) {
    return OkHolder<Decayed>(std::forward<T>(value));
}

template <typename E, typename Decayed = typename std::decay<E>::type>
ErrHolder<Decayed> Err(E &&value) {
    return ErrHolder<Decayed>(std::forward<E>(value));
}

template <typename T, typename E>
class Result : public Union<T, E> {
public:
    NON_COPYABLE(Result);

    Result() = delete;

    Result(OkHolder<T> &&okHolder) noexcept : Union<T, E>(std::move(okHolder.value)) {} //NOLINT

    Result(ErrHolder<E> &&errHolder) noexcept : Union<T, E>(std::move(errHolder.value)) {}  //NOLINT

    Result(Result &&result) noexcept = default;

    ~Result() = default;

    explicit operator bool() const {
        return is<T>(*this);
    }
    T &asOk() {
        return get<T>(*this);
    }

    E &asErr() {
        return get<E>(*this);
    }
};

} // namespace ydsh

#endif //YDSH_RESULT_HPP
