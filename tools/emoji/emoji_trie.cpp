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

#include <cstdint>
#include <list>
#include <vector>

#include "emoji_trie.h"

namespace arsh {

// ############################
// ##     EmojiRadixTree     ##
// ############################

static StringRef findCommonPrefix(StringRef x, StringRef y) {
  const size_t limit = std::min(x.size(), y.size());
  size_t i = 0;
  for (; i < limit; i++) {
    if (x[i] != y[i]) {
      break;
    }
  }
  return x.slice(0, i);
}

bool EmojiRadixTree::add(StringRef seq, EmojiProperty p) {
  if (p == EmojiProperty::None) {
    return false;
  }
  for (auto *tree = this;;) {
    const auto common = findCommonPrefix(tree->prefix, seq);
    seq.removePrefix(common.size());
    if (common.size() == tree->prefix.size()) {
      if (seq.empty()) {
        if (tree->property == EmojiProperty::None) {
          tree->property = p;
          return true;
        }
        break; // already inserted
      }
      if (tree->prefix.empty() && tree->children.empty() && tree->property == EmojiProperty::None) {
        tree->prefix = seq.toString();
        tree->property = p;
        return true;
      }
      tree = tree->getOrCreate(seq[0]);
    } else { // split
      assert(common.size() < tree->prefix.size());
      /*
       * tree:prefix: ABC, tree:prop:10, seq: ABD, common: AB
       * -> tree:prefix:AB, tree:prop:0,
       *    child1:prefix:C, child1:prop:10, child1:children:[..inherit]
       *    child2:prefix:D, child2:prop:p, child2:children:[]
       *
       * tree:prefix: ABC, tree:prop:10, seq: AB, common: AB
       * -> tree:prefix:AB, tree:prop:p,
       *    child1:prefix:C, child1:prop:10, child1:children:[..inherit]
       */
      StringRef childPrefix(tree->prefix);
      childPrefix.removePrefix(common.size());
      assert(!childPrefix.empty());
      auto child = std::make_unique<EmojiRadixTree>(childPrefix, tree->property);
      std::swap(child->children, tree->children);
      char key = child->getPrefix()[0];
      tree->children.emplace(key, std::move(child));
      tree->prefix = common.toString();
      tree->property = EmojiProperty::None;
      if (seq.empty()) {
        tree->property = p;
        return true;
      }
      tree = tree->getOrCreate(seq[0]);
    }
  }
  return false;
}

static EmojiProperty findImpl(const EmojiRadixTree *tree, StringRef seq) {
  while (true) {
    if (!seq.startsWith(tree->getPrefix())) {
      break;
    }
    seq.removePrefix(tree->getPrefix().size());
    if (seq.empty()) {
      if (tree->getProperty() != EmojiProperty::None) { // reach edge
        return tree->getProperty();
      }
      break;
    }
    // visit children
    const auto iter = tree->getChildren().find(seq[0]);
    if (iter != tree->getChildren().end() && iter->first == seq[0]) {
      tree = iter->second.get();
    } else {
      break;
    }
  }
  return EmojiProperty::None;
}

EmojiProperty EmojiRadixTree::find(const StringRef seq) const { return findImpl(this, seq); }

EmojiRadixTree *EmojiRadixTree::getOrCreate(char ch) {
  auto iter = this->children.find(ch);
  if (iter != this->children.end()) {
    return iter->second.get();
  }
  return this->children.emplace(ch, std::make_unique<EmojiRadixTree>()).first->second.get();
}

FlexBuffer<uint8_t> serialize(const EmojiRadixTree &radixTree) {
  /**
   * maintains tree start offset
   * [(tree0, 0), (tree1, offset1)]
   */
  std::list<std::pair<const EmojiRadixTree *, unsigned int>> targets;
  targets.emplace_back(&radixTree, 0);

  FlexBuffer<uint8_t> buf;
  while (targets.size()) {
    const auto &tree = *targets.front().first;
    assert(tree.getPrefix().size() <= UINT8_MAX);
    buf.push_back(tree.getPrefix().size());
    buf.append(reinterpret_cast<const uint8_t *>(tree.getPrefix().c_str()),
               tree.getPrefix().size());
    buf.push_back(toUnderlying(tree.getProperty()));

    // write child offset
    assert(tree.getChildren().size() <= UINT8_MAX);
    buf.push_back(tree.getChildren().size());
    std::pmr::vector<const EmojiRadixTree *> children;
    for (auto &e : tree.getChildren()) {
      assert(!e.second->getPrefix().empty());
      children.push_back(e.second.get());
    }
    std::sort(children.begin(), children.end(),
              [](const EmojiRadixTree *x, const EmojiRadixTree *y) {
                return x->getPrefix()[0] < y->getPrefix()[0];
              });
    for (auto &e : children) {
      targets.emplace_back(e, targets.back().second + targets.back().first->packedSize());
      unsigned int childOffset = targets.back().second;
      assert(childOffset <= ((1u << 8 * RadixChildIter::CHILD_OFFSET_BYTES) - 1));
      for (unsigned int i = 0; i < RadixChildIter::CHILD_OFFSET_BYTES; i++) {
        unsigned int shift = (RadixChildIter::CHILD_OFFSET_BYTES - 1 - i) * 8;
        buf.push_back((childOffset >> shift) & 0xFF);
      }
    }
    targets.pop_front();
  }
  return buf;
}

} // namespace arsh