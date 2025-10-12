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

  static ObjPtr<CandidateObject> create(const StringRef candidate, const StringRef description) {
    assert(candidate.size() <= MAX_SIZE);
    assert(description.size() <= MAX_SIZE);
    assert(candidate.size() + 1 <= MAX_SIZE - description.size());
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

class CandidatesWrapper {
private:
  ObjPtr<ArrayObject> obj; // must be Candidates

public:
  union Meta {
    CandidateAttr attr;
    unsigned int value;
  };

  static_assert(sizeof(Meta) == sizeof(unsigned int));

  explicit CandidatesWrapper(const TypePool &pool);

  explicit CandidatesWrapper(const ObjPtr<ArrayObject> &obj) : obj(obj) {
    assert(!this->obj || this->obj->getTypeID() == toUnderlying(TYPE::Candidates));
  }

  explicit CandidatesWrapper(ObjPtr<ArrayObject> &&obj) : obj(std::move(obj)) {
    assert(!this->obj || this->obj->getTypeID() == toUnderlying(TYPE::Candidates));
  }

  explicit operator bool() const { return static_cast<bool>(this->obj); }

  /**
   * always ignore empty candidate
   * @param state
   * @param value
   * must be String
   * @param needSpace
   * @return
   */
  bool addAsCandidate(ARState &state, const Value &value, bool needSpace);

  /**
   * for builtin method. always ignore empty candidate
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

  /**
   * @param state
   * @param o
   * must be Candidates
   * @return
   */
  bool addAll(ARState &state, const ArrayObject &o);

  void pop() {
    this->obj->refValues().pop_back(); // not check iterator invalidation
  }

  void clearAndShrink() {
    this->obj->refValues().clear(); // not check iterator invalidation
    this->obj->refValues().shrink_to_fit();
  }

  ObjPtr<ArrayObject> take() && { return std::move(this->obj); }

  void sortAndDedup(unsigned int beginOffset);

  unsigned int size() const { return this->obj->size(); }

  StringRef getCandidateAt(const unsigned int index) const {
    return toStrRef(this->underlying()[index]);
  }

  StringRef getDescriptionAt(const unsigned int index) const {
    auto &v = this->underlying()[index];
    return v.isObject() && isa<CandidateObject>(v.get()) ? typeAs<CandidateObject>(v).description()
                                                         : "";
  }

  CandidateAttr getAttrAt(const unsigned int index) const {
    return getAttr(this->underlying()[index]);
  }

  /**
   * resolve common prefix string (valid utf-8)
   * @return
   * if not found common prefix string, return empty
   */
  StringRef getCommonPrefixStr() const;

private:
  static StringRef toStrRef(const Value &v) {
    return v.isObject() && isa<CandidateObject>(v.get()) ? typeAs<CandidateObject>(v).candidate()
                                                         : v.asStrRef();
  }

  static CandidateAttr getAttr(const Value &v) {
    const Meta m{.value = v.getMetaData()};
    return m.attr;
  }

  const ArrayObject &underlying() const { return *this->obj; }

  bool add(ARState &state, Value &&v) { return this->obj->append(state, std::move(v)); }
};

} // namespace arsh

#endif // ARSH_CANDIDATES_H
