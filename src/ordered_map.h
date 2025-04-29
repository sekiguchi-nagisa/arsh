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

#ifndef ARSH_ORDERED_MAP_H
#define ARSH_ORDERED_MAP_H

#include "object.h"

namespace arsh {

class OrderedMapKey {
private:
  Union<std::reference_wrapper<const Value>, StringRef> value;

public:
  explicit OrderedMapKey(StringRef key) : value(key) {}

  explicit OrderedMapKey(const Value &key) : value(std::cref(key)) {}

  bool equals(const Value &other) const;

  unsigned int hash(uint64_t seed) const;
};

class OrderedMapEntries {
public:
  class Entry {
  private:
    Value key;
    Value value;

  public:
    Entry() = default;

    void reset(Value &&k, Value &&v) {
      this->key = std::move(k);
      this->value = std::move(v);
    }

    explicit operator bool() const { return static_cast<bool>(this->key); }

    const Value &getKey() const { return this->key; }

    unsigned int getKeyHash() const { return this->key.getMetaData(); }

    const Value &getValue() const { return this->value; }

    Value &refValue() { return this->value; }
  };

private:
  std::unique_ptr<Entry[]> values;

  unsigned int capacity{0};

  unsigned int usedSize{0};

public:
  unsigned int getCapacity() const { return this->capacity; }

  unsigned int getUsedSize() const { return this->usedSize; }

  const Entry &operator[](unsigned int index) const { return this->values[index]; }

  Entry &operator[](unsigned int index) { return this->values[index]; }

  const auto *begin() const { return this->values.get(); }

  const auto *end() const { return this->values.get() + this->usedSize; }

  /**
   *
   * @param key
   * @param value
   * @return
   */
  unsigned int add(Value &&key, Value &&value);

  Entry del(unsigned int index) {
    Entry tmp;
    std::swap(this->values[index], tmp);
    while (this->usedSize > 0 && !this->values[this->usedSize - 1]) {
      this->usedSize--; // pop last deleted entry
    }
    return tmp;
  }

  void clear() {
    while (this->usedSize > 0) {
      this->values[this->usedSize - 1] = Entry();
      this->usedSize--;
    }
  }

  /**
   *
   * @return
   * return last deleted index (before compaction)
   */
  unsigned int compact();
};

// actual hash map implementation (based on robin-hood hashmap)
// see
// (https://www.sebastiansylvan.com/post/robin-hood-hashing-should-be-your-default-hash-table-implementation/)
// (https://codecapsule.com/2013/11/11/robin-hood-hashing/)
// (https://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/)
class OrderedMapObject : public ObjectWithRtti<ObjectKind::OrderedMap> {
private:
  class BucketLen {
  private:
    /**
     * | 5bit (cap exp) | 27bit (size) |
     */
    unsigned int value{0};

  public:
    static constexpr uint64_t MASK_27bit = 0x7FFFFFF; // 2^27 - 1

    explicit BucketLen(unsigned int initCapExp) { this->setCapExp(initCapExp); }

    unsigned int capExp() const { return this->value >> 27; }

    unsigned int capacity() const { return 1 << this->capExp(); }

    unsigned int size() const { return this->value & MASK_27bit; }

    void incExp() {
      unsigned int exp = this->capExp() + 1;
      this->setCapExp(exp);
    }

    void setSize(unsigned int size) {
      unsigned int newValue = this->value & ~MASK_27bit;
      newValue |= MASK_27bit & size;
      this->value = newValue;
    }

    unsigned int toBucketIndex(unsigned int hash) const { return hash & (this->capacity() - 1); }

    unsigned int nextBucketIndex(unsigned int index) const {
      return this->toBucketIndex(index + 1);
    }

    double loadFactor() const {
      unsigned int cap = this->capacity();
      unsigned int size = this->size();
      return static_cast<double>(size) / static_cast<double>(cap);
    }

  private:
    void setCapExp(unsigned int v) {
      unsigned int newValue = this->value & MASK_27bit;
      newValue |= v << 27;
      this->value = newValue;
    }
  };

  struct Bucket {
    int distanceToInitBucketIndex{0};
    int entryIndex{-1};

    explicit operator bool() const { return this->entryIndex > -1; }
  };

  const uint64_t seed;
  int iterCount{0};
  BucketLen bucketLen{2};
  std::unique_ptr<Bucket[]> buckets;
  OrderedMapEntries entries;

public:
  static constexpr uint64_t MAX_SIZE = BucketLen::MASK_27bit; // up to 2^27 - 1

  static constexpr double MAX_LOAD_FACTOR = 0.9;

  OrderedMapObject(const Type &type, uint64_t seed) : ObjectWithRtti(type), seed(seed) {}

  unsigned int size() const { return this->bucketLen.size(); }

  bool inIteration() const { return this->iterCount > 0; }

  void lockIter() { this->iterCount++; }

  void unlockIter() { this->iterCount--; }

  auto &operator[](unsigned int index) { return this->entries[index]; }

  const auto &operator[](unsigned int index) const { return this->entries[index]; }

  const auto &getEntries() const { return this->entries; }

  enum class InsertStatus : unsigned char {
    OK,    // successfully inserted
    NOP,   // do nothing (already inserted)
    LIMIT, // reach map size limit
  };

  /**
   * insert new entry
   * @param key
   * @param value
   * @return
   * if already inserted, return (entry index, false)
   * if insertion failed (reach size limit), return (-1, false)
   * otherwise, return (entry index, true)
   */
  std::pair<int, InsertStatus> insert(const Value &key, Value &&value);

  /**
   * lookup map entry index by key
   * @param key
   * @return
   * if not found, return -1.
   * otherwise, return entry index
   */
  int lookup(const OrderedMapKey &key) const {
    if (this->bucketLen.size() == 0) {
      return -1;
    }
    ProbeState probe; // NOLINT
    if (!this->probeBuckets(key, probe)) {
      return -1;
    }
    return this->buckets[probe.bucketIndex].entryIndex;
  }

  int lookup(const Value &key) const { return this->lookup(OrderedMapKey(key)); }

  /**
   * remove existing entry
   * @param key
   * @return
   * if not found, return empty entry
   * otherwise, return removed entry
   */
  OrderedMapEntries::Entry remove(const Value &key);

  void clear();

  /**
   *
   * @param state
   * @param isReplyVar
   * @return
   * if in iteration, return false
   */
  bool checkIteratorInvalidation(ARState &state, bool isReplyVar = false) const;

  /**
   * insert key-value even if already inserted
   * @param st
   * @param key
   * @param value
   * @return
   * if not found key (newly inserted), return invalid
   * if insertion failed (reach limit), return empty and raise error
   * otherwise, return old value
   */
  [[nodiscard]] Value put(ARState &st, Value &&key, Value &&value);

private:
  struct ProbeState {
    unsigned int keyHash;
    unsigned int bucketIndex;
    int dist;
  };

  bool probeBuckets(const OrderedMapKey &key, ProbeState &probe) const;

  void insertEntryIndex(unsigned int entryIndex, const ProbeState &probe);

  /**
   * @param grow
   * if true, grow capacity before rehash
   * @return
   */
  void rehash(bool grow);
};

class OrderedMapIterObject : public ObjectWithRtti<ObjectKind::OrderedMapIter> {
private:
  ObjPtr<OrderedMapObject> mapObj;
  unsigned int index{0};

public:
  explicit OrderedMapIterObject(ObjPtr<OrderedMapObject> obj)
      : ObjectWithRtti(obj->getTypeID()), mapObj(std::move(obj)) {
    this->mapObj->lockIter();
  }

  ~OrderedMapIterObject() { this->mapObj->unlockIter(); }

  bool hasNext() const { return this->index < this->mapObj->getEntries().getUsedSize(); }

  Value next(TypePool &pool);

  const OrderedMapEntries::Entry &nextEntry() {
    while (!(*this->mapObj)[this->index]) {
      this->index++;
    }
    return (*this->mapObj)[this->index++];
  }
};

} // namespace arsh

#endif // ARSH_ORDERED_MAP_H
