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

#include "misc/flag_util.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

struct SignalPair {
  const char *name;
  int sigNum;
};

/**
 * real time signal is not supported
 * @return
 * terminated with sentinel {nullptr, -1}
 */
const SignalPair *getSignalList();

/**
 *
 * @param name
 * @return
 * if invalid signal name, return -1
 */
int getSignalNum(StringRef name);

/**
 *
 * @param sigNum
 * @return
 * if invalid signal number, return null
 */
const char *getSignalName(int sigNum);

/**
 * get sorted unique signal list
 * @return
 */
std::vector<int> getUniqueSignalList();

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
