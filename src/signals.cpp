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
#include <string>

#include "signals.h"

namespace ydsh {

const SignalPair *getSignalList() {
#define SIG_(E) {#E, SIG##E},

  static const SignalPair signalList[] = {

// clang-format off
// POSIX.1-1990 standard
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

// SUSv2 and POSIX.1-2001 standard
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

// other
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
      // clang-format on

      // sentinel
      {nullptr, -1},
  };

  return signalList;
}

int getSignalNum(StringRef ref) {
  if (ref.hasNullChar()) {
    return -1;
  }

  std::string name = ref.toString();
  std::transform(name.begin(), name.end(), name.begin(), ::toupper);
  StringRef nameRef = name;

  if (nameRef.startsWith("SIG")) {
    nameRef.removePrefix(3);
  }

  for (auto ptr = getSignalList(); ptr->name != nullptr; ptr++) {
    if (nameRef == ptr->name) {
      return ptr->sigNum;
    }
  }
  return -1;
}

const char *getSignalName(int sigNum) {
  for (auto ptr = getSignalList(); ptr->name != nullptr; ptr++) {
    if (sigNum == ptr->sigNum) {
      return ptr->name;
    }
  }
  return nullptr;
}

std::vector<int> getUniqueSignalList() {
  std::vector<int> ret;
  ret.reserve(40);
  for (auto *ptr = getSignalList(); ptr->name != nullptr; ptr++) {
    ret.push_back(ptr->sigNum);
  }

  std::sort(ret.begin(), ret.end());
  ret.erase(std::unique(ret.begin(), ret.end()), ret.end());

  return ret;
}

// ####################
// ##     SigSet     ##
// ####################

int SigSet::popPendingSig() {
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

} // namespace ydsh