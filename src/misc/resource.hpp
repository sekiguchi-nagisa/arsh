/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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

#include <cstdio>
#include <type_traits>
#include <memory>

#include "noncopyable.h"

namespace ydsh {

template <typename T> struct RefCountOp;

template <typename T>
class RefCount {
private:
    long count{0};
    friend struct RefCountOp<T>;

protected:
    RefCount() = default;
};

template <typename T>
struct RefCountOp final {
    static long useCount(const RefCount<T> *ptr) noexcept {
        return ptr->count;
    }

    static void increase(RefCount<T> *ptr) noexcept {
        if(ptr != nullptr) {
            ptr->count++;
        }
    }

    static void decrease(RefCount<T> *ptr) noexcept {
        if(ptr != nullptr && --ptr->count == 0) {
            delete static_cast<T *>(ptr);
        }
    }
};

template <typename T, typename P = RefCountOp<T>>
class IntrusivePtr final {
private:
    T *ptr;

public:
    constexpr IntrusivePtr() noexcept : ptr(nullptr) { }

    constexpr IntrusivePtr(std::nullptr_t) noexcept : ptr(nullptr) { }  //NOLINT

    explicit IntrusivePtr(T *ptr) noexcept : ptr(ptr) { P::increase(this->ptr); }

    IntrusivePtr(const IntrusivePtr &v) noexcept : IntrusivePtr(v.ptr) { }

    IntrusivePtr(IntrusivePtr &&v) noexcept : ptr(v.ptr) { v.ptr = nullptr; }

    template <typename U>
    IntrusivePtr(const IntrusivePtr<U, P> &v) noexcept : IntrusivePtr(v.get()) { }  //NOLINT

    template <typename U>
    IntrusivePtr(IntrusivePtr<U, P> &&v) noexcept : ptr(v.get()) { v.reset(); } //NOLINT

    ~IntrusivePtr() { P::decrease(this->ptr); }

    IntrusivePtr &operator=(const IntrusivePtr &v) noexcept {
        IntrusivePtr tmp(v);
        this->swap(tmp);
        return *this;
    }

    IntrusivePtr &operator=(IntrusivePtr &&v) noexcept {
        this->swap(v);
        return *this;
    }

    void reset() noexcept {
        IntrusivePtr tmp;
        this->swap(tmp);
    }

    void swap(IntrusivePtr &o) noexcept {
        std::swap(this->ptr, o.ptr);
    }

    long useCount() const noexcept {
        return P::useCount(this->ptr);
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

    bool operator==(const IntrusivePtr &obj) const noexcept {
        return this->get() == obj.get();
    }

    bool operator!=(const IntrusivePtr &obj) const noexcept {
        return this->get() != obj.get();
    }

    template <typename ... A>
    static IntrusivePtr create(A && ...arg) {
        return IntrusivePtr(new T(std::forward<A>(arg)...));
    }
};

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
            resource(std::move(o.resource)), deleter(std::move(o.deleter)), deleteResource(o.deleteResource) {
        o.release();
    }

    ~ScopedResource() noexcept {
        this->reset();
    }

    ScopedResource &operator=(ScopedResource &&o) noexcept {
        this->reset();
        this->resource = std::move(o.resource);
        this->deleter = std::move(o.deleter);
        this->deleteResource = o.deleteResource;
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
            this->getDeleter()(this->resource);
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
    return ScopedResource<R, ActualD>(std::forward<R>(r), std::forward<ActualD>(d));
}

struct FileCloser {
    void operator()(FILE *fp) const {
        if(fp) {
            fclose(fp);
        }
    }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

template <typename Func, typename ...Arg>
FilePtr createFilePtr(Func func, Arg &&...arg) {
    return FilePtr(func(std::forward<Arg>(arg)...));
}

template <typename T>
class Singleton {
protected:
    Singleton() = default;

public:
    NON_COPYABLE(Singleton);

    static T &instance() {
        static T value;
        return value;
    }
};

struct CallCounter {
    unsigned int &count;

    explicit CallCounter(unsigned int &count) : count(count) {
        ++this->count;
    }

    ~CallCounter() {
        --this->count;
    }
};

} // namespace ydsh

#endif //YDSH_MISC_RESOURCE_HPP
