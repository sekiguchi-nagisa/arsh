/*
 * Copyright (C) 2016-2020 Nagisa Sekiguchi
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

#ifndef MISC_LIB_RESOURCE_HPP
#define MISC_LIB_RESOURCE_HPP

#include <cerrno>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>

#include "noncopyable.h"

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T>
struct RefCountOp;

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
  static long useCount(const RefCount<T> *ptr) noexcept { return ptr->count; }

  static void increase(RefCount<T> *ptr) noexcept {
    if (ptr != nullptr) {
      ptr->count++;
    }
  }

  static void decrease(RefCount<T> *ptr) noexcept {
    if (ptr != nullptr && --ptr->count == 0) {
      delete static_cast<T *>(ptr);
    }
  }
};

template <typename T, typename P = RefCountOp<T>>
class IntrusivePtr final {
private:
  T *ptr;

public:
  constexpr IntrusivePtr() noexcept : ptr(nullptr) {}

  constexpr IntrusivePtr(std::nullptr_t) noexcept : ptr(nullptr) {} // NOLINT

  explicit IntrusivePtr(T *ptr) noexcept : ptr(ptr) { P::increase(this->ptr); }

  IntrusivePtr(const IntrusivePtr &v) noexcept : IntrusivePtr(v.ptr) {}

  IntrusivePtr(IntrusivePtr &&v) noexcept : ptr(v.ptr) { v.ptr = nullptr; }

  template <typename U>
  IntrusivePtr(const IntrusivePtr<U, P> &v) noexcept : IntrusivePtr(v.get()) {} // NOLINT

  template <typename U>
  IntrusivePtr(IntrusivePtr<U, P> &&v) noexcept : ptr(v.get()) { // NOLINT
    v.reset();
  }

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

  void swap(IntrusivePtr &o) noexcept { std::swap(this->ptr, o.ptr); }

  long useCount() const noexcept { return P::useCount(this->ptr); }

  T *get() const noexcept { return this->ptr; }

  T &operator*() const noexcept { return *this->ptr; }

  T *operator->() const noexcept { return this->ptr; }

  explicit operator bool() const noexcept { return this->ptr != nullptr; }

  bool operator==(const IntrusivePtr &obj) const noexcept { return this->get() == obj.get(); }

  bool operator!=(const IntrusivePtr &obj) const noexcept { return this->get() != obj.get(); }

  template <typename... A>
  static IntrusivePtr create(A &&...arg) {
    return IntrusivePtr(new T(std::forward<A>(arg)...));
  }
};

template <typename T>
class ObserverPtr {
private:
  T *ptr;

public:
  explicit ObserverPtr(T *ptr) noexcept : ptr(ptr) {}

  ObserverPtr(std::nullptr_t) noexcept : ptr(nullptr) {} // NOLINT

  ObserverPtr() noexcept : ptr(nullptr) {}

  explicit operator bool() const { return this->ptr != nullptr; }

  T *operator->() const { return this->ptr; }

  T &operator*() const { return *this->ptr; }

  template <typename... Arg>
  auto operator()(Arg &&...arg) const {
    return (*this->ptr)(std::forward<Arg>(arg)...);
  }

  void reset(T *p) { this->ptr = p; }
};

template <typename T>
inline ObserverPtr<T> makeObserver(T &v) {
  return ObserverPtr<T>(&v);
}

struct FileCloser {
  void operator()(FILE *fp) const {
    if (fp) {
      int old = errno;
      fclose(fp);
      errno = old; // ignore fclose error
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

/**
 * use 'static' modifier due to suppress 'noexcept-type' warning in gcc7
 */
template <typename Func, typename... Arg>
inline FilePtr createFilePtr(Func func, Arg &&...arg) {
  return FilePtr(func(std::forward<Arg>(arg)...));
}

template <typename Buf>
bool readAll(FILE *fp, Buf &buf) {
  while (true) {
    char data[128];
    clearerr(fp);
    errno = 0;
    unsigned int size = fread(data, sizeof(char), std::size(data), fp);
    if (size > 0) {
      buf.append(data, size);
    } else if (errno) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    } else {
      break;
    }
  }
  return true;
}

template <typename Buf>
inline bool readAll(const FilePtr &filePtr, Buf &buf) {
  return readAll(filePtr.get(), buf);
}

inline bool writeAll(const FilePtr &filePtr, const std::string &str) {
  return fwrite(str.c_str(), sizeof(char), str.size(), filePtr.get()) == str.size();
}

struct CStrDeleter {
  void operator()(char *ptr) const { free(ptr); }
};

using CStrPtr = std::unique_ptr<char, CStrDeleter>;

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

  explicit CallCounter(unsigned int &count) : count(count) { ++this->count; }

  ~CallCounter() { --this->count; }
};

template <typename Func>
class Finally {
private:
  Func func;

public:
  NON_COPYABLE(Finally);

  explicit Finally(Func &&func) : func(std::move(func)) {}

  Finally(Finally &&o) noexcept = default;

  ~Finally() { this->func(); }
};

template <typename Func>
inline auto finally(Func &&func) {
  return Finally<Func>(std::forward<Func>(func));
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_RESOURCE_HPP
