/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "ordered_map.h"
#include "core.h"
#include "misc/num_util.hpp"
#include "misc/wyhash.h"
#include "type_pool.h"

namespace ydsh {

// ###############################
// ##     OrderedMapEntries     ##
// ###############################

unsigned int OrderedMapEntries::add(DSValue &&key, DSValue &&value) {
  if (unlikely(this->capacity == 0)) {
    this->capacity = 4;
    this->values = std::make_unique<Entry[]>(this->capacity);
  }

  if (this->usedSize == this->capacity) {
    unsigned int newCap = this->capacity;
    newCap += (newCap >> 1);
    auto newValues = std::make_unique<Entry[]>(newCap);
    for (unsigned int i = 0; i < this->usedSize; i++) {
      newValues[i] = std::move(this->values[i]);
    }
    this->values = std::move(newValues);
    this->capacity = newCap;
  }
  unsigned int index = this->usedSize;
  this->values[index].reset(std::move(key), std::move(value));
  this->usedSize++;
  return index;
}

unsigned int OrderedMapEntries::compact() {
  unsigned int lastDeletedIndex = this->usedSize;

  // find first deleted index
  for (unsigned int i = 0; i < this->usedSize; i++) {
    if (!values[i]) {
      lastDeletedIndex = i;
      break;
    }
  }
  const unsigned int ret = lastDeletedIndex;

  // pack entry
  for (unsigned int i = lastDeletedIndex + 1; i < this->usedSize; i++) {
    if (values[i]) {
      std::swap(values[i], values[lastDeletedIndex]);
      lastDeletedIndex++;
    }
  }
  this->usedSize = lastDeletedIndex;
  return ret;
}

// ##############################
// ##     OrderedMapObject     ##
// ##############################

static unsigned int hash(const DSValue &value) {
  bool isStr = false;
  uint64_t u64 = 0;
  const void *ptr = nullptr;
  size_t size = 0;
  switch (value.kind()) {
  case DSValueKind::BOOL:
    u64 = value.asBool() ? 1 : 0;
    break;
  case DSValueKind::SIG:
    u64 = value.asSig();
    break;
  case DSValueKind::INT:
    u64 = value.asInt();
    break;
  case DSValueKind::FLOAT:
    u64 = doubleToBits(value.asFloat());
    break;
  default:
    if (value.hasStrRef()) {
      auto ref = value.asStrRef();
      ptr = ref.data();
      size = ref.size();
      isStr = true;
    } else {
      assert(value.isObject());
      u64 = static_cast<int64_t>(reinterpret_cast<uintptr_t>(value.get()));
    }
    break;
  }

  if (isStr) {
    uint64_t hash = wyhash(ptr, size, 42, _wyp);
    return static_cast<unsigned int>(hash);
  } else {
    uint64_t hash = wy2u0k(u64, UINT64_MAX);
    return static_cast<unsigned int>(hash);
  }
}

std::pair<int, bool> OrderedMapObject::insert(const DSValue &key, DSValue &&value) {
  if (unlikely(!this->buckets)) {
    this->buckets = std::make_unique<Bucket[]>(this->bucketLen.capacity());
  }

  ProbeState state; // NOLINT
  if (this->probeBuckets(key, state)) {
    return {this->buckets[state.bucketIndex].entryIndex, false};
  }

  if (unlikely(this->size() == MAX_SIZE)) {
    return {-1, false};
  }

  // add entry (but not add to buckets)
  const bool needGrow = this->bucketLen.loadFactor() > MAX_LOAD_FACTOR;
  bool needCompaction = this->entries.getUsedSize() == this->entries.getCapacity() &&
                        this->entries.getUsedSize() != this->bucketLen.size();
  if (needGrow || needCompaction) {
    if (needGrow && this->entries.getUsedSize() != this->bucketLen.size()) {
      needCompaction = true;
    }
    if (needCompaction) {
      this->entries.compact();
    }
    this->rehash(needGrow);
    state.bucketIndex = this->bucketLen.toBucketIndex(state.keyHash);
    state.dist = 0;
  }
  const unsigned int entryIndex =
      this->entries.add(key.withMetaData(state.keyHash), std::move(value));

  // add entry index to buckets
  this->insertEntryIndex(entryIndex, state);

  this->bucketLen.setSize(this->bucketLen.size() + 1);
  return {static_cast<int>(entryIndex), true};
}

void OrderedMapObject::insertEntryIndex(unsigned int entryIndex, const ProbeState &state) {
  unsigned int bucketIndex = state.bucketIndex;
  int dist = state.dist;

  while (true) {
    auto &curBucket = this->buckets[bucketIndex];
    if (!curBucket) {
      curBucket.entryIndex = static_cast<int>(entryIndex);
      curBucket.distanceToInitBucketIndex = dist;
      break;
    } else {
      if (curBucket.distanceToInitBucketIndex < dist) {
        Bucket bucket{
            .distanceToInitBucketIndex = dist,
            .entryIndex = static_cast<int>(entryIndex),
        };
        std::swap(curBucket, bucket);
        dist = bucket.distanceToInitBucketIndex;
        entryIndex = bucket.entryIndex;
      }
      dist++;
      bucketIndex = this->bucketLen.nextBucketIndex(bucketIndex);
    }
  }
}

OrderedMapEntries::Entry OrderedMapObject::remove(const DSValue &key) {
  if (this->bucketLen.size() == 0) {
    return {};
  }

  ProbeState state; // NOLINT
  if (!this->probeBuckets(key, state)) {
    return {};
  }

  auto entry = this->entries.del(this->buckets[state.bucketIndex].entryIndex);
  this->buckets[state.bucketIndex] = Bucket();
  this->bucketLen.setSize(this->bucketLen.size() - 1);

  unsigned int prevBucketIndex = state.bucketIndex;
  unsigned int bucketIndex = this->bucketLen.nextBucketIndex(state.bucketIndex);
  while (true) {
    auto &curBucket = this->buckets[bucketIndex];
    if (!curBucket || curBucket.distanceToInitBucketIndex == 0) {
      break;
    }
    curBucket.distanceToInitBucketIndex--;
    this->buckets[prevBucketIndex] = curBucket;
    this->buckets[bucketIndex] = Bucket();
    prevBucketIndex = bucketIndex;
    bucketIndex = this->bucketLen.nextBucketIndex(bucketIndex);
  }
  return entry;
}

void OrderedMapObject::clear() {
  unsigned int size = this->bucketLen.size();
  for (unsigned int i = 0; i < size; i++) {
    this->buckets[i] = Bucket();
  }
  this->bucketLen.setSize(0);
  this->entries.clear();
}

bool OrderedMapObject::probeBuckets(const DSValue &key, ProbeState &state) const {
  const auto keyHash = hash(key);
  return this->probeBuckets(key, keyHash, state);
}

bool OrderedMapObject::probeBuckets(const DSValue &key, unsigned int keyHash,
                                    ProbeState &state) const {
  unsigned int bucketIndex = this->bucketLen.toBucketIndex(keyHash);
  int dist = 0;
  bool found = false;

  while (true) {
    auto &curBucket = this->buckets[bucketIndex];
    if (!curBucket || dist > curBucket.distanceToInitBucketIndex) {
      break;
    }
    int index = curBucket.entryIndex;
    auto &entry = this->entries[index];
    if (keyHash == entry.getKeyHash() && key.equals(entry.getKey())) {
      found = true;
      break;
    }
    dist++;
    bucketIndex = this->bucketLen.nextBucketIndex(bucketIndex);
  }

  state = {
      .keyHash = keyHash,
      .bucketIndex = bucketIndex,
      .dist = dist,
  };
  return found;
}

void OrderedMapObject::rehash(bool grow) {
  if (grow) {
    this->bucketLen.incExp();
    unsigned int newCap = this->bucketLen.capacity();
    this->buckets = std::make_unique<Bucket[]>(newCap);
  } else {
    unsigned int cap = this->bucketLen.capacity();
    for (unsigned int i = 0; i < cap; i++) {
      this->buckets[i] = Bucket();
    }
  }

  // rehash
  unsigned int size = this->entries.getUsedSize();
  for (unsigned int i = 0; i < size; i++) {
    if (auto &e = this->entries[i]) {
      unsigned int keyHash = e.getKeyHash();
      ProbeState state = {
          .keyHash = keyHash,
          .bucketIndex = this->bucketLen.toBucketIndex(keyHash),
          .dist = 0,
      };
      this->insertEntryIndex(i, state);
    }
  }
}

bool OrderedMapObject::checkIterInvalidation(DSState &state, bool isReplyVar) const {
  if (this->inIteration()) {
    std::string value = "cannot modify map object";
    if (isReplyVar) {
      value += " (reply)";
    }
    value += " during iteration";
    raiseError(state, TYPE::InvalidOperationError, std::move(value));
    return false;
  }
  return true;
}

std::string OrderedMapObject::toString() const {
  std::string ret = "[";
  unsigned int count = 0;
  for (auto &e : this->entries) {
    if (!e) {
      continue;
    }
    if (count++ > 0) {
      ret += ", ";
    }
    ret += e.getKey().toString();
    ret += " : ";
    ret += e.getValue().toString();
  }
  ret += "]";
  return ret;
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool OrderedMapObject::opStr(StrBuilder &builder) const {
  TRY(builder.add("["));
  unsigned int count = 0;
  for (auto &e : this->entries) {
    if (!e) {
      continue;
    }
    if (count++ > 0) {
      TRY(builder.add(", "));
    }
    TRY(e.getKey().opStr(builder));
    TRY(builder.add(" : "));
    TRY(e.getValue().opStr(builder));
  }
  return builder.add("]");
}

DSValue OrderedMapIterObject::next(TypePool &pool) {
  auto &entry = this->nextEntry();
  const auto *keyType = &pool.get(entry.getKey().getTypeID());
  const auto *valueType = &pool.get(entry.getValue().getTypeID());

  auto *type = pool.createTupleType({keyType, valueType}).take();
  auto value = DSValue::create<BaseObject>(cast<TupleType>(*type));
  typeAs<BaseObject>(value)[0] = entry.getKey();
  typeAs<BaseObject>(value)[1] = entry.getValue();
  return value;
}

} // namespace ydsh