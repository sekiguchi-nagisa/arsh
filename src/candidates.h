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
  const unsigned int canSize; // len(candidate)
  const unsigned int sigSize; // len(signature)

  char payload[]; // candidate + '@' + signature

  CandidateObject(const StringRef can, const StringRef sig)
      : ObjectWithRtti(TYPE::String), canSize(can.size()), sigSize(sig.size()) {
    memcpy(this->payload, can.data(), can.size());
    this->payload[this->canSize] = '@';
    memcpy(this->payload + this->canSize + 1, sig.data(), sig.size());
  }

public:
  static constexpr size_t MAX_SIZE = SYS_LIMIT_STRING_MAX;

  static ObjPtr<CandidateObject> create(const StringRef candidate, const StringRef signature) {
    assert(candidate.size() <= MAX_SIZE);
    assert(signature.size() <= MAX_SIZE);
    assert(candidate.size() + 1 <= MAX_SIZE - signature.size());
    const unsigned int allocSize = candidate.size() + signature.size() + 1;
    void *ptr = malloc(sizeof(CandidateObject) + sizeof(char) * allocSize);
    auto *obj = new (ptr) CandidateObject(candidate, signature);
    return ObjPtr<CandidateObject>(obj);
  }

  static void operator delete(void *ptr) noexcept { // NOLINT
    free(ptr);
  }

  unsigned int candidateSize() const { return this->canSize; }

  unsigned int signatureSize() const { return this->sigSize; }

  unsigned int allocSize() const { return this->canSize + 1 + this->sigSize; }

  StringRef candidate() const { return {this->payload, this->candidateSize()}; }

  StringRef signature() const { return {this->payload + this->canSize + 1, this->signatureSize()}; }

  StringRef underlying() const { return {this->payload, this->allocSize()}; }
};

class CandidatesWrapper {
private:
  ObjPtr<ArrayObject> obj; // must be [String] or Candidates

public:
  explicit CandidatesWrapper(const TypePool &pool);

  explicit CandidatesWrapper(const ObjPtr<ArrayObject> &obj) : obj(obj) {}

  explicit CandidatesWrapper(ObjPtr<ArrayObject> &&obj) : obj(std::move(obj)) {}

  explicit operator bool() const { return static_cast<bool>(this->obj); }

  bool add(ARState &state, Value &&value) { return this->obj->append(state, std::move(value)); }

  bool addNew(ARState &state, StringRef candidate, StringRef signature);

  /**
   * @param state
   * @param candidate
   * must be String
   * @param signature
   * must be String or invalid
   * @return
   */
  bool add(ARState &state, Value &&candidate, Value &&signature);

  /**
   * @param state
   * @param o
   * must be [String] or Candidates
   * @return
   */
  bool addAll(ARState &state, const ArrayObject &o);

  ObjPtr<ArrayObject> take() && { return std::move(this->obj); }

  void sortAndDedup(unsigned int beginOffset);

  unsigned int size() const { return this->obj->size(); }

  StringRef getCandidateAt(const unsigned int index) const {
    return toStrRef(this->values()[index]);
  }

  StringRef getSignatureAt(const unsigned int index) const {
    auto &v = this->values()[index];
    return v.isObject() && isa<CandidateObject>(v.get()) ? typeAs<CandidateObject>(v).signature()
                                                         : "";
  }

  /**
   * resolve common prefix string (valid utf-8)
   * @return
   * if not found common prefix string, return empty
   */
  StringRef getCommonPrefixStr() const;

  static StringRef toStrRef(const Value &v) {
    return v.isObject() && isa<CandidateObject>(v.get()) ? typeAs<CandidateObject>(v).candidate()
                                                         : v.asStrRef();
  }

private:
  const std::vector<Value> &values() const { return this->obj->getValues(); }
};

} // namespace arsh

#endif // ARSH_CANDIDATES_H
