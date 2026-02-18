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

#include "capture.h"
#include "flag.h"
#include "instruction.h"

namespace arsh::regex {

class Regex {
private:
  Flag flag;
  unsigned int captureGroupCount; // 1-based index. if 0, indicate no captures
  FlexBuffer<Inst> instSeq;
  NamedCaptureGroups named;

public:
  static constexpr size_t MAX_STACK_DEPTH = UINT16_MAX >> 2;

  Regex(Flag flag, unsigned int count, FlexBuffer<Inst> &&seq, NamedCaptureGroups &&named)
      : flag(flag), captureGroupCount(count), instSeq(std::move(seq)), named(std::move(named)) {}

  Flag getFlag() const { return this->flag; }

  unsigned int getCaptureGroupCount() const { return this->captureGroupCount; }

  const auto &getInstSeq() const { return this->instSeq; }

  const auto &getNamedCaptureGroups() const { return this->named; }
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

MatchStatus match(const Regex &regex, StringRef text, FlexBuffer<Capture> &captures);

} // namespace arsh::regex

#endif // ARSH_REGEX_REGEX_H
