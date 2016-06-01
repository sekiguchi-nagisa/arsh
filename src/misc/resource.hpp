/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_RESOURCE_HPP
#define YDSH_MISC_RESOURCE_HPP

#include <type_traits>

#include "noncopyable.h"

namespace ydsh {
namespace misc {

template <typename T>
class IntrusivePtr final {
private:
    T *ptr;

public:
    IntrusivePtr() noexcept : ptr(nullptr) { }

    IntrusivePtr(std::nullptr_t) noexcept : ptr(nullptr) { }

    IntrusivePtr(T *ptr) noexcept : ptr(ptr) { intrusivePtr_addRef(this->ptr); }

    IntrusivePtr(const IntrusivePtr &v) noexcept : IntrusivePtr(v.ptr) { }

    IntrusivePtr(IntrusivePtr &&v) noexcept : ptr(v.ptr) { v.ptr = nullptr; }

    ~IntrusivePtr() { intrusivePtr_release(this->ptr); }

    IntrusivePtr &operator=(const IntrusivePtr &v) noexcept {
        IntrusivePtr tmp(v);
        std::swap(this->ptr, tmp.ptr);
        return *this;
    }

    IntrusivePtr &operator=(IntrusivePtr &&v) noexcept {
        IntrusivePtr tmp(std::move(v));
        std::swap(this->ptr, tmp.ptr);
        return *this;
    }

    void reset() noexcept {
        IntrusivePtr tmp;
        std::swap(this->ptr, tmp.ptr);
    }

    T *get() const noexcept {
        return this->ptr;
    }

    T &operator*() const noexcept {
        return *this->ptr;
    }

    T *operator->() const noexcept {
        return this->ptr;
    }

    explicit operator bool() const noexcept {
        return this->ptr != nullptr;
    }
};

template <typename T, typename ... A>
inline IntrusivePtr<T> makeIntrusive(A ... arg) {
    return IntrusivePtr<T>(new T(std::forward<A>(arg)...));
}

template <typename R, typename D>
class ScopedResource {
private:
    R resource;
    D deleter;
    bool deleteResource;

public:
    NON_COPYABLE(ScopedResource);

    ScopedResource(R &&resource, D &&deleter) noexcept :
            resource(std::move(resource)), deleter(std::move(deleter)), deleteResource(true) { }

    ScopedResource(ScopedResource &&o) noexcept :
            resource(std::move(o.resource)), deleter(std::move(o.deleter)), deleteResource(true) {
        o.release();
    }

    ~ScopedResource() noexcept {
        this->reset();
    }

    ScopedResource &operator=(ScopedResource &&o) noexcept {
        this->reset();
        this->resource = std::move(o.resource);
        this->deleter = std::move(o.deleter);
        this->deleteResource = true;
        o.release();
        return *this;
    }


    R const &get() const noexcept {
        return this->resource;
    }

    D const &getDeleter() const noexcept {
        return this->deleter;
    }

    void reset() noexcept {
        if(this->deleteResource) {
            this->deleteResource = false;
            this->deleter(this->resource);
        }
    }

    void reset(R &&r) noexcept {
        this->reset();
        this->resource = std::move(r);
        this->deleteResource = true;
    }

    R const &release() noexcept {
        this->deleteResource = false;
        return this->get();
    }
};

template <typename R, typename D>
ScopedResource<R, typename std::remove_reference<D>::type> makeScopedResource(R &&r, D &&d) {
    using ActualD = typename std::remove_reference<D>::type;
    return ScopedResource<R, ActualD>(std::move(r), std::forward<ActualD>(d));
};

} // namespace misc
} // namespace ydsh

#endif //YDSH_MISC_RESOURCE_HPP
