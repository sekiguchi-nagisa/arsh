/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

template <typename T>
inline typename std::enable_if<std::is_const<T>::value, void>::type destroy(T *) {}   // do nothing

template <typename T>
inline typename std::enable_if<!std::is_const<T>::value, void>::type destroy(T *v) {
    delete v;
}

} // namespace __detail

template <typename T>
class OkHolder {
private:
    T *ptr;

public:
    NON_COPYABLE(OkHolder);

    OkHolder(T *ptr) : ptr(ptr) {}

    ~OkHolder() {
        __detail::destroy(this->ptr);
    }
};

template <typename E>
class ErrHolder {
private:
    E *ptr;

public:
    NON_COPYABLE(ErrHolder);

    ErrHolder(E *ptr) : ptr(ptr) {}

    ~ErrHolder() {
        __detail::destroy(this->ptr);
    }
};

class __Result {
protected:
    uintptr_t ptr;

    __Result(uintptr_t ptr) : ptr(ptr) {}
};

template <typename T, typename E>
class Result : public __Result {
private:
    enum Kind : unsigned char {
        OK = 0,
        ERR = 1,
    };

    friend class OkHolder<T>;
    friend class ErrHolder<E>;

    Result(uintptr_t ptr) : __Result(ptr) {}

public:
    NON_COPYABLE(Result);

    Result() noexcept : __Result(0) {}

    Result(std::nullptr_t) noexcept : Result() {}

    Result(Result &&v) noexcept : __Result(v.ptr) {
        v.ptr = nullptr;
    }

    template <typename U, typename F>
    Result(Result<U, F> &&v) noexcept : __Result(v.ptr) {};

    ~Result() {
        if(this->isErr()) {
            __detail::destroy(this->asErr());
        } else {
            __detail::destroy(this->asOk());
        }
    }

    Result &operator=(Result &&v) noexcept {
        auto tmp(std::move(v));
        std::swap(this->ptr, tmp.ptr);
        return *this;
    }

    template <typename U, typename F>
    Result &operator=(Result<U, F> &&v) noexcept {
        auto tmp(std::move(v));
        std::swap(this->ptr, tmp.ptr);
        return *this;
    }

    bool isNull() const {
        return this->ptr == 0;
    }

    bool isErr() const {
        return this->ptr & 1;
    }

    bool isOk() const {
        return !this->isErr();
    }

    T &ok() {
        return *this->asOk();
    }

    E &err() {
        return *this->asErr();
    }

    T *releaseAsOk() {
        T *ptr = this->asOk();
        this->ptr = nullptr;
        return ptr;
    }

    E *releaseAsErr() {
        E *ptr = this->asErr();
        this->ptr = nullptr;
        return ptr;
    }

private:
    T *asOk() {
        return reinterpret_cast<T *>(this->ptr);
    }

    E *asErr() {
        return reinterpret_cast<E *>(this->ptr & static_cast<uintptr_t>(-2));
    }
};

} // namespace ydsh

#endif //YDSH_RESULT_HPP
