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

#ifndef ARSH_CANDIDATES_H
#define ARSH_CANDIDATES_H

#include "object.h"

namespace arsh {

class CandidateObject : public ObjectWithRtti<ObjectKind::Candidate> {
private:
  const unsigned int canSize;  // len(candidate)
  const unsigned int descSize; // len(description)

  char payload[]; // candidate + '@' + description

  CandidateObject(const StringRef can, const StringRef desc)
      : ObjectWithRtti(TYPE::String), canSize(can.size()), descSize(desc.size()) {
    memcpy(this->payload, can.data(), can.size());
    this->payload[this->canSize] = '@';
    memcpy(this->payload + this->canSize + 1, desc.data(), desc.size());
  }

public:
  static constexpr size_t MAX_SIZE = SYS_LIMIT_STRING_MAX;

  static bool checkAllocSize(const size_t canSize, const size_t descSize) {
    return canSize < MAX_SIZE && descSize < MAX_SIZE && canSize + 1 <= MAX_SIZE - descSize;
  }

  static ObjPtr<CandidateObject> create(const StringRef candidate, const StringRef description) {
    assert(checkAllocSize(candidate.size(), description.size()));
    const unsigned int allocSize = candidate.size() + description.size() + 1;
    void *ptr = operator new(sizeof(CandidateObject) + (sizeof(char) * allocSize));
    auto *obj = new (ptr) CandidateObject(candidate, description);
    return ObjPtr<CandidateObject>(obj);
  }

  void operator delete(void *ptr) { ::operator delete(ptr); }

  unsigned int candidateSize() const { return this->canSize; }

  unsigned int descriptionSize() const { return this->descSize; }

  unsigned int allocSize() const { return this->canSize + 1 + this->descSize; }

  StringRef candidate() const { return {this->payload, this->candidateSize()}; }

  StringRef description() const {
    return {this->payload + this->canSize + 1, this->descriptionSize()};
  }

  StringRef underlying() const { return {this->payload, this->allocSize()}; }
};

struct CandidateAttr {
  enum class Kind : unsigned char { // not change enum order
    NONE,
    KEYWORD,
    CMD_MOD,
    CMD_UDC,
    CMD_BUILTIN,
    CMD_DYNA,
    CMD_EXTERNAL,
    TYPE_SIGNATURE, // for variable/function/member
  } kind;

  enum class Suffix : unsigned char {
    NONE,       // do nothing
    SPACE,      // insert space
    PAREN,      // insert parenthesis `(`
    PAREN_PAIR, // insert parenthesis pair `()`
  } suffix;

  CandidateAttr(Kind k, bool needSpace)
      : kind(k), suffix(needSpace ? Suffix::SPACE : Suffix::NONE) {}

  CandidateAttr(Kind k, Suffix s) : kind(k), suffix(s) {}
};

class CandidatesObject : public ObjectWithRtti<ObjectKind::Candidates> {
public:
  static constexpr size_t MAX_SIZE = SYS_LIMIT_ARRAY_MAX;

  using Entry = std::pair<Value, CandidateAttr>;

private:
  std::vector<Entry> entries; // must be String or Candidate
  const bool sorting;

public:
  explicit CandidatesObject(bool sorting = true)
      : ObjectWithRtti(TYPE::Candidates), sorting(sorting) {}

  /**
   * for builtin method. always ignore an empty candidate
   * @param state
   * @param candidate
   * must be String
   * @param description
   * must be String or invalid
   * @param needSpace
   * @return
   */
  bool addNewCandidate(ARState &state, Value &&candidate, Value &&description, bool needSpace);

  bool addNewCandidateWith(ARState &state, StringRef candidate, StringRef description,
                           CandidateAttr attr);

  bool addNewCandidateFrom(ARState &state, std::string &&candidate, CandidateAttr attr);

  bool addNewCandidateFrom(ARState &state, std::string &&candidate, bool needSpace) {
    return this->addNewCandidateFrom(state, std::move(candidate),
                                     {CandidateAttr::Kind::NONE, needSpace});
  }

  /**
   * @param state
   * @param o
   * @return
   */
  bool addAll(ARState &state, const CandidatesObject &o) {
    if (this != std::addressof(o)) {
      for (auto &[v, a] : o.entries) {
        if (!this->add(state, Value(v), a)) {
          return false;
        }
      }
    }
    return true;
  }

  size_t size() const { return this->entries.size(); }

  void clearAndShrink() { this->entries = {}; }

  ObjPtr<CandidatesObject> copy() const {
    auto ret = createObject<CandidatesObject>(this->sorting);
    ret->entries = this->entries;
    return ret;
  }

  void sortAndDedup();

  StringRef getCandidateAt(const unsigned int index) const {
    return toStrRef(this->entries[index].first);
  }

  StringRef getDescriptionAt(unsigned int index) const;

  CandidateAttr getAttrAt(const unsigned int index) const { return this->entries[index].second; }

  const auto &operator[](const size_t index) const { return this->entries[index]; }

  /**
   * resolve common prefix string (valid utf-8)
   * @return
   * if not found common prefix string, return empty
   */
  StringRef resolveCommonPrefixStr() const;

  /**
   * quote each candidate.
   *
   * ex. -\-h => [ --hey, --help ]  => [ -\-hey, -\-help ]
   * @param quotedWord must be quoted
   * @param asCmd only affect if quotedWord is empty
   */
  void quote(StringRef quotedWord, bool asCmd);

  static bool isCandidateObj(const Value &v) {
    return v.isObject() && isa<CandidateObject>(v.get());
  }

private:
  static StringRef toStrRef(const Value &v) {
    return isCandidateObj(v) ? typeAs<CandidateObject>(v).candidate() : v.asStrRef();
  }

  bool add(ARState &state, Value &&value, CandidateAttr attr);
};

} // namespace arsh

#endif // ARSH_CANDIDATES_H
