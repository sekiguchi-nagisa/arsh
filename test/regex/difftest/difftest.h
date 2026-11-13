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

#ifndef ARSH_TEST_REGEX_DIFFTEST_H
#define ARSH_TEST_REGEX_DIFFTEST_H

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include <misc/unicode.hpp>
#include <regex/flag.h>

#include "../tools/json/json.h"

namespace arsh::difftest {

/**
 * A single differential test case, read from the corpus (`difftest/cases`).
 *
 * `pattern` and `input` are UTF-8 and `flags` is the flag string accepted by `regex::Flag::parse`
 * (`u`, `v`, `i`, `m`, `s` in any order), so the arsh engine and the Node.js oracle are driven by
 * exactly the same spelling. Only the `u` (unicode) and `v` (unicode sets) modes are used: arsh
 * matches UTF-8 input by code point, and those are the code point based modes that are comparable
 * with JavaScript, so every case is required to be in one of them (see `Case::isUnicodeMode`).
 *
 * `requires` lists the syntax features the oracle must implement (see the `capabilities` banner of
 * `oracle.js`); a case is skipped, not failed, when the Node.js binary does not support one.
 *
 * `unicode` is `"versioned"` when the outcome may depend on the Unicode data version (`\p{Emoji}`
 * against a recently added emoji, for instance). Such a case is skipped unless arsh and Node.js
 * bundle the same Unicode version, so a difference between their UCD revisions never shows up as a
 * divergence. It is empty (equivalently `"stable"`) when the outcome is version independent.
 *
 * `xfail` marks a case where arsh is known to disagree with the oracle; the case then doubles as an
 * executable bug report. `xfailArsh` / `xfailOracle` record the exact result each side produced
 * when the divergence was filed (see `MatchResult::toCompactString`), so the harness fails both
 * when arsh starts to agree (drop the marker) and when the known wrong result changes (update the
 * record). Keeping the old behavior pinned on purpose makes the case useful in both directions.
 */
struct Case {
  std::string pattern;
  std::string input;
  std::string flags{"u"};
  std::vector<std::string> requiredFeatures;
  std::string unicode;
  bool xfail{false};
  std::string xfailArsh;   // the arsh result recorded when the divergence was filed
  std::string xfailOracle; // the oracle result recorded when the divergence was filed

  /**
   * Deserialize one corpus line. The optional members are read directly out of the JSON object
   * instead of through the generic object deserializer, which treats a missing member as an error.
   */
  static Case fromJSON(const json::JSON &json);

  /**
   * Parse `flags` into the arsh flag representation. The corpus only uses the ECMAScript flag
   * letters, so a parse failure means the corpus itself is malformed.
   */
  Optional<regex::Flag> parseFlag(std::string *err) const {
    return regex::Flag::parse(this->flags, regex::Mode::UNICODE, err);
  }

  /**
   * true if the case runs in the unicode or unicode sets mode. This holds for every well formed
   * corpus entry (`Flag::parse` defaults to the unicode mode) and is checked rather than assumed,
   * because comparing a byte oriented mode with JavaScript would be meaningless.
   */
  bool isUnicodeMode() const {
    std::string err;
    auto flag = this->parseFlag(&err);
    return flag.hasValue() && flag.unwrap().isEitherUnicodeMode();
  }

  bool isVersioned() const { return this->unicode == "versioned"; }
};

/**
 * One capture of a match, as a half-open range of UTF-8 byte offsets into the input.
 *
 * `set` is false for a group that did not participate in the match. Keeping `set` separate from
 * the offsets (instead of using `start == end == 0`) distinguishes an empty match at the start of
 * the input from an unmatched group, which JavaScript reports as `undefined` and arsh as an unset
 * capture.
 */
struct CaptureRange {
  bool set{false};
  uint32_t start{0};
  uint32_t end{0};

  friend bool operator==(const CaptureRange &x, const CaptureRange &y) {
    if (x.set != y.set) {
      return false;
    }
    return !x.set || (x.start == y.start && x.end == y.end);
  }

  friend bool operator!=(const CaptureRange &x, const CaptureRange &y) { return !(x == y); }

  std::string toString() const {
    if (!this->set) {
      return "(unset)";
    }
    std::string ret = "(offset=";
    ret += std::to_string(this->start);
    ret += ", size=";
    ret += std::to_string(this->end - this->start);
    ret += ')';
    return ret;
  }
};

/**
 * The outcome of running one case, normalized so an arsh result and an oracle result are directly
 * comparable.
 */
struct MatchResult {
  enum class Kind : unsigned char {
    MATCH,
    NOMATCH,
    COMPILE_ERROR, // the pattern is rejected (arsh) / throws a SyntaxError (oracle)
    RUNTIME_ERROR, // the engine failed for another reason; such a case is not compared
    SKIP,          // the case could not be run (missing feature, timeout, ...)
  };

  Kind kind{Kind::SKIP};
  std::vector<CaptureRange> captures; // group 0 first, only meaningful for MATCH
  std::string message;                // error / skip reason

  bool isComparable() const {
    return this->kind == Kind::MATCH || this->kind == Kind::NOMATCH ||
           this->kind == Kind::COMPILE_ERROR;
  }

  bool isSkip() const { return this->kind == Kind::SKIP; }

  const char *kindName() const;

  std::string toString() const;

  /**
   * Single line form, used to record an expected result in the corpus: `match [0,4) unset`.
   */
  std::string toCompactString() const;
};

/**
 * Convert capture offsets reported in UTF-16 code units (what JavaScript reports) into UTF-8 byte
 * offsets (what arsh reports).
 *
 * arsh matches UTF-8 input by code point, while `RegExp.prototype.exec` with the `d` flag returns
 * UTF-16 code unit indices, so every reported index has to be translated before the two results
 * can be compared: a supplementary code point is one code point (4 UTF-8 bytes) for arsh but two
 * code units for JavaScript.
 *
 * The input is decoded once into an index table. A code unit index that falls between the two
 * halves of a surrogate pair has no UTF-8 byte offset; `toByteOffset` reports that as an error
 * instead of silently rounding, because in the code point based modes a match boundary can never
 * legitimately be there.
 */
class Utf16Index {
private:
  std::vector<uint32_t> byteOffsets; // code unit index -> byte offset, INVALID if inside a pair
  uint32_t codeUnitCount{0};

public:
  static constexpr uint32_t INVALID = UINT32_MAX;

  /**
   * @param utf8 UTF-8 input (WTF-8 is accepted, so a lone surrogate counts as one code unit)
   */
  static Utf16Index create(const StringRef utf8);

  uint32_t getCodeUnitCount() const { return this->codeUnitCount; }

  /**
   * @return the byte offset of the given code unit index, or INVALID if the index is out of range
   * or points into the middle of a surrogate pair
   */
  uint32_t toByteOffset(const uint32_t codeUnitIndex) const {
    if (codeUnitIndex > this->codeUnitCount) {
      return INVALID;
    }
    return this->byteOffsets[codeUnitIndex];
  }
};

/**
 * Compare an arsh result with the oracle result for the same case.
 *
 * @return true if they agree. On disagreement a human readable report is appended to `report`.
 */
bool compareResult(const MatchResult &arsh, const MatchResult &oracle, std::string &report);

/**
 * Format a case so a failure can be reproduced by hand.
 */
std::string formatCase(const Case &c);

/**
 * Let GoogleTest print a case as its pattern / input / flags instead of as a raw byte blob, so a
 * failing parameterized test names the case it failed on.
 */
void PrintTo(const Case &c, std::ostream *os);

/**
 * Run one case through the arsh regex engine (in process, so a NUL byte in the input is fine).
 *
 * The match is performed with the prefilter both enabled and disabled: the prefilter is only an
 * optimization, so a difference between the two means the search itself is unsound. That check is
 * reported as a runtime error, since the oracle has nothing to say about it.
 */
MatchResult runArsh(const Case &c, bool checkPrefilter = true);

} // namespace arsh::difftest

#endif // ARSH_TEST_REGEX_DIFFTEST_H
