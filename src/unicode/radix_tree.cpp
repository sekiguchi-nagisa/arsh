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

#include "radix_tree.h"

namespace arsh {

// #######################
// ##     RadixTree     ##
// #######################

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

RadixTree::AddStatus RadixTree::add(StringRef seq, uint8_t p) {
  if (seq.empty()) {
    return AddStatus::EMPTY;
  }
  if (!p) {
    return AddStatus::NONE_PROPERTY;
  }
  if (seq.size() > MAX_STRING_SIZE) {
    return AddStatus::TOO_LARGE;
  }
  for (auto *tree = this;;) {
    const auto common = findCommonPrefix(tree->prefix, seq);
    seq.removePrefix(common.size());
    if (common.size() == tree->prefix.size()) {
      if (seq.empty()) {
        if (!tree->property) {
          tree->property = p;
          return AddStatus::OK;
        }
        break; // already inserted
      }
      if (tree->prefix.empty() && tree->children.empty() && !tree->property) {
        tree->prefix = seq.toString();
        tree->property = p;
        return AddStatus::OK;
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
      auto child = std::make_unique<RadixTree>(childPrefix, tree->property);
      std::swap(child->children, tree->children);
      char key = child->getPrefix()[0];
      tree->children.emplace(key, std::move(child));
      tree->prefix = common.toString();
      tree->property = 0;
      if (seq.empty()) {
        tree->property = p;
        return AddStatus::OK;
      }
      tree = tree->getOrCreate(seq[0]);
    }
  }
  return AddStatus::ADDED;
}

static uint8_t findImpl(const RadixTree *tree, StringRef seq) {
  while (true) {
    if (!seq.startsWith(tree->getPrefix())) {
      break;
    }
    seq.removePrefix(tree->getPrefix().size());
    if (seq.empty()) {
      if (tree->getProperty()) { // reach edge
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
  return 0;
}

uint8_t RadixTree::find(const StringRef seq) const { return findImpl(this, seq); }

static size_t longestStringSizeImpl(const RadixTree &tree) {
  size_t maxChildSize = 0;
  for (auto &e : tree.getChildren()) {
    auto size = longestStringSizeImpl(*e.second);
    maxChildSize = std::max(maxChildSize, size);
  }
  return tree.getPrefix().size() + maxChildSize;
}

size_t RadixTree::longestStringSize() const { return longestStringSizeImpl(*this); }

static bool iterateImpl(std::string &buf, const RadixTree &tree,
                        const std::function<bool(StringRef, unsigned char)> &walker) {
  buf += tree.getPrefix();
  if (auto p = tree.getProperty()) {
    if (!walker(buf, p)) {
      return false; // break iteration
    }
  }
  const auto oldSize = buf.size();
  for (auto &e : tree.getChildren()) {
    buf.resize(oldSize);
    if (!iterateImpl(buf, *e.second, walker)) {
      return false;
    }
  }
  return true;
}

void RadixTree::iterate(const std::function<bool(StringRef, unsigned char)> &walker) const {
  if (walker) {
    std::string buf;
    iterateImpl(buf, *this, walker);
  }
}

RadixTree *RadixTree::getOrCreate(char ch) {
  auto iter = this->children.find(ch);
  if (iter != this->children.end()) {
    return iter->second.get();
  }
  return this->children.emplace(ch, std::make_unique<RadixTree>()).first->second.get();
}

static bool serialize(const RadixTree &radixTree, FlexBuffer<uint8_t> &buf,
                      const unsigned char childOffsetBytes) {
  /**
   * maintains tree start offset
   * [(tree0, 1), (tree1, offset1)]
   */
  std::list<std::pair<const RadixTree *, unsigned int>> targets;
  targets.emplace_back(&radixTree, 1);

  buf.clear();
  buf.push_back(childOffsetBytes);
  while (targets.size()) {
    const auto &tree = *targets.front().first;
    assert(tree.getPrefix().size() <= RadixTree::MAX_STRING_SIZE);
    assert(tree.getChildren().size() <= RadixTree::MAX_N_CHILDREN);
    const unsigned int meta = static_cast<unsigned int>(tree.getPrefix().size()) << 9 |
                              static_cast<unsigned int>(tree.getChildren().size());
    for (unsigned int i = 0; i < PackedRadixChildIter::META_BYTES; i++) {
      unsigned int shift = (PackedRadixChildIter::META_BYTES - 1 - i) * 8;
      buf.push_back((meta >> shift) & 0xFF);
    }
    buf.append(reinterpret_cast<const uint8_t *>(tree.getPrefix().c_str()),
               tree.getPrefix().size());
    buf.push_back(tree.getProperty());

    // write child offset
    std::vector<const RadixTree *> children;
    for (auto &e : tree.getChildren()) {
      assert(!e.second->getPrefix().empty());
      children.push_back(e.second.get());
    }
    std::sort(children.begin(), children.end(), [](const RadixTree *x, const RadixTree *y) {
      return x->getPrefix()[0] < y->getPrefix()[0];
    });
    for (auto &e : children) {
      targets.emplace_back(e, targets.back().second +
                                  targets.back().first->packedSize(childOffsetBytes));
      unsigned int childOffset = targets.back().second;
      if (childOffset > ((1u << 8 * childOffsetBytes) - 1)) {
        return false; // too large tree
      }
      for (unsigned int i = 0; i < childOffsetBytes; i++) {
        unsigned int shift = (childOffsetBytes - 1 - i) * 8;
        buf.push_back((childOffset >> shift) & 0xFF);
      }
    }
    targets.pop_front();
  }
  return true;
}

bool serialize(const RadixTree &radixTree, FlexBuffer<uint8_t> &buf) {
  for (unsigned char childOffsetBytes = 2; childOffsetBytes <= 3; childOffsetBytes++) {
    if (serialize(radixTree, buf, childOffsetBytes)) {
      return true;
    }
  }
  return false;
}

} // namespace arsh