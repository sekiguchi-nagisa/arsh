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
#include "../external/rapidhash/rapidhash.h"
#include "core.h"
#include "misc/num_util.hpp"
#include "object_util.h"
#include "type_pool.h"

namespace arsh {

// ###########################
// ##     OrderedMapKey     ##
// ###########################

bool OrderedMapKey::equals(const Value &other) const {
  if (is<StringRef>(this->value)) {
    assert(other.hasStrRef());
    return get<StringRef>(this->value) == other.asStrRef();
  }
  assert(is<std::reference_wrapper<const Value>>(this->value));
  auto &v = get<std::reference_wrapper<const Value>>(this->value).get();
  return Equality()(v, other); // never overflow
}

static uint64_t simpleHash(uint64_t value) {
  uint64_t ret = UINT64_MAX;
  rapid_mum(&value, &ret); // generate up to UINT64_MAX hash value
  return ret;
}

// rapidhash equivalent to `wyhash64` see. https://github.com/wangyi-fudan/wyhash
// a useful 64bit-64bit mix function to produce deterministic pseudo random numbers that can pass
// BigCrush and PractRand
static uint64_t rapidhash64(uint64_t A, uint64_t B) {
  A ^= 0x2d358dccaa6c78a5ull;
  B ^= 0x8bb84b93962eacc9ull;
  rapid_mum(&A, &B);
  return rapid_mix(A ^ 0x2d358dccaa6c78a5ull, B ^ 0x8bb84b93962eacc9ull);
}

uint64_t OrderedMapKey::hash(uint64_t seed) const {
  bool isStr = false;
  uint64_t u64 = 0;
  const void *ptr = nullptr;
  size_t size = 0;

  if (is<StringRef>(this->value)) {
    auto &ref = get<StringRef>(this->value);
    ptr = ref.data();
    size = ref.size();
    isStr = true;
  } else {
    assert(is<std::reference_wrapper<const Value>>(this->value));
    auto &v = get<std::reference_wrapper<const Value>>(this->value).get();
    switch (v.kind()) {
    case ValueKind::BOOL:
      u64 = v.asBool() ? 1 : 0;
      break;
    case ValueKind::SIG:
      u64 = v.asSig();
      break;
    case ValueKind::INT:
      u64 = v.asInt();
      break;
    case ValueKind::FLOAT:
      u64 = doubleToBits(v.asFloat());
      break;
    default:
      if (v.hasStrRef()) {
        auto ref = v.asStrRef();
        ptr = ref.data();
        size = ref.size();
        isStr = true;
      } else if (v.kind() == ValueKind::OBJECT) {
        if (v.get()->getKind() == ObjectKind::Base) {
          u64 = typeAs<BaseObject>(v).getHash();
        } else {
          u64 = reinterpret_cast<uintptr_t>(v.get());
        }
      }
      break;
    }
  }
  return isStr ? rapidhash_withSeed(ptr, size, seed) : simpleHash(u64);
}

uint64_t hashRange(const Value *begin, const Value *const end) {
  uint64_t hash = 0;
  for (; begin != end; ++begin) {
    hash = rapidhash64(hash, OrderedMapKey(*begin).hash(0));
  }
  return hash;
}

// ###############################
// ##     OrderedMapEntries     ##
// ###############################

unsigned int OrderedMapEntries::add(Value &&key, Value &&value) {
  if (this->usedSize == this->capacity) {
    unsigned int newCap = this->capacity;
    newCap += (newCap >> 1);
    if (unlikely(newCap == 0)) {
      newCap = 4;
    }
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

std::pair<int, OrderedMapObject::InsertStatus> OrderedMapObject::insert(const Value &key,
                                                                        Value &&value) {
  if (unlikely(!this->buckets)) {
    this->buckets = std::make_unique<Bucket[]>(this->bucketLen.capacity());
  }

  ProbeState probe; // NOLINT
  if (this->probeBuckets(OrderedMapKey(key), probe)) {
    int index = this->buckets[probe.bucketIndex].entryIndex;
    assert(index != -1);
    return {index, InsertStatus::NOP};
  }

  if (unlikely(this->size() == MAX_SIZE)) {
    return {-1, InsertStatus::LIMIT};
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
    probe.bucketIndex = this->bucketLen.toBucketIndex(probe.keyHash);
    probe.dist = 0;
  }
  const unsigned int entryIndex =
      this->entries.add(key.withMetaData(probe.keyHash), std::move(value));

  // add entry index to buckets
  this->insertEntryIndex(entryIndex, probe);

  this->bucketLen.setSize(this->bucketLen.size() + 1);
  return {static_cast<int>(entryIndex), InsertStatus::OK};
}

void OrderedMapObject::insertEntryIndex(unsigned int entryIndex, const ProbeState &probe) {
  unsigned int bucketIndex = probe.bucketIndex;
  int dist = probe.dist;

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

OrderedMapEntries::Entry OrderedMapObject::remove(const Value &key) {
  if (this->bucketLen.size() == 0) {
    return {};
  }

  ProbeState probe; // NOLINT
  if (!this->probeBuckets(OrderedMapKey(key), probe)) {
    return {};
  }

  auto entry = this->entries.del(this->buckets[probe.bucketIndex].entryIndex);
  this->buckets[probe.bucketIndex] = Bucket();
  this->bucketLen.setSize(this->bucketLen.size() - 1);

  unsigned int prevBucketIndex = probe.bucketIndex;
  unsigned int bucketIndex = this->bucketLen.nextBucketIndex(probe.bucketIndex);
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
  if (this->bucketLen.size() == 0) {
    return;
  }
  unsigned int size = this->bucketLen.capacity();
  for (unsigned int i = 0; i < size; i++) {
    this->buckets[i] = Bucket();
  }
  this->bucketLen.setSize(0);
  this->entries.clear();
}

bool OrderedMapObject::probeBuckets(const OrderedMapKey &key, ProbeState &probe) const {
  const auto keyHash = static_cast<unsigned int>(key.hash(this->seed)); // only use 32bit
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

  probe = {
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
      ProbeState probe = {
          .keyHash = keyHash,
          .bucketIndex = this->bucketLen.toBucketIndex(keyHash),
          .dist = 0,
      };
      this->insertEntryIndex(i, probe);
    }
  }
}

bool OrderedMapObject::checkIteratorInvalidation(ARState &state, bool isReplyVar) const {
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

Value OrderedMapObject::put(ARState &st, Value &&key, Value &&value) {
  switch (auto [index, s] = this->insert(key, Value(value)); s) {
  case InsertStatus::OK:
    return Value::createInvalid();
  case InsertStatus::NOP:
    std::swap((*this)[index].refValue(), value);
    return std::move(value);
  case InsertStatus::LIMIT:
    raiseError(st, TYPE::OutOfRangeError, ERROR_MAP_LIMIT);
    break;
  }
  return {};
}

Value OrderedMapIterObject::next(TypePool &pool) {
  auto &entry = this->nextEntry();
  const auto *keyType = &pool.get(entry.getKey().getTypeID());
  const auto *valueType = &pool.get(entry.getValue().getTypeID());

  auto *type = pool.createTupleType({keyType, valueType}).take();
  auto value = Value::create<BaseObject>(cast<TupleType>(*type));
  typeAs<BaseObject>(value)[0] = entry.getKey();
  typeAs<BaseObject>(value)[1] = entry.getValue();
  return value;
}

} // namespace arsh