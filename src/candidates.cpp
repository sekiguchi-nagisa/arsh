/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#include "candidates.h"
#include "core.h"
#include "format_util.h"
#include "misc/unicode.hpp"

namespace arsh {

// ##############################
// ##     CandidatesObject     ##
// ##############################

bool CandidatesObject::addNewCandidate(ARState &state, Value &&candidate, Value &&description,
                                       bool needSpace) {
  assert(candidate.hasStrRef());
  if (candidate.asStrRef().empty()) {
    return true;
  }
  if (description.isInvalid() || description.asStrRef().empty()) {
    return this->add(state, std::move(candidate), {CandidateAttr::Kind::NONE, needSpace});
  }
  return this->addNewCandidateWith(state, candidate.asStrRef(), description.asStrRef(),
                                   {CandidateAttr::Kind::NONE, needSpace});
}

bool CandidatesObject::addNewCandidateWith(ARState &state, StringRef candidate,
                                           StringRef description, const CandidateAttr attr) {
  if (likely(CandidateObject::checkAllocSize(candidate.size(), description.size()))) {
    Value value = CandidateObject::create(candidate, description);
    return this->add(state, std::move(value), attr);
  }
  raiseError(state, TYPE::OutOfRangeError, "sum of candidate and signature size reaches limit");
  return false;
}

static StringRef toDescription(const CandidateAttr::Kind kind) {
  switch (kind) {
  case CandidateAttr::Kind::NONE:
    break;
  case CandidateAttr::Kind::KEYWORD:
    return "keyword";
  case CandidateAttr::Kind::CMD_MOD:
    return "module";
  case CandidateAttr::Kind::CMD_UDC:
    return "user-defined";
  case CandidateAttr::Kind::CMD_BUILTIN:
    return "builtin";
  case CandidateAttr::Kind::CMD_DYNA:
    return "dynamic";
  case CandidateAttr::Kind::CMD_EXTERNAL:
    return "command";
  case CandidateAttr::Kind::TYPE_SIGNATURE:
    break;
  }
  return "";
}

static size_t toDescriptionSize(const CandidateAttr::Kind kind) {
  return toDescription(kind).size();
}

bool CandidatesObject::addNewCandidateFrom(ARState &state, std::string &&candidate,
                                           const CandidateAttr attr) {
  if (likely(CandidateObject::checkAllocSize(candidate.size(), toDescriptionSize(attr.kind)))) {
    return this->add(state, Value::createStr(std::move(candidate)), attr);
  }
  raiseError(state, TYPE::OutOfRangeError, "candidate size reaches limit");
  return false;
}

void CandidatesObject::sortAndDedup() {
  if (!this->sorting) {
    return; // do nothing
  }
  std::sort(this->entries.begin(), this->entries.end(), [](const Entry &x, const Entry &y) {
    const int r = toStrRef(x.first).compare(toStrRef(y.first));
    return r < 0 || (r == 0 && toUnderlying(x.second.kind) < toUnderlying(y.second.kind));
  });

  // de-dup (only extract the first appeared element)
  const auto iter =
      std::unique(this->entries.begin(), this->entries.end(), [](const Entry &x, const Entry &y) {
        return toStrRef(x.first) == toStrRef(y.first);
      });
  this->entries.erase(iter, this->entries.end());
}

StringRef CandidatesObject::getDescriptionAt(const unsigned int index) const {
  if (auto &v = this->entries[index].first; v.isObject() && isa<CandidateObject>(v.get())) {
    return typeAs<CandidateObject>(v).description();
  }
  return toDescription(this->getAttrAt(index).kind);
}

StringRef CandidatesObject::resolveCommonPrefixStr() const {
  const auto size = this->size();
  if (size == 0) {
    return "";
  }
  if (size == 1) {
    return this->getCandidateAt(0);
  }

  // resolve common prefix length
  size_t prefixSize = 0;
  const auto first = this->getCandidateAt(0);
  for (const auto firstSize = first.size(); prefixSize < firstSize; prefixSize++) {
    const char ch = first[prefixSize];
    size_t index = 1;
    for (; index < size; index++) {
      if (const auto ref = this->getCandidateAt(index);
          prefixSize < ref.size() && ch == ref[prefixSize]) {
        continue;
      }
      break;
    }
    if (index < size) {
      break;
    }
  }

  // extract valid utf8 string
  const StringRef prefix(this->getCandidateAt(0).data(), prefixSize);
  const auto begin = prefix.begin();
  auto iter = begin;
  for (const auto end = prefix.end(); iter != end;) {
    unsigned int byteSize = UnicodeUtil::utf8ValidateChar(iter, end);
    if (byteSize != 0) {
      iter += byteSize;
    } else {
      break;
    }
  }
  return {begin, static_cast<size_t>(iter - begin)};
}

/**
 * ex. quotedWord: `--cmd=l\l`, candidatePrefix: `llvm-`
 * => ret: (`l\l`, `ll`)
 * @param quotedWord
 * @param candidatePrefix
 * @return
 */
static CompPrefix resolveQuotedCandidatePrefix(const StringRef quotedWord,
                                               const StringRef candidatePrefix) {
  const std::string word = unquoteCmdArgLiteral(quotedWord, true);
  /*
   * compute suffix overlapped with candidate prefix
   *
   * ex. word: --cmd=ll, prefix: llvm-
   * => suffix: ll (offset: 6)
   */
  bool matched = false;
  size_t wordSuffixOffset = word.size() - std::min(word.size(), candidatePrefix.size());
  for (; wordSuffixOffset < word.size(); wordSuffixOffset++) {
    auto wordSuffix = StringRef(word).substr(wordSuffixOffset);
    if (candidatePrefix.startsWith(wordSuffix)) {
      matched = true;
      break;
    }
  }
  if (!matched) {
    return {};
  }

  const size_t wordSuffixSize = StringRef(word).substr(wordSuffixOffset).size();
  const StringRef canPrefix = candidatePrefix.substr(0, wordSuffixSize);
  CompPrefix prefix{
      .compWordToken = quotedWord,
      .compWord = word,
  };
  prefix.removePrefix(wordSuffixOffset);
  assert(prefix.compWord == canPrefix);
  prefix.compWord = canPrefix; // due to prevent dangling reference
  return prefix;
}

static void replaceCandidate(CandidatesObject::Entry &entry, std::string &&replaced) {
  if (entry.first.hasStrRef() &&
      CandidateObject::checkAllocSize(replaced.size(), toDescriptionSize(entry.second.kind))) {
    if (replaced.size() <= Value::TValue::MAX_STR_SIZE || entry.first.isSmallStr()) {
      entry.first = Value::createStr(std::move(replaced));
    } else {
      entry.first = typeAs<StringObject>(entry.first).tryToAssign(std::move(replaced));
    }
  } else if (CandidatesObject::isCandidateObj(entry.first) &&
             CandidateObject::checkAllocSize(
                 replaced.size(), typeAs<CandidateObject>(entry.first).descriptionSize())) {
    entry.first =
        CandidateObject::create(replaced, typeAs<CandidateObject>(entry.first).description());
  }
}

void CandidatesObject::quote(const StringRef quotedWord, bool asCmd) {
  if (quotedWord.size()) {
    asCmd = false;
  }

  /**
   * allocate std::string due to prevent dangling reference
   * (original reference will be modified below)
   */
  const std::string candidatePrefix = this->resolveCommonPrefixStr().toString();
  const CompPrefix prefix = resolveQuotedCandidatePrefix(quotedWord, candidatePrefix);

  // modify original candidate (apply quoting)
  const unsigned int size = this->size();
  for (unsigned int i = 0; i < size; i++) {
    const StringRef can = this->getCandidateAt(i);
    assert(prefix.compWord.size() <= can.size());
    std::string replaced = prefix.compWordToken.toString();
    quoteAsCmdOrShellArg(can.substr(prefix.compWord.size()), replaced,
                         {.asCmd = asCmd, .carryBackslash = prefix.carryBackslash()});
    if (can != replaced) {
      assert(replaced.size() > can.size());
      replaceCandidate(this->entries[i], std::move(replaced));
    }
  }
}

bool CandidatesObject::add(ARState &state, Value &&value, CandidateAttr attr) {
  if (unlikely(this->size() == MAX_SIZE)) {
    raiseError(state, TYPE::OutOfRangeError, "reach Candidates size limit");
    return false;
  }
  this->entries.emplace_back(std::move(value), attr);
  return true;
}

} // namespace arsh