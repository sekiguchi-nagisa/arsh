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

#include <atomic>
#include <cerrno>
#include <csignal>
#include <vector>

#include "misc/array_ref.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

struct SignalEntry {
  enum class Kind : unsigned char {
    POSIX_1_1990,
    POSIX_1_2001,
    OTHER,
    REAL_TIME,
  };

  char abbrName[11]; // abbreviate signal name (INT).
  Kind kind{};
  int sigNum{-1};

  bool isRealTime() const { return this->kind == Kind::REAL_TIME; }

  explicit operator bool() const { return this->sigNum > 0; }

  std::string toFullName() const {
    std::string value = "SIG";
    value += this->abbrName;
    return value;
  }
};

using SignalEntryRange = ArrayRef<SignalEntry>;

SignalEntryRange getStandardSignalEntries();

SignalEntryRange getRealTimeSignalEntries();

const SignalEntry *findSignalEntryByName(StringRef name);

const SignalEntry *findSignalEntryByNum(int sigNum);

std::vector<SignalEntry> toSortedUniqueSignalEntries();

class SignalGuard {
private:
  sigset_t maskSet;

public:
  SignalGuard() {
    sigfillset(&this->maskSet);
    sigprocmask(SIG_BLOCK, &this->maskSet, nullptr);
  }

  ~SignalGuard() {
    int e = errno;
    sigprocmask(SIG_UNBLOCK, &this->maskSet, nullptr);
    errno = e;
  }
};

class AtomicSigSet {
private:
  volatile std::atomic_uint64_t set{0};
  int pendingIndex{1};

  using underlying_t = uint64_t;

  static_assert(NSIG - 1 <= sizeof(set) * 8, "huge signal number");
  static_assert(decltype(set)::is_always_lock_free);

public:
  AtomicSigSet() = default;

  AtomicSigSet(AtomicSigSet &&o) noexcept : set(o.value()), pendingIndex(o.pendingIndex) {}

  void add(int sigNum) {
    const underlying_t v = static_cast<underlying_t>(1) << static_cast<underlying_t>(sigNum - 1);
    this->set.fetch_or(v, std::memory_order_release);
  }

  void del(int sigNum) {
    const underlying_t v = static_cast<underlying_t>(1) << static_cast<underlying_t>(sigNum - 1);
    this->set.fetch_and(~v, std::memory_order_release);
  }

  bool has(int sigNum) const {
    const underlying_t v = static_cast<underlying_t>(1) << static_cast<underlying_t>(sigNum - 1);
    return this->value() & v;
  }

  underlying_t value() const { return this->set.load(std::memory_order_acquire); }

  bool empty() const { return this->value() == 0; }

  void clear() {
    this->set.store(0);
    this->pendingIndex = 1;
  }

  int popPendingSig();
};

} // namespace arsh

#endif // ARSH_SIGNAL_LIST_H
