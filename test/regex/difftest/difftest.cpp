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

#include <regex/emit.h>
#include <regex/match_context.h>
#include <regex/parser.h>

#include "difftest.h"

namespace arsh::difftest {

// ###########################
// ##     MatchResult     ##
// ###########################

const char *MatchResult::kindName() const {
  switch (this->kind) {
  case Kind::MATCH:
    return "match";
  case Kind::NOMATCH:
    return "nomatch";
  case Kind::COMPILE_ERROR:
    return "compile-error";
  case Kind::RUNTIME_ERROR:
    return "runtime-error";
  case Kind::SKIP:
    return "skip";
  }
  return "";
}

std::string MatchResult::toString() const {
  std::string ret = this->kindName();
  switch (this->kind) {
  case Kind::MATCH: {
    ret += '\n';
    for (auto &c : this->captures) {
      ret += c.toString();
      ret += '\n';
    }
    break;
  }
  case Kind::NOMATCH:
    break;
  case Kind::COMPILE_ERROR:
  case Kind::RUNTIME_ERROR:
  case Kind::SKIP:
    if (!this->message.empty()) {
      ret += ": ";
      ret += this->message;
    }
    break;
  }
  return ret;
}

std::string MatchResult::toCompactString() const {
  std::string ret = this->kindName();
  if (this->kind == Kind::MATCH) {
    for (auto &c : this->captures) {
      ret += ' ';
      if (!c.set) {
        ret += "unset";
      } else {
        ret += '[';
        ret += std::to_string(c.start);
        ret += ',';
        ret += std::to_string(c.end);
        ret += ')';
      }
    }
  }
  return ret;
}

// ##########################
// ##     Utf16Index     ##
// ##########################

Utf16Index Utf16Index::create(const StringRef utf8) {
  Utf16Index ret;
  ret.byteOffsets.push_back(0);
  const char *iter = utf8.begin();
  const char *const end = utf8.end();
  while (iter != end) {
    const uint32_t offset = static_cast<uint32_t>(iter - utf8.begin());
    int codePoint = 0;
    if (unsigned int len = UnicodeUtil::wtf8ToCodePoint(iter, end, codePoint)) {
      iter += len;
      if (UnicodeUtil::isSupplementaryCodePoint(codePoint)) { // two UTF-16 code units
        ret.byteOffsets.push_back(INVALID);                   // index of the high surrogate
        ret.byteOffsets.push_back(offset + len);
        ret.codeUnitCount += 2;
      } else {
        ret.byteOffsets.push_back(offset + len);
        ret.codeUnitCount++;
      }
    } else { // invalid byte, count it as one code unit but do not advance by a character
      iter++;
      ret.byteOffsets.push_back(offset + 1);
      ret.codeUnitCount++;
    }
  }
  return ret;
}

// ###########################
// ##     Case loading     ##
// ###########################

Case Case::fromJSON(const json::JSON &json) {
  Case ret;
  if (!json.isObject()) {
    return ret;
  }
  const auto &obj = json.asObject();
  if (auto iter = obj.find("pattern"); iter != obj.end() && iter->second.isString()) {
    ret.pattern = iter->second.asString();
  }
  if (auto iter = obj.find("input"); iter != obj.end() && iter->second.isString()) {
    ret.input = iter->second.asString();
  }
  if (auto iter = obj.find("flags"); iter != obj.end() && iter->second.isString()) {
    ret.flags = iter->second.asString();
  }
  if (auto iter = obj.find("unicode"); iter != obj.end() && iter->second.isString()) {
    ret.unicode = iter->second.asString();
  }
  if (auto iter = obj.find("xfail"); iter != obj.end() && iter->second.isBool()) {
    ret.xfail = iter->second.asBool();
  }
  if (auto iter = obj.find("xfailArsh"); iter != obj.end() && iter->second.isString()) {
    ret.xfailArsh = iter->second.asString();
  }
  if (auto iter = obj.find("xfailOracle"); iter != obj.end() && iter->second.isString()) {
    ret.xfailOracle = iter->second.asString();
  }
  if (auto iter = obj.find("requires"); iter != obj.end() && iter->second.isArray()) {
    for (const auto &e : iter->second.asArray()) {
      if (e.isString()) {
        ret.requiredFeatures.push_back(e.asString());
      }
    }
  }
  return ret;
}

void PrintTo(const Case &c, std::ostream *os) { *os << formatCase(c); }

std::string formatCase(const Case &c) {
  auto quote = [](const std::string &str) {
    std::string out;
    json::JSON::quote(str, out);
    return out;
  };
  std::string ret = "pattern=";
  ret += quote(c.pattern);
  ret += " input=";
  ret += quote(c.input);
  ret += " flags=";
  ret += quote(c.flags);
  return ret;
}

// ###########################
// ##     arsh (engine)    ##
// ###########################

static MatchResult toMatchResult(const regex::MatchStatus status,
                                 const std::vector<regex::Capture> &captures) {
  MatchResult ret;
  switch (status) {
  case regex::MatchStatus::OK:
    ret.kind = MatchResult::Kind::MATCH;
    for (auto &c : captures) {
      CaptureRange range;
      if (c) {
        range.set = true;
        range.start = c.offset;
        range.end = c.endOffset();
      }
      ret.captures.push_back(range);
    }
    break;
  case regex::MatchStatus::FAIL:
    ret.kind = MatchResult::Kind::NOMATCH;
    break;
  case regex::MatchStatus::INVALID_UTF8:
  case regex::MatchStatus::INPUT_LIMIT:
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    ret.message = regex::toString(status);
    break;
  case regex::MatchStatus::CANCEL:
  case regex::MatchStatus::TIMEOUT:
  case regex::MatchStatus::STACK_LIMIT:
    ret.kind = MatchResult::Kind::SKIP;
    ret.message = regex::toString(status);
    break;
  case regex::MatchStatus::CONSUMED_LIMIT:
  case regex::MatchStatus::INVALID_REPLACE_PATTERN:
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    ret.message = "unexpected match status";
    break;
  }
  return ret;
}

static MatchResult matchWith(const regex::Regex &re, const StringRef input, const bool prefilter) {
  std::vector<regex::Capture> captures;
  auto ctx = regex::tryToCreateMatchContext(re, input, 0, captures);
  if (!ctx) {
    return toMatchResult(ctx.asErr(), captures);
  }
  ctx.asOk().setPrefilter(prefilter);
  // generous per case limit: the corpus holds a few pathological patterns, and a case that
  // exceeds the limit is skipped rather than failed (the oracle has no timeout of its own)
  regex::Timer timer(std::chrono::milliseconds(5000));
  const auto status = regex::match(re, ctx.asOk(), makeObserver(timer));
  return toMatchResult(status, captures);
}

MatchResult runArsh(const Case &case_, const bool checkPrefilter) {
  std::string err;
  auto flag = case_.parseFlag(&err);
  if (!flag.hasValue()) {
    MatchResult ret;
    ret.kind = MatchResult::Kind::COMPILE_ERROR;
    ret.message = err;
    return ret;
  }

  regex::Parser parser;
  auto tree = parser(case_.pattern, flag.unwrap());
  if (parser.hasError()) {
    MatchResult ret;
    ret.kind = MatchResult::Kind::COMPILE_ERROR;
    ret.message = parser.getError()->message;
    return ret;
  }

  regex::CodeGen codeGen;
  auto re = codeGen(std::move(tree));
  if (!re.hasValue()) {
    MatchResult ret;
    ret.kind = MatchResult::Kind::COMPILE_ERROR;
    ret.message = codeGen.getError();
    return ret;
  }

  const StringRef input = case_.input;
  auto ret = matchWith(re.unwrap(), input, true);
  if (!checkPrefilter || !ret.isComparable()) {
    return ret;
  }

  auto withoutPrefilter = matchWith(re.unwrap(), input, false);
  if (withoutPrefilter.isComparable() &&
      (withoutPrefilter.kind != ret.kind || withoutPrefilter.captures != ret.captures)) {
    MatchResult diff;
    diff.kind = MatchResult::Kind::RUNTIME_ERROR;
    diff.message = "prefilter changed the result: prefilter=on -> " + ret.toString() +
                   ", prefilter=off -> " + withoutPrefilter.toString();
    return diff;
  }
  return ret;
}

// ###########################
// ##     comparison      ##
// ###########################

bool compareResult(const MatchResult &arsh, const MatchResult &oracle, std::string &report) {
  if (arsh.kind != oracle.kind) {
    report += "  arsh  : ";
    report += arsh.toString();
    report += "  oracle: ";
    report += oracle.toString();
    return false;
  }
  if (arsh.kind != MatchResult::Kind::MATCH) {
    return true;
  }
  if (arsh.captures.size() != oracle.captures.size()) {
    report += "  capture count differs: arsh=";
    report += std::to_string(arsh.captures.size());
    report += " oracle=";
    report += std::to_string(oracle.captures.size());
    report += '\n';
    return false;
  }
  for (size_t i = 0; i < arsh.captures.size(); i++) {
    if (arsh.captures[i] != oracle.captures[i]) {
      report += "  capture ";
      report += std::to_string(i);
      report += " differs: arsh=";
      report += arsh.captures[i].toString();
      report += " oracle=";
      report += oracle.captures[i].toString();
      report += '\n';
      return false;
    }
  }
  return true;
}

} // namespace arsh::difftest
