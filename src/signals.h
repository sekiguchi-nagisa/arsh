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

#ifndef ARSH_SIGNAL_LIST_H
#define ARSH_SIGNAL_LIST_H

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <vector>

#include "misc/string_ref.hpp"

namespace arsh {

class SignalEntry {
public:
  static constexpr unsigned int MAX_ABBR_NAME = 10;

  using NameBuf = std::array<char, MAX_ABBR_NAME + 1>;

  enum class Kind : unsigned char {
    POSIX_1_1990,
    POSIX_1_2001,
    OTHER,
    REAL_TIME,
  };

private:
  NameBuf name{}; // abbreviate signal name (INT).
  Kind kind{};
  int sigNum{-1};

public:
  constexpr SignalEntry() = default;

  constexpr SignalEntry(NameBuf name, Kind kind, int sigNum)
      : name(name), kind(kind), sigNum(sigNum) {}

  const char *getAbbrName() const { return this->name.data(); }

  int getSigNum() const { return this->sigNum; }

  Kind getKind() const { return this->kind; }

  bool isRealTime() const { return this->getKind() == Kind::REAL_TIME; }

  explicit operator bool() const { return this->getSigNum() > 0; }

  std::string toFullName() const {
    std::string value = "SIG";
    value += this->getAbbrName();
    return value;
  }
};

class SignalEntryRange {
private:
  const SignalEntry *ptr_{nullptr};
  size_t size_{0};

public:
  SignalEntryRange(const SignalEntry *ptr, size_t size) : ptr_(ptr), size_(size) {}

  const SignalEntry *begin() const { return this->ptr_; }

  const SignalEntry *end() const { return this->ptr_ + this->size_; }

  size_t size() const { return this->size_; }

  bool empty() const { return this->size() == 0; }

  const SignalEntry &front() const { return *this->begin(); }

  const SignalEntry &back() const { return this->ptr_[this->size() - 1]; }
};

SignalEntryRange getStandardSignalEntries();

SignalEntryRange getRealTimeSignalEntries();

const SignalEntry *findSignalEntryByName(StringRef name);

const SignalEntry *findSignalEntryByNum(int sigNum);

std::vector<SignalEntry> toSortedUniqueSignalEntries();

class SignalGuard {
private:
  sigset_t maskset;

public:
  SignalGuard() {
    sigfillset(&this->maskset);
    sigprocmask(SIG_BLOCK, &this->maskset, nullptr);
  }

  ~SignalGuard() {
    int e = errno;
    sigprocmask(SIG_UNBLOCK, &this->maskset, nullptr);
    errno = e;
  }
};

class AtomicSigSet {
private:
  std::atomic_uint64_t set{0};
  int pendingIndex{1};

  using underlying_t = uint64_t;

  static_assert(NSIG - 1 <= sizeof(set) * 8, "huge signal number");
  static_assert(decltype(set)::is_always_lock_free);

public:
  AtomicSigSet() = default;

  AtomicSigSet(AtomicSigSet &&o) noexcept : set(o.value()), pendingIndex(o.pendingIndex) {}

  void add(int sigNum) {
    const underlying_t v = static_cast<underlying_t>(1) << static_cast<underlying_t>(sigNum - 1);
    this->set.fetch_or(v);
  }

  void del(int sigNum) {
    const underlying_t v = static_cast<underlying_t>(1) << static_cast<underlying_t>(sigNum - 1);
    this->set.fetch_and(~v);
  }

  bool has(int sigNum) const {
    const underlying_t v = static_cast<underlying_t>(1) << static_cast<underlying_t>(sigNum - 1);
    return this->value() & v;
  }

  underlying_t value() const { return this->set.load(); }

  bool empty() const { return this->value() == 0; }

  void clear() {
    this->set.store(0);
    this->pendingIndex = 1;
  }

  int popPendingSig();
};

} // namespace arsh

#endif // ARSH_SIGNAL_LIST_H
