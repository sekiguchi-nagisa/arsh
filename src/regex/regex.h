/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_REGEX_REGEX_H
#define ARSH_REGEX_REGEX_H

#include <functional>

#include "capture.h"
#include "flag.h"
#include "instruction.h"
#include "matcher.h"
#include "misc/array_ref.hpp"
#include "misc/resource.hpp"
#include "misc/time_util.hpp"

namespace arsh::regex {

class Regex {
private:
  Flag flag;
  unsigned short loopCount;
  unsigned int captureGroupCount; // 1-based index. if 0, indicate no captures
  FlexBuffer<Inst> instSeq;
  std::vector<Matcher> matchers;
  NamedCaptureGroups named;

public:
  static constexpr size_t MAX_STACK_DEPTH = 0xFFFFFF;
  static constexpr unsigned int TIMER_CHECK_INTERVAL = UINT16_MAX >> 1;

  Regex(Flag flag, unsigned short loopCount, FlexBuffer<Inst> &&seq,
        std::vector<Matcher> &&matchers, NamedCaptureGroups &&named, unsigned int count)
      : flag(flag), loopCount(loopCount), captureGroupCount(count), instSeq(std::move(seq)),
        matchers(std::move(matchers)), named(std::move(named)) {}

  Flag getFlag() const { return this->flag; }

  unsigned short getLoopCount() const { return this->loopCount; }

  unsigned int getCaptureGroupCount() const { return this->captureGroupCount; }

  const auto &getInstSeq() const { return this->instSeq; }

  ArrayRef<Matcher> getMatchers() const { return {this->matchers.data(), this->matchers.size()}; }

  const auto &getNamedCaptureGroups() const { return this->named; }
};

class Timer {
public:
  enum class Status : unsigned char {
    None,
    Canceled,
    Expired,
  };

private:
  const std::chrono::milliseconds limit;
  timestamp base;
  std::function<bool()> cancelToken; // return true if canceled

public:
  explicit Timer(std::chrono::milliseconds limit) : limit(limit) {}

  void setCancelToken(std::function<bool()> &&c) { this->cancelToken = std::move(c); }

  static timestamp now() { return getCurrentTimestamp(); }

  void start() { this->base = now(); }

  std::chrono::milliseconds elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(now() - this->base);
  }

  Status check() const {
    if (this->elapsed() >= this->limit) {
      return Status::Expired;
    }
    if (this->cancelToken && this->cancelToken()) {
      return Status::Canceled;
    }
    return Status::None;
  }
};

enum class MatchStatus : unsigned char {
  OK,           // successfully matched
  FAIL,         // no matches
  INVALID_UTF8, // invalid encoding of input
  INPUT_LIMIT,  // the size of the input is too large (up to UINT32_MAX)
  CANCEL,       // interrupted
  TIMEOUT,      // timeout
  STACK_LIMIT,  // stack size reaches limits
};

const char *toString(MatchStatus s);

MatchStatus match(const Regex &regex, StringRef text, std::vector<Capture> &captures,
                  ObserverPtr<Timer> timer = nullptr);

} // namespace arsh::regex

#endif // ARSH_REGEX_REGEX_H
