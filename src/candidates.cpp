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
#include "misc/unicode.hpp"
#include "type_pool.h"

namespace arsh {

// ###############################
// ##     CandidatesWrapper     ##
// ###############################

CandidatesWrapper::CandidatesWrapper(const TypePool &pool)
    : obj(toObjPtr<ArrayObject>(Value::create<ArrayObject>(pool.get(TYPE::Candidates)))) {}

bool CandidatesWrapper::addAsCandidate(ARState &state, const Value &value) {
  assert(value.hasStrRef());
  if (value.asStrRef().empty()) {
    return true;
  }
  return this->add(state, value.withMetaData(toUnderlying(CandidateAttr::NONE)));
}

bool CandidatesWrapper::addNewCandidate(ARState &state, Value &&candidate, Value &&description) {
  assert(candidate.hasStrRef());
  if (candidate.asStrRef().empty()) {
    return true;
  }
  if (description.isInvalid() || description.asStrRef().empty()) {
    return this->addAsCandidate(state, candidate);
  }
  return this->addNewCandidateWith(state, candidate.asStrRef(), description.asStrRef(),
                                   CandidateAttr::NONE);
}

bool CandidatesWrapper::addNewCandidateWith(ARState &state, StringRef candidate,
                                            StringRef description, const CandidateAttr attr) {
  if (likely(candidate.size() < CandidateObject::MAX_SIZE &&
             description.size() < CandidateObject::MAX_SIZE &&
             candidate.size() + 1 <= CandidateObject::MAX_SIZE - description.size())) {
    const Value value = CandidateObject::create(candidate, description);
    return this->add(state, value.withMetaData(toUnderlying(attr)));
  }
  raiseError(state, TYPE::OutOfRangeError, "sum of candidate and signature size reaches limit");
  return false;
}

bool CandidatesWrapper::addAll(ARState &state, const ArrayObject &o) {
  assert(o.getTypeID() == toUnderlying(TYPE::Candidates));
  if (this->obj.get() != std::addressof(o)) {
    const auto &values = o.getValues();
    for (auto &e : values) {
      if (!this->add(state, Value(e))) {
        return false;
      }
    }
  }
  return true;
}

void CandidatesWrapper::sortAndDedup(const unsigned int beginOffset) {
  if (beginOffset >= this->size() || this->size() - beginOffset == 1) {
    return;
  }
  std::sort(this->obj->refValues().begin() + beginOffset, this->obj->refValues().end(),
            [](const Value &x, const Value &y) {
              const int r = toStrRef(x).compare(toStrRef(y));
              return r < 0 || (r == 0 && x.getMetaData() < y.getMetaData());
            });

  // dedup
  auto begin = this->obj->refValues().begin() + beginOffset;
  const auto end = this->obj->refValues().end();
  auto iter = begin;
  Value prev = std::move(*begin);
  for (++begin; begin != end; ++begin) {
    if (toStrRef(prev) != toStrRef(*begin)) {
      *iter = std::move(prev);
      ++iter;
      prev = std::move(*begin);
    }
  }
  *iter = std::move(prev);
  ++iter;
  this->obj->refValues().erase(iter, end);
}

StringRef CandidatesWrapper::getCommonPrefixStr() const {
  const auto size = this->values().size();
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

} // namespace arsh