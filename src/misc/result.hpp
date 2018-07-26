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

}   // namespace __detail

template <typename ...T>
struct Storage {
    static_assert(sizeof...(T) > 1, "atleast 2 type");

    static constexpr auto size = __detail::max(sizeof(T)...);
    static constexpr auto align = __detail::max(alignof(T)...);

    using type = typename std::aligned_storage<size, align>::type;

    type data;

    template <typename U>
    void construct(U &&value) {
        using Decayed = typename std::decay<U>::type;
        new (&this->data) Decayed(std::move(value));
    }

    template <typename U>
    void destroy() {
        this->get<U>().~U();
    }

    template <typename U>
    U &get() {
        return *reinterpret_cast<U *>(&this->data);
    }

    template <typename U>
    static void move(Storage &src, Storage &dest) {
        dest.construct(std::move(src.get<U>()));
        src.destroy<U>();
    }
};

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
class Result {
private:
    static_assert(std::is_move_constructible<T>::value, "must be move-constructible");
    static_assert(std::is_move_constructible<E>::value, "must be move-constructible");

    using StorageType = Storage<T, E>;
    StorageType value;
    bool ok;

public:
    NON_COPYABLE(Result);

    Result(OkHolder<T> &&okHolder) : ok(true) {
        this->value.construct(std::move(okHolder.value));
    }

    Result(ErrHolder<E> &&errHolder) : ok(false) {
        this->value.construct(std::move(errHolder.value));
    }

    Result(Result &&result) noexcept : ok(result.ok) {
        if(this->ok) {
            StorageType::template move<T>(result.value, this->value);
        } else {
            StorageType::template move<E>(result.value, this->value);
        }
    }

    ~Result() {
        if(this->ok) {
            this->value.template destroy<T>();
        } else {
            this->value.template destroy<E>();
        }
    }

    explicit operator bool() const {
        return this->ok;
    }

    T &asOk() {
        return this->value.template get<T>();
    }

    E &asErr() {
        return this->value.template get<E>();
    }
};

} // namespace ydsh

#endif //YDSH_RESULT_HPP
