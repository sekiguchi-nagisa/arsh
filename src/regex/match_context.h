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

#ifndef ARSH_REGEX_MATCH_CONTEXT_H
#define ARSH_REGEX_MATCH_CONTEXT_H

#include <cstdint>
#include <vector>

#include "input.h"
#include "regex.h"

namespace arsh::regex {

struct LoopState {
  uint32_t count{0};
  uint32_t inputOffset{0};
};

class MatchContext {
private:
  const Regex &regex;
  Input input;
  std::vector<Capture> &captures;
  std::vector<LoopState> loops;

public:
  MatchContext(const Regex &regex, const Input &input, std::vector<Capture> &captures)
      : regex(regex), input(input), captures(captures) {
    this->loops.resize(this->regex.getLoopCount());
  }

  const Regex &getRegex() const { return this->regex; }

  Capture *getCaptures() const { return this->captures.data(); }

  LoopState *getLoops() { return this->loops.data(); }

  Input copyInput() const { return this->input; }

  void syncInput(const Input &in) { this->input = in; }

  const Input &getInput() const { return this->input; }

  Input &refInput() { return this->input; }

  const Inst *getInst() const { return this->regex.getInstSeq().data(); }

  void clearCaptures() const {
    this->captures.clear();
    this->captures.resize(this->regex.getCaptureGroupCount() + 1);
  }

  ArrayRef<Matcher> getMatchers() const { return this->regex.getMatchers(); }

  Capture resolveNamedBackRef(unsigned int refIndex) const {
    auto &entry = this->regex.getNamedCaptureGroups().toArrayRef()[refIndex].second;
    return this->resolveNamedBackRef(entry);
  }

  Capture resolveNamedBackRef(const NamedCaptureEntry &entry) const {
    if (entry.hasMultipleIndices()) {
      for (unsigned int i = 0; i < entry.getSize(); i++) {
        unsigned int capIndex = entry[i];
        if (auto cap = this->captures[capIndex]) {
          return cap;
        }
      }
      return {};
    }
    return this->captures[entry.getIndex()];
  }
};

inline Result<MatchContext, MatchStatus> tryToCreateMatchContext(const Regex &regex,
                                                                 const StringRef text,
                                                                 const unsigned int codePointOffset,
                                                                 std::vector<Capture> &captures) {
  Input input;
  if (auto ret = Input::create(text, codePointOffset)) {
    input = ret.asOk();
  } else {
    switch (ret.asErr()) {
    case Input::Error::TOO_LARGE:
      return Err(MatchStatus::INPUT_LIMIT);
    case Input::Error::INVALID_UTF8:
      return Err(MatchStatus::INVALID_UTF8);
    }
  }
  return Ok(MatchContext(regex, input, captures));
}

} // namespace arsh::regex

#endif // ARSH_REGEX_MATCH_CONTEXT_H
