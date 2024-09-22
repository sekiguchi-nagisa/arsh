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

#include <algorithm>

#include "signals.h"

namespace arsh {

static constexpr SignalEntry signalEntries[] = {
// clang-format off
  // POSIX.1-1990 standard
#define SIG_(E) {#E, SignalEntry::Kind::POSIX_1_1990, SIG## E},
#ifdef SIGHUP
  SIG_(HUP)
#endif
#ifdef SIGINT
  SIG_(INT)
#endif
#ifdef SIGQUIT
  SIG_(QUIT)
#endif
#ifdef SIGILL
  SIG_(ILL)
#endif
#ifdef SIGABRT
  SIG_(ABRT)
#endif
#ifdef SIGFPE
  SIG_(FPE)
#endif
#ifdef SIGKILL
  SIG_(KILL)
#endif
#ifdef SIGSEGV
  SIG_(SEGV)
#endif
#ifdef SIGPIPE
  SIG_(PIPE)
#endif
#ifdef SIGALRM
  SIG_(ALRM)
#endif
#ifdef SIGTERM
  SIG_(TERM)
#endif
#ifdef SIGUSR1
  SIG_(USR1)
#endif
#ifdef SIGUSR2
  SIG_(USR2)
#endif
#ifdef SIGCHLD
  SIG_(CHLD)
#endif
#ifdef SIGCONT
  SIG_(CONT)
#endif
#ifdef SIGSTOP
  SIG_(STOP)
#endif
#ifdef SIGTSTP
  SIG_(TSTP)
#endif
#ifdef SIGTTIN
  SIG_(TTIN)
#endif
#ifdef SIGTTOU
  SIG_(TTOU)
#endif
#undef SIG_

// SUSv2 and POSIX.1-2001 standard
#define SIG_(E) {#E, SignalEntry::Kind::POSIX_1_2001, SIG## E},
#ifdef SIGBUS
  SIG_(BUS)
#endif
#ifdef SIGPOLL
  SIG_(POLL)
#endif
#ifdef SIGPROF
  SIG_(PROF)
#endif
#ifdef SIGSYS
  SIG_(SYS)
#endif
#ifdef SIGTRAP
  SIG_(TRAP)
#endif
#ifdef SIGURG
  SIG_(URG)
#endif
#ifdef SIGVTALRM
  SIG_(VTALRM)
#endif
#ifdef SIGXCPU
  SIG_(XCPU)
#endif
#ifdef SIGXFSZ
  SIG_(XFSZ)
#endif
#undef SIG_

// other
#define SIG_(E) {#E, SignalEntry::Kind::OTHER, SIG## E},
#ifdef SIGIOT
  SIG_(IOT)
#endif
#ifdef SIGEMT
  SIG_(EMT)
#endif
#ifdef SIGSTKFLT
  SIG_(STKFLT)
#endif
#ifdef SIGIO
  SIG_(IO)
#endif
#ifdef SIGCLD
  SIG_(CLD)
#endif
#ifdef SIGPWR
  SIG_(PWR)
#endif
#ifdef SIGINFO
  SIG_(INFO)
#endif
#ifdef SIGLOST
  SIG_(LOST)
#endif
#ifdef SIGWINCH
  SIG_(WINCH)
#endif
#ifdef SIGUNUSED
  SIG_(UNUSED)
#endif
#undef SIG_
    // clang-format on
};

SignalEntryRange getSignalEntryRange() {
  return {signalEntries, signalEntries + std::size(signalEntries)};
}

const SignalEntry *findSignalEntryByName(StringRef ref) {
  if (ref.hasNullChar()) {
    return nullptr;
  }

  std::string name = ref.toString();
  std::transform(name.begin(), name.end(), name.begin(), ::toupper);
  StringRef nameRef = name;

  if (nameRef.startsWith("SIG")) {
    nameRef.removePrefix(3);
  }

  const auto range = getSignalEntryRange();
  for (auto &e : range) {
    if (nameRef == e.name) {
      return &e;
    }
  }
  return nullptr;
}

const SignalEntry *findSignalEntryByNum(int sigNum) {
  const auto range = getSignalEntryRange();
  for (auto &e : range) {
    if (sigNum == e.sigNum) {
      return &e;
    }
  }
  return nullptr;
}

std::vector<SignalEntry> toSortedUniqueSignalEntries() {
  const auto range = getSignalEntryRange();
  std::vector<SignalEntry> values(range.begin(), range.end());
  std::sort(values.begin(), values.end());
  auto iter =
      std::unique(values.begin(), values.end(),
                  [](const SignalEntry &x, const SignalEntry &y) { return x.sigNum == y.sigNum; });
  values.erase(iter, values.end());
  return values;
}

// ##########################
// ##     AtomicSigSet     ##
// ##########################

int AtomicSigSet::popPendingSig() {
  assert(!this->empty());
  int sigNum;
  do {
    sigNum = this->pendingIndex++;
    if (this->pendingIndex == NSIG) {
      this->pendingIndex = 1;
    }
  } while (!this->has(sigNum));
  this->del(sigNum);
  return sigNum;
}

} // namespace arsh