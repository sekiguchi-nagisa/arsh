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

class SigSet : protected StaticBitSet<uint64_t> {
private:
  static_assert(NSIG - 1 <= BIT_SIZE, "huge signal number");

  int pendingIndex{1};

public:
  void add(int sigNum) { StaticBitSet::add(static_cast<uint8_t>(sigNum - 1)); }

  void del(int sigNum) { StaticBitSet::del(static_cast<uint8_t>(sigNum - 1)); }

  bool has(int sigNum) const { return StaticBitSet::has(static_cast<uint8_t>(sigNum - 1)); }

  bool empty() const { return StaticBitSet::empty(); }

  void clear() {
    StaticBitSet::clear();
    this->pendingIndex = 1;
  }

  int popPendingSig();
};

} // namespace arsh

#endif // ARSH_SIGNAL_LIST_H
