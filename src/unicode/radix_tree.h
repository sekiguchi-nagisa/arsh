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

#include <functional>
#include <memory>
#include <unordered_map>

#include "misc/buffer.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

constexpr unsigned int PACKED_RADIX_META_BYTES = 3;
constexpr unsigned int PACKED_RADIX_MAX_STRING_SIZE = (1u << 15) - 1;
constexpr unsigned int PACKED_RADIX_MAX_N_CHILDREN = 1u << 8;

struct PackedRadixChildIter {

  const uint8_t *ptr{nullptr};

  unsigned int offset{0};

  unsigned char childOffsetBytes{0};

  using difference_type = std::ptrdiff_t;

  using value_type = char;

  using reference = char &;

  using pointer = char *;

  using iterator_category = std::forward_iterator_tag;

  PackedRadixChildIter(const uint8_t *ptr, size_t offset, size_t n, unsigned char childOffsetBytes)
      : ptr(ptr), offset(offset + (n * childOffsetBytes)), childOffsetBytes(childOffsetBytes) {}

  unsigned int childOffset() const {
    unsigned int ret = 0;
    for (unsigned int i = 0; i < this->childOffsetBytes; i++) {
      ret <<= 8;
      ret |= this->ptr[this->offset + i];
    }
    return ret;
  }

  auto operator*() const {
    return static_cast<char>(this->ptr[this->childOffset() + PACKED_RADIX_META_BYTES]);
  }

  auto operator-(const PackedRadixChildIter &other) const {
    return (this->offset - other.offset) / this->childOffsetBytes;
  }

  bool operator==(const PackedRadixChildIter &other) const { return this->offset == other.offset; }

  bool operator!=(const PackedRadixChildIter &other) const { return this->offset != other.offset; }

  auto &operator++() {
    this->offset += this->childOffsetBytes;
    return *this;
  }
};

class PackedRadixTree {
private:
  /*
   * `ptr` confirm the following binary format
   *
   * | header (1 byte, child offset bytes (2 or 3)) |
   * | ---- node ---- |
   * | meta (3 bytes, prefix len: 15 bit, n_children: 9 bit (up to 256)) |
   * | prefix (N bytes) | property (1 byte) |
   * | 0-child (3 bytes) | ... | N-child (up to 255) (3 bytes) |
   * | ... next nodes
   */
  const uint8_t *ptr;
  unsigned short longestStringSize;
  unsigned int size;

public:
  PackedRadixTree(unsigned short longestStringSize, const uint8_t *ptr, unsigned int size)
      : ptr(ptr), longestStringSize(longestStringSize), size(size) {}

  unsigned short getLongestStringSize() const { return this->longestStringSize; }

  unsigned char getChildOffsetBytes() const { return this->ptr[0]; }

  uint8_t find(StringRef ref) const {
    const unsigned char childOffsetBytes = this->getChildOffsetBytes();
    for (unsigned int offset = 1; offset < this->size;) {
      const unsigned int meta = static_cast<unsigned int>(this->ptr[offset]) << 16 |
                                static_cast<unsigned int>(this->ptr[offset + 1]) << 8 |
                                this->ptr[offset + 2];
      offset += PACKED_RADIX_META_BYTES;
      const unsigned int prefixLen = meta >> 9;
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
      const unsigned int n = meta & 0x1FF;
      const PackedRadixChildIter begin(this->ptr, offset, 0, childOffsetBytes);
      const PackedRadixChildIter end(this->ptr, offset, n, childOffsetBytes);
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

  unsigned int packedSize(const unsigned char childOffsetBytes) const {
    unsigned int size = 0;
    size += PACKED_RADIX_META_BYTES; // meta (prefix len + n_children)
    size += this->prefix.size();
    size += 1; // property
    size += this->children.size() * childOffsetBytes;
    return size;
  }

  enum class AddStatus : unsigned char {
    OK,
    ADDED,         // already added
    EMPTY,         // empty string
    NONE_PROPERTY, // empty property (0)
    TOO_LARGE,     // too large string
  };

  /**
   *
   * @param seq
   * @param p
   * @return
   * if already found, return false
   * if p is None, return false
   */
  AddStatus add(StringRef seq, uint8_t p);

  uint8_t find(StringRef seq) const;

  size_t longestStringSize() const;

  void iterate(const std::function<bool(StringRef, unsigned char)> &walker) const;

private:
  RadixTree *getOrCreate(char ch);
};

bool serialize(const RadixTree &radixTree, FlexBuffer<uint8_t> &buf);

} // namespace arsh

#endif // ARSH_UNICODE_RADIX_TREE_H
