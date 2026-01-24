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

#ifndef ARSH_TOOLS_EMOJI_EMOJI_TRIE_H
#define ARSH_TOOLS_EMOJI_EMOJI_TRIE_H

#include <array>
#include <memory>
#include <unordered_map>

#include <misc/buffer.hpp>
#include <misc/unicode.hpp>
#include <unicode/emoji_seq.hpp>

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

constexpr unsigned int MAX_TEST_CODEPOINT_SIZE = 16;

template <typename... T>
constexpr std::array<int, MAX_TEST_CODEPOINT_SIZE> toArray(T... arg) {
  static_assert(sizeof...(T) <= MAX_TEST_CODEPOINT_SIZE);
  std::array<int, MAX_TEST_CODEPOINT_SIZE> ret = {arg...};
  return ret;
}

struct CodePointArray {
  std::array<int, MAX_TEST_CODEPOINT_SIZE> codePoints;
  unsigned int usedSize;

  template <typename... T>
  constexpr CodePointArray(int first, T &&...remain) // NOLINT
      : codePoints(toArray(first, std::forward<T>(remain)...)), usedSize(sizeof...(T) + 1) {}

  std::string toUTF8() const {
    std::string str;
    for (unsigned int i = 0; i < this->usedSize; i++) {
      int code = this->codePoints[i];
      char buf[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(code, buf);
      str += StringRef(buf, len);
    }
    return str;
  }
};

FlexBuffer<uint8_t> serialize(const EmojiRadixTree &radixTree);

} // namespace arsh

#endif // ARSH_TOOLS_EMOJI_EMOJI_TRIE_H
