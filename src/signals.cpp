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

#include "misc/num_util.hpp"
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

SignalEntryRange getStandardSignalEntries() { return {signalEntries, std::size(signalEntries)}; }

static std::vector<SignalEntry> initRealTimeSignalEntries() {
  std::vector<SignalEntry> values;

#if defined(SIGRTMIN) && defined(SIGRTMAX)
  const int min = SIGRTMIN;
  const int max = SIGRTMAX;
  values.reserve(max - min + 1);
  for (int sigNum = min; sigNum <= max; sigNum++) {
    SignalEntry entry{.abbrName = "", .kind = SignalEntry::Kind::REAL_TIME, .sigNum = sigNum};
    int diff;
    const char *prefix;
    if (sigNum <= min + (max - min) / 2) {
      diff = sigNum - min;
      prefix = "MIN+";
    } else {
      diff = max - sigNum;
      prefix = "MAX-";
    }
    const int s = snprintf(entry.abbrName, std::size(entry.abbrName), "RT%s%d", prefix, diff);
    if (diff == 0) { // remove suffix '-0'/'+0'
      entry.abbrName[s - 1] = '\0';
      entry.abbrName[s - 2] = '\0';
    }
    values.push_back(entry);
  }
#endif

  return values;
}

SignalEntryRange getRealTimeSignalEntries() {
  static const auto entries = initRealTimeSignalEntries();
  return {entries.data(), entries.size()};
}

static auto toInt32(const StringRef ref) { return convertToNum10<int>(ref.begin(), ref.end()); }

static int parseRTSigName(StringRef ref) {
  int rtSig = -1;
  if (const auto range = getRealTimeSignalEntries(); !range.empty() && ref.startsWith("RT")) {
    ref.removePrefix(2);
    const int min = range.front().sigNum;
    const int max = range.back().sigNum;
    if (ref.startsWith("MIN")) {
      ref.removePrefix(3);
      if (ref.empty()) {
        rtSig = min;
      } else if (ref[0] == '+') {
        if (const auto ret = toInt32(ref); ret && ret.value >= 0 && ret.value <= max - min) {
          rtSig = min + ret.value;
        }
      }
    } else if (ref.startsWith("MAX")) {
      ref.removePrefix(3);
      if (ref.empty()) {
        rtSig = max;
      } else if (ref[0] == '-') {
        if (const auto ret = toInt32(ref); ret && ret.value <= 0 && ret.value + max >= min) {
          rtSig = max + ret.value;
        }
      }
    }
  }
  return rtSig;
}

const SignalEntry *findSignalEntryByName(StringRef ref) {
  if (ref.hasNullChar()) {
    return nullptr;
  }

  std::string name = ref.toString();
  std::transform(name.begin(), name.end(), name.begin(), ::toupper);
  ref = name;

  if (ref.startsWith("SIG")) {
    ref.removePrefix(3);
  }
  if (ref.empty()) {
    return nullptr;
  }

  if (const int rtSig = parseRTSigName(ref); rtSig > -1) {
    const auto range = getRealTimeSignalEntries();
    for (auto &e : range) {
      if (rtSig == e.sigNum) {
        return &e;
      }
    }
  }

  const auto range = getStandardSignalEntries();
  for (auto &e : range) {
    if (ref == e.abbrName) {
      return &e;
    }
  }
  return nullptr;
}

const SignalEntry *findSignalEntryByNum(int sigNum) {
  auto range = getStandardSignalEntries();
  for (auto &e : range) {
    if (sigNum == e.sigNum) {
      return &e;
    }
  }
  range = getRealTimeSignalEntries();
  for (auto &e : range) {
    if (sigNum == e.sigNum) {
      return &e;
    }
  }
  return nullptr;
}

std::vector<SignalEntry> toSortedUniqueSignalEntries() {
  std::vector<SignalEntry> values(NSIG);
  const SignalEntryRange ranges[] = {
      getStandardSignalEntries(),
      getRealTimeSignalEntries(),
  };
  for (auto &range : ranges) {
    for (auto &e : range) {
      if (!static_cast<bool>(values[e.sigNum])) {
        values[e.sigNum] = e;
      }
    }
  }
  const auto iter = std::remove_if(values.begin(), values.end(),
                                   [](const SignalEntry &e) { return !static_cast<bool>(e); });
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