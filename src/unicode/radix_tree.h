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

#ifndef ARSH_UNICODE_RADIX_TREE_H
#define ARSH_UNICODE_RADIX_TREE_H

#include <memory>
#include <unordered_map>

#include "misc/buffer.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

struct PackedRadixChildIter {
  static constexpr unsigned int CHILD_OFFSET_BYTES = 2;

  const uint8_t *ptr{nullptr};

  unsigned int offset{0};

  using difference_type = std::ptrdiff_t;

  using value_type = char;

  using reference = char &;

  using pointer = char *;

  using iterator_category = std::forward_iterator_tag;

  PackedRadixChildIter(const uint8_t *ptr, size_t offset, size_t n)
      : ptr(ptr), offset(offset + (n * CHILD_OFFSET_BYTES)) {}

  unsigned int childOffset() const {
    unsigned int ret = 0;
    for (unsigned int i = 0; i < CHILD_OFFSET_BYTES; i++) {
      ret <<= 8;
      ret |= this->ptr[this->offset + i];
    }
    return ret;
  }

  auto operator*() const { return static_cast<char>(this->ptr[this->childOffset() + 1]); }

  auto operator-(const PackedRadixChildIter &other) const {
    return (this->offset - other.offset) / CHILD_OFFSET_BYTES;
  }

  bool operator==(const PackedRadixChildIter &other) const { return this->offset == other.offset; }

  bool operator!=(const PackedRadixChildIter &other) const { return this->offset != other.offset; }

  auto &operator++() {
    this->offset += CHILD_OFFSET_BYTES;
    return *this;
  }
};

class PackedRadixTree {
private:
  /*
   * `ptr` confirm the following binary format
   *
   * | prefix len (1 byte, up to 64 code points) | prefix (N bytes) | property (1 byte) |
   * | n_child (1 byte, up to 256 children) | 1 child (3 bytes) | ... | N child (3 bytes) |
   * | ... next node
   */
  const uint8_t *ptr;
  unsigned int size;

public:
  PackedRadixTree(const uint8_t *ptr, unsigned int size) : ptr(ptr), size(size) {}

  uint8_t find(StringRef ref) const {
    for (unsigned int offset = 0; offset < this->size;) {
      const unsigned int prefixLen = this->ptr[offset++];
      const StringRef prefix(reinterpret_cast<const char *>(this->ptr) + offset, prefixLen);
      if (!ref.startsWith(prefix)) {
        break;
      }
      offset += prefixLen;
      auto property = this->ptr[offset++];
      ref.removePrefix(prefixLen);
      if (ref.empty()) {
        if (property != 0) { // reach edge
          return property;
        }
        break;
      }
      // visit children
      const unsigned int n = this->ptr[offset++];
      const PackedRadixChildIter begin(this->ptr, offset, 0);
      const PackedRadixChildIter end(this->ptr, offset, n);
      const auto iter = std::lower_bound(begin, end, ref[0]);
      if (iter != end && *iter == ref[0]) {
        offset = iter.childOffset();
        assert(offset < this->size);
      } else {
        break;
      }
    }
    return 0;
  }
};

class RadixTree {
private:
  std::string prefix;
  uint8_t property{0};
  std::unordered_map<char, std::unique_ptr<RadixTree>> children;

public:
  RadixTree() = default;

  explicit RadixTree(StringRef prefix, uint8_t property)
      : prefix(prefix.toString()), property(property) {}

  const auto &getPrefix() const { return this->prefix; }

  const auto &getChildren() const { return this->children; }

  const RadixTree *childAt(char ch) const {
    auto iter = this->children.find(ch);
    return iter != this->children.end() ? iter->second.get() : nullptr;
  }

  uint8_t getProperty() const { return this->property; }

  bool empty() const { return this->prefix.empty() && this->children.empty(); }

  unsigned int packedSize() const {
    unsigned int size = 0;
    size += 1; // prefix len
    size += this->prefix.size();
    size += 1; // property
    size += 1; // n_child
    size += this->children.size() * PackedRadixChildIter::CHILD_OFFSET_BYTES;
    return size;
  }

  /**
   *
   * @param seq
   * @param p
   * @return
   * if already found, return false
   * if p is None, return false
   */
  bool add(StringRef seq, uint8_t p);

  uint8_t find(StringRef seq) const;

private:
  RadixTree *getOrCreate(char ch);
};

bool serialize(const RadixTree &radixTree, FlexBuffer<uint8_t> &buf);

} // namespace arsh

#endif // ARSH_UNICODE_RADIX_TREE_H
