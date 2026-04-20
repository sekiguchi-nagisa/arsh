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
#include "unicode/packed_radix_tree.hpp"

namespace arsh {

class EmojiRadixTree {
private:
  std::string prefix;
  RGIEmojiSeq property{RGIEmojiSeq::None};
  std::unordered_map<char, std::unique_ptr<EmojiRadixTree>> children;

public:
  EmojiRadixTree() = default;

  explicit EmojiRadixTree(StringRef prefix, RGIEmojiSeq property)
      : prefix(prefix.toString()), property(property) {}

  const auto &getPrefix() const { return this->prefix; }

  const auto &getChildren() const { return this->children; }

  const EmojiRadixTree *childAt(char ch) const {
    auto iter = this->children.find(ch);
    return iter != this->children.end() ? iter->second.get() : nullptr;
  }

  RGIEmojiSeq getProperty() const { return this->property; }

  bool empty() const { return this->prefix.empty() && this->children.empty(); }

  unsigned int packedSize() const {
    unsigned int size = 0;
    size += 1; // prefix len
    size += this->prefix.size();
    size += 1; // property
    size += 1; // n_child
    size += this->children.size() * RadixChildIter::CHILD_OFFSET_BYTES;
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
  bool add(StringRef seq, RGIEmojiSeq p);

  RGIEmojiSeq find(StringRef seq) const;

private:
  EmojiRadixTree *getOrCreate(char ch);
};

bool serialize(const EmojiRadixTree &radixTree, FlexBuffer<uint8_t> &buf);

} // namespace arsh

#endif // ARSH_UNICODE_RADIX_TREE_H
