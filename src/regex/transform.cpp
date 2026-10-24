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

#include "transform.h"
#include "misc/unicode.hpp"
#include "unicode/case_fold.h"

namespace arsh::regex {

// ###########################
// ##     StrSetBuilder     ##
// ###########################

void StrSetBuilder::add(StringRef ref) {
  if (!this->inEmoji(ref)) {
    this->radix.add(ref, 1);
  }
}

void StrSetBuilder::add(StrSetBuilder &&builder) {
  this->add(builder.emoji);
  if (builder.emptySeq) {
    this->emptySeq = true;
  }
  this->codePoints.add(builder.codePoints);
  auto walker = [this](StringRef ref, unsigned char p) {
    if (!this->inEmoji(ref)) {
      this->radix.add(ref, p);
    }
    return true;
  };
  builder.radix.iterate(walker);
}

template <typename Func>
constexpr bool cp_iter_requirement_v =
    std::is_same_v<bool, std::invoke_result_t<Func, StringRef, int>>;

template <typename Func, enable_when<cp_iter_requirement_v<Func>> = nullptr>
static void iterateCodes(CodePointSetBuilder &builder, Func func) {
  for (auto [first, last] : builder.toCompactArrayRef()) {
    for (; first <= last; first++) {
      char buf[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(first, buf);
      assert(len);
      if (!func(StringRef(buf, len), first)) {
        return;
      }
    }
  }
}

template <typename Func>
constexpr bool str_iter_requirement_v = std::is_same_v<bool, std::invoke_result_t<Func, StringRef>>;

template <typename Func, enable_when<str_iter_requirement_v<Func>> = nullptr>
static void iterateEmoji(const ucp::RGIEmojiSeq emoji, Func func) {
  if (StrSetBuilder::hasEmoji(emoji)) {
    auto walker = [&](StringRef ref, unsigned char p) {
      if (hasFlag(toUnderlying(emoji), p)) {
        if (!func(ref)) {
          return false;
        }
      }
      return true;
    };
    ucp::getEmojiTrie().iterate(walker);
  }
}

static void mergeEmoji(ucp::RGIEmojiSeq &emoji, RadixTree &radixTree) {
  iterateEmoji(emoji, [&radixTree](StringRef ref) {
    radixTree.add(ref, 1);
    return true;
  });
  emoji = ucp::RGIEmojiSeq::None;
}

static bool findFromEmoji(const ucp::RGIEmojiSeq emoji, const StringRef ref) {
  auto p = ucp::getEmojiTrie().find(ref);
  return p && hasFlag(toUnderlying(emoji), p);
}

static Optional<int> tryToCodePoint(const StringRef ref) {
  int codePoint = -1;
  const char *iter = ref.begin();
  if (unsigned int len = UnicodeUtil::wtf8ToCodePoint(iter, ref.end(), codePoint);
      len && iter + len == ref.end()) {
    return codePoint;
  }
  return {};
}

void StrSetBuilder::intersect(StrSetBuilder &&builder) {
  StrSetBuilder newBuilder(hasFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE));
  newBuilder.emptySeq = this->emptySeq && builder.emptySeq;

  // emoji
  newBuilder.emoji = this->emoji & builder.emoji;
  if (hasFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE)) {
    setFlag(newBuilder.emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  }
  if (this->hasEmoji()) {
    auto walker = [&](StringRef ref, unsigned char) {
      if (findFromEmoji(this->emoji, ref)) {
        newBuilder.radix.add(ref, 1);
      }
      return true;
    };
    builder.radix.iterate(walker);
    iterateCodes(builder.codePoints, [&](StringRef ref, int codePoint) {
      if (findFromEmoji(this->emoji, ref)) {
        newBuilder.codePoints.addRange(codePoint, codePoint);
      }
      return true;
    });
  }

  // radix
  if (!this->radix.empty()) {
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      if (this->radix.find(ref)) {
        newBuilder.radix.add(ref, 1);
      }
      return true;
    });
    auto walker = [&](StringRef ref, unsigned char) {
      if (this->radix.find(ref)) {
        newBuilder.radix.add(ref, 1);
      }
      return true;
    };
    builder.radix.iterate(walker);
    iterateCodes(builder.codePoints, [&](StringRef ref, int codePoint) {
      if (this->radix.find(ref)) {
        newBuilder.codePoints.addRange(codePoint, codePoint);
      }
      return true;
    });
  }

  // codepoints
  if (this->codePoints) {
    auto set = this->codePoints.build();
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue() && set.ref().contains(cp.unwrap())) {
        newBuilder.codePoints.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    });
    auto walker = [&](StringRef ref, unsigned char) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue() && set.ref().contains(cp.unwrap())) {
        newBuilder.codePoints.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    };
    builder.radix.iterate(walker);
    if (builder.codePoints) {
      this->codePoints.intersect(builder.codePoints);
      newBuilder.codePoints.add(this->codePoints);
    }
  }

  *this = std::move(newBuilder);
}

void StrSetBuilder::sub(StrSetBuilder &&builder) {
  if (this->emptySeq == builder.emptySeq && this->emptySeq) {
    this->emptySeq = false;
  }

  // emoji
  const bool ignoreCase = hasFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  unsetFlag(this->emoji, builder.emoji);
  if (ignoreCase) {
    setFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
  }
  if (this->hasEmoji()) {
    auto walker = [&](StringRef ref, unsigned char) {
      if (findFromEmoji(this->emoji, ref)) {
        mergeEmoji(this->emoji, this->radix);
        return false;
      }
      return true;
    };
    builder.radix.iterate(walker);
  }
  if (this->hasEmoji()) {
    iterateCodes(builder.codePoints, [&](StringRef ref, int) {
      if (findFromEmoji(this->emoji, ref)) {
        mergeEmoji(this->emoji, this->radix);
        return false;
      }
      return true;
    });
  }

  // radix
  if (!this->radix.empty()) {
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      this->radix.remove(ref);
      return !this->radix.empty();
    });
  }
  if (!this->radix.empty()) {
    auto walker = [&](StringRef ref, unsigned char) {
      this->radix.remove(ref);
      return !this->radix.empty();
    };
    builder.radix.iterate(walker);
  }
  if (!this->radix.empty()) {
    iterateCodes(builder.codePoints, [&](StringRef ref, int) {
      this->radix.remove(ref);
      return !this->radix.empty();
    });
  }

  // codepoint
  CodePointSetBuilder sub;
  if (this->codePoints) {
    iterateEmoji(builder.emoji, [&](StringRef ref) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue()) {
        sub.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    });
    this->codePoints.sub(sub);
  }
  if (this->codePoints) {
    sub.clear();
    auto walker = [&](StringRef ref, unsigned char) {
      if (auto cp = tryToCodePoint(ref); cp.hasValue()) {
        sub.addRange(cp.unwrap(), cp.unwrap());
      }
      return true;
    };
    builder.radix.iterate(walker);
    this->codePoints.sub(sub);
  }
  if (this->codePoints) {
    this->codePoints.sub(builder.codePoints);
  }
}

// ######################
// ##     Lowering     ##
// ######################

std::unique_ptr<Node> Lowering::tryToGenerateCharSetNode(Token token,
                                                         CodePointSetBuilder &setBuilder,
                                                         const bool invert) const {
  const auto ranges = setBuilder.toCompactArrayRef();
  if (ranges.empty()) {
    if (invert) {
      auto node = std::make_unique<AnyNode>(token);
      node->setModifier(Modifier::DOT_ALL);
      return node;
    }
  }
  if (ranges.size() == 1 && !invert) {
    auto [first, last] = ranges[0];
    if (first == last) { // only contains single code point
      if (this->has(Modifier::IGNORE_CASE)) {
        int fold = doSimpleCaseFolding(first);
        return std::make_unique<FoldCharNode>(token, fold);
      }
      return std::make_unique<CharNode>(token, first);
    }
    if (first == 0 && last == UnicodeUtil::CODE_POINT_MAX) { // Any
      auto node = std::make_unique<AnyNode>(token);
      node->setModifier(Modifier::DOT_ALL);
      return node;
    }
  }
  return std::make_unique<CharSetNode>(token, invert, setBuilder.build(),
                                       this->has(Modifier::IGNORE_CASE));
}

void Lowering::toCodePointSet(ucp::BuilderOrSet builderOrSet, const PropertyNode &node) const {
  switch (node.getNormalizedType()) {
  case PropertyNode::Type::RANGE:
  case PropertyNode::Type::INTERSECT:
  case PropertyNode::Type::SUBTRACT:
  case PropertyNode::Type::NOT_DIGIT:
  case PropertyNode::Type::NOT_WORD:
  case PropertyNode::Type::NOT_SPACE:
  case PropertyNode::Type::NOT_UNICODE:
  case PropertyNode::Type::EMOJI:
  case PropertyNode::Type::GRAPHEME:
    assert(false);
    break; // normally unreachable
  case PropertyNode::Type::DIGIT:
    ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassDigit), builderOrSet);
    break;
  case PropertyNode::Type::WORD: {
    const auto p = this->has(Modifier::IGNORE_CASE) ? ucp::Lone::ESRegexClassExtendWord
                                                    : ucp::Lone::ESRegexClassWord;
    ucp::getPropertySet(ucp::Property::lone(p), builderOrSet);
    break;
  }
  case PropertyNode::Type::SPACE:
    ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassSpace), builderOrSet);
    break;
  case PropertyNode::Type::UNICODE:
    ucp::getPropertySet(node.getUCP(), builderOrSet);
    break;
  }
}

template <typename Func>
constexpr bool range_walker_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Func, int, int>>;

template <typename Func, enable_when<range_walker_requirement_v<Func>> = nullptr>
static bool intoCharRange(const CharClassNode &node, const unsigned int offset, Func func) {
  if (node.getType() == CharClassNode::Type::RANGE && offset + 2 < node.getChars().size()) {
    auto &n1 = *node.getChars()[offset];
    auto &n2 = *node.getChars()[offset + 1];
    auto &n3 = *node.getChars()[offset + 2];
    if (isa<CharNode>(n1) && isProperty(n2, PropertyNode::Type::RANGE) && isa<CharNode>(n3)) {
      int first = cast<CharNode>(n1).getCodePoint();
      int last = cast<CharNode>(n3).getCodePoint();
      func(first, last);
      return true;
    }
  }
  return false;
}

void Lowering::generateStrSet(StrSetBuilder &setBuilder, unsigned int level,
                              const Node &node) const {
  switch (node.getKind()) {
  case NodeKind::Char: {
    int codePoint = cast<CharNode>(node).getCodePoint();
    if (this->needSimpleCaseFolding()) {
      codePoint = doSimpleCaseFolding(codePoint);
    }
    setBuilder.add(codePoint);
    break;
  }
  case NodeKind::FoldChar:
    setBuilder.add(cast<FoldCharNode>(node).getCodePoint());
    break;
  case NodeKind::Property: {
    auto &p = cast<PropertyNode>(node);
    if (p.mayContainString()) {
      assert(p.getNormalizedType() == PropertyNode::Type::EMOJI);
      auto emoji = p.getEmojiSeq();
      if (this->needSimpleCaseFolding()) {
        setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
      }
      setBuilder.add(emoji);
      break;
    }

    const bool useSubBuilder = p.isInvert() || this->has(Modifier::IGNORE_CASE);
    StrSetBuilder subBuilder(this->has(Modifier::IGNORE_CASE));
    StrSetBuilder *builderPtr = useSubBuilder ? &subBuilder : &setBuilder;
    this->toCodePointSet(ucp::BuilderOrSet(builderPtr->codePoints), p);
    if (useSubBuilder) {
      this->mayBeSimpleCaseFolding(subBuilder.codePoints);
      if (p.isInvert()) {
        this->complement(subBuilder.codePoints);
      }
      setBuilder.codePoints.add(subBuilder.codePoints);
    }
    break;
  }
  case NodeKind::CharClass: {
    auto &classNode = cast<CharClassNode>(node);
    const unsigned int size = classNode.getChars().size();
    StrSetBuilder classBuilder(this->has(Modifier::IGNORE_CASE));
    StrSetBuilder *builderPtr = level ? &classBuilder : &setBuilder;
    switch (classNode.getType()) {
    case CharClassNode::Type::UNION:
    case CharClassNode::Type::RANGE: {
      for (unsigned int i = 0; i < size; i++) {
        auto rangeConsumer = [&builderPtr, this](int first, int last) {
          builderPtr->addRange(first, last, this->needSimpleCaseFolding());
        };
        if (intoCharRange(classNode, i, rangeConsumer)) {
          i += 2;
        } else {
          this->generateStrSet(*builderPtr, level + 1, *classNode.getChars()[i]);
        }
      }
      break;
    }
    case CharClassNode::Type::INTERSECT:
    case CharClassNode::Type::SUBTRACT: {
      generateStrSet(*builderPtr, level + 1, *classNode.getChars()[0]);
      for (unsigned int i = 1; i < size; i++) {
        auto &e = *classNode.getChars()[i];
        if (isProperty(e, PropertyNode::Type::INTERSECT) ||
            isProperty(e, PropertyNode::Type::SUBTRACT)) {
          continue;
        }
        StrSetBuilder sub(this->has(Modifier::IGNORE_CASE));
        this->generateStrSet(sub, 0, e);
        if (classNode.getType() == CharClassNode::Type::INTERSECT) {
          builderPtr->intersect(std::move(sub));
        } else {
          assert(classNode.getType() == CharClassNode::Type::SUBTRACT);
          builderPtr->sub(std::move(sub));
        }
      }
      break;
    }
    }
    if (classNode.isInvert() && this->mode == Mode::UNICODE_SET) {
      this->complement(builderPtr->codePoints);
    }
    if (level) {
      setBuilder.add(std::move(classBuilder));
    }
    break;
  }
  case NodeKind::Alt: { // for \q{}, \q{A}, \q{A|B}, \q{AA|B}
    auto &altNode = cast<AltNode>(node);
    std::string buf;
    for (auto &e : altNode.getPatterns()) {
      if (isa<CharNode>(*e) || isa<FoldCharNode>(*e)) {
        this->generateStrSet(setBuilder, level + 1, *e);
        continue;
      }
      if (isa<EmptyNode>(*e)) {
        setBuilder.emptySeq = true;
        continue;
      }
      assert(isa<SeqNode>(*e));
      auto &seqNode = cast<SeqNode>(*e);
      assert(seqNode.getPatterns().size() > 1);
      buf.clear();
      for (auto &c : seqNode.getPatterns()) {
        int codePoint;
        if (isa<CharNode>(*c)) {
          codePoint = cast<CharNode>(*c).getCodePoint();
          if (this->has(Modifier::IGNORE_CASE)) {
            /*
             * always perform case folding regardless of mode for string alt optimization
             */
            codePoint = doSimpleCaseFolding(codePoint);
          }
        } else {
          assert(isa<FoldCharNode>(*c));
          codePoint = cast<FoldCharNode>(*c).getCodePoint();
        }
        char data[4];
        if (unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data)) {
          buf.append(data, len);
        }
      }
      setBuilder.add(buf);
    }
    break;
  }
  default:
    assert(false); // unreachable
    break;
  }
}

static bool isSingleCharClass(const CharClassNode &node) {
  return node.getChars().size() == 1 && !node.isInvert() && !isa<AltNode>(*node.getChars()[0]);
}

static void normalizeStrCharClass(Node &node) {
  if (isa<NestedNode>(node)) {
    normalizeStrCharClass(*cast<NestedNode>(node).getPattern());
    return;
  }
  if (isa<ListNode>(node) && !isa<CharClassNode>(node)) {
    for (auto &e : cast<ListNode>(node).getPatterns()) {
      normalizeStrCharClass(*e);
    }
    return;
  }
  if (!isa<CharClassNode>(node)) {
    return;
  }
  auto &classNode = cast<CharClassNode>(node);
  if (!classNode.mayContainStrings() || classNode.getType() == CharClassNode::Type::INTERSECT ||
      classNode.getType() == CharClassNode::Type::SUBTRACT) {
    return;
  }

  /*
   * move string class to head (emoji, string literal, code points)
   * [A-B\w\p{Basic_Emoji}\q{12|@}\p{RGI_Emoji}あ]
   * => [\p{Basic_Emoji}\p{RGI_Emoji}\q{12|@}A-B\wあ]
   */
  std::stable_sort(
      classNode.refChars().begin(), classNode.refChars().end(),
      [](const std::unique_ptr<Node> &x, const std::unique_ptr<Node> &y) {
        unsigned int xv = isProperty(*x, PropertyNode::Type::EMOJI) ? 0 : isa<AltNode>(*x) ? 1 : 2;
        unsigned int yv = isProperty(*y, PropertyNode::Type::EMOJI) ? 0 : isa<AltNode>(*y) ? 1 : 2;
        return xv < yv;
      });

  for (auto &e : classNode.refChars()) {
    if (isa<CharClassNode>(*e)) {
      normalizeStrCharClass(*e);
    }
  }
}

static bool isCharSeq(const Node &node) {
  if (isa<CharNode>(node)) {
    return true;
  }
  if (!isa<SeqNode>(node)) {
    return false;
  }
  for (auto &e : cast<SeqNode>(node).getPatterns()) {
    if (!isa<CharNode>(*e)) {
      return false;
    }
  }
  return true;
}

static bool isFoldCharSeq(const Node &node) {
  if (isa<FoldCharNode>(node)) {
    return true;
  }
  if (!isa<SeqNode>(node)) {
    return false;
  }
  for (auto &e : cast<SeqNode>(node).getPatterns()) {
    if (!isa<FoldCharNode>(*e)) {
      return false;
    }
  }
  return true;
}

static bool optimizeStrAlt(std::unique_ptr<Node> &node) {
  if (!isa<AltNode>(*node)) {
    return false;
  }
  auto &altNode = cast<AltNode>(*node);
  {
    auto iter = altNode.getPatterns().begin();
    auto end = altNode.getPatterns().end();
    while (iter != end && isa<EmptyNode>(**(end - 1))) {
      --end; // only allow last empty pattern (AA|B|A||)
    }
    using Entry = decltype(*iter);
    if (!std::all_of(iter, end, [](Entry e) { return isCharSeq(*e); }) &&
        !std::all_of(iter, end, [](Entry e) { return isFoldCharSeq(*e); })) {
      return false;
    }
  }
  /**
   * check alt string order
   *
   * (AA|A) -> accept
   * (AA|AB) -> accept
   * (A|AA) -> not accept
   */
  std::unordered_map<int, unsigned int> prefixCodeMap;
  for (auto &e : altNode.getPatterns()) {
    if (isa<EmptyNode>(*e)) {
      continue;
    }
    int codePoint;
    unsigned int len;
    if (isa<CharNode>(*e)) {
      codePoint = cast<CharNode>(*e).getCodePoint();
      len = 1;
    } else if (isa<FoldCharNode>(*e)) {
      codePoint = cast<FoldCharNode>(*e).getCodePoint();
      len = 1;
    } else {
      assert(isa<SeqNode>(*e));
      auto &seqNode = cast<SeqNode>(*e);
      if (auto &first = *seqNode.getPatterns()[0]; isa<CharNode>(first)) {
        codePoint = cast<CharNode>(first).getCodePoint();
      } else {
        assert(isa<FoldCharNode>(first));
        codePoint = cast<FoldCharNode>(first).getCodePoint();
      }
      len = seqNode.getPatterns().size();
    }
    if (auto iter = prefixCodeMap.find(codePoint); iter != prefixCodeMap.end()) {
      if (len <= iter->second) {
        iter->second = len;
      } else {
        return false;
      }
    } else {
      prefixCodeMap.emplace(codePoint, len);
    }
  }
  auto classNode = std::make_unique<CharClassNode>(node->getToken(), false);
  classNode->add(std::move(node));
  classNode->finalize(classNode->getToken());
  node = std::move(classNode);
  return true;
}

std::unique_ptr<Node> Lowering::generateClassSet(const CharClassNode &node) const {
  StrSetBuilder setBuilder(this->has(Modifier::IGNORE_CASE));
  this->generateStrSet(setBuilder, 0, node);
  bool invert = false;
  if (node.isInvert() && this->mode != Mode::UNICODE_SET) {
    invert = true;
  }
  if (this->has(Modifier::IGNORE_CASE)) {
    setBuilder.codePoints.foldCase();
  }
  if (setBuilder.radix.empty() && !setBuilder.hasEmoji()) { // for charset or empty seq (\q{})
    if (setBuilder.emptySeq && !setBuilder.codePoints) {
      return std::make_unique<EmptyNode>(node.getToken().pos); // do nothing
    }
    auto newNode = this->tryToGenerateCharSetNode(node.getToken(), setBuilder.codePoints, invert);
    if (setBuilder.emptySeq) { // [\w\q{}] => (?:[\w] | )
      newNode = std::make_unique<AltNode>(std::move(newNode));
    }
    return newNode;
  }

  assert(node.mayContainStrings());
  std::unique_ptr<Node> chars;
  if (setBuilder.codePoints) {
    chars = this->tryToGenerateCharSetNode(node.getToken(), setBuilder.codePoints, invert);
  }
  return std::make_unique<StrSetNode>(node.getToken(), setBuilder.emoji,
                                      std::move(setBuilder.radix), setBuilder.emptySeq,
                                      std::move(chars), this->has(Modifier::IGNORE_CASE));
}

std::unique_ptr<Node> Lowering::generateClassSet(const PropertyNode &node) const {
  if (node.mayContainString()) {
    assert(node.getNormalizedType() == PropertyNode::Type::EMOJI);
    auto emoji = node.getEmojiSeq();
    if (this->has(Modifier::IGNORE_CASE)) {
      setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
    }
    return std::make_unique<StrSetNode>(node.getToken(), emoji, RadixTree(), false, nullptr,
                                        this->has(Modifier::IGNORE_CASE));
  }
  CodePointSet set;
  CodePointSetBuilder setBuilder;
  const bool useBuilder = node.isInvert() || this->has(Modifier::IGNORE_CASE);
  this->toCodePointSet(useBuilder ? ucp::BuilderOrSet(setBuilder) : ucp::BuilderOrSet(set), node);
  if (useBuilder) {
    this->mayBeSimpleCaseFolding(setBuilder);
    if (node.isInvert()) {
      this->complement(setBuilder);
    }
    if (this->has(Modifier::IGNORE_CASE)) {
      setBuilder.foldCase();
    }
    set = setBuilder.build();
  }
  return std::make_unique<CharSetNode>(node.getToken(), false, std::move(set),
                                       this->has(Modifier::IGNORE_CASE));
}

bool Lowering::checkBackRef(const BackRefNode &node) const {
  if (node.isNamed()) {
    auto *entry = this->namedCaptureGroups.find(node.getName());
    assert(entry);
    if (entry->hasMultipleIndices()) {
      unsigned int closedCount = 0;
      for (unsigned int i = 0; i < entry->getSize(); i++) {
        auto state = this->resolvedCaptures[(*entry)[i]];
        if (state == CaptureState::CLOSED) {
          closedCount++;
        } else if (state == CaptureState::NOT_CLOSED) {
          return false; // refer to its own group, ex. (?:(?<A>.)|(?<A>\k<A>))
        }
      }
      if (!closedCount) {
        return false;
      }
    } else if (this->resolvedCaptures[entry->getIndex()] != CaptureState::CLOSED) {
      return false; // invalid capture, do nothing
    }
  } else if (this->resolvedCaptures[node.getIndex()] != CaptureState::CLOSED) {
    return false; // invalid capture, do nothing
  }
  return true;
}

void Lowering::visit(std::unique_ptr<Node> &node) {
  switch (node->getKind()) {
  case NodeKind::Empty:
    break;
  case NodeKind::Any:
    cast<AnyNode>(*node).setModifier(this->modifiers());
    break;
  case NodeKind::Char: {
    const int codePoint = cast<CharNode>(*node).getCodePoint();
    int fold = codePoint;
    if (this->has(Modifier::IGNORE_CASE)) {
      fold = doSimpleCaseFolding(codePoint);
      node = std::make_unique<FoldCharNode>(node->getToken(), fold);
    }
    break;
  }
  case NodeKind::CharClass:
    normalizeStrCharClass(*node);
    if (auto &charClass = cast<CharClassNode>(*node); isSingleCharClass(charClass)) {
      node = std::move(charClass.refChars()[0]);
      this->visit(node);
    } else {
      node = this->generateClassSet(charClass);
    }
    break;
  case NodeKind::Property:
    if (auto &property = cast<PropertyNode>(*node);
        property.getNormalizedType() != PropertyNode::Type::GRAPHEME) {
      node = this->generateClassSet(property);
    }
    break;
  case NodeKind::Boundary:
    cast<BoundaryNode>(*node).setModifier(this->modifiers());
    break;
  case NodeKind::BackRef:
    if (auto &backRef = cast<BackRefNode>(*node); this->checkBackRef(backRef)) {
      backRef.setIgnoreCase(this->has(Modifier::IGNORE_CASE));
    } else {
      node = std::make_unique<EmptyNode>(node->getToken().pos); // remove
    }
    break;
  case NodeKind::Seq:
    this->visit(cast<SeqNode>(*node));
    break;
  case NodeKind::Alt:
    for (auto &e : cast<ListNode>(*node).refPatterns()) {
      this->visit(e);
    }
    if (this->direction() && optimizeStrAlt(node)) {
      this->visit(node);
    }
    break;
  case NodeKind::LookAround: {
    auto &n = cast<LookAroundNode>(*node);
    this->directions.push_back(n.isLookAhead());
    this->visit(n.refPattern());
    this->directions.pop_back();
    break;
  }
  case NodeKind::Repeat:
    this->visit(cast<NestedNode>(*node).refPattern());
    break;
  case NodeKind::Group:
    switch (auto &group = cast<GroupNode>(*node); group.getType()) {
    case GroupNode::Type::CAPTURE:
      assert(group.getGroupIndex());
      this->resolvedCaptures[group.getGroupIndex()] = CaptureState::NOT_CLOSED;
      this->visit(group.refPattern());
      this->resolvedCaptures[group.getGroupIndex()] = CaptureState::CLOSED;
      break;
    case GroupNode::Type::NON_CAPTURE:
      node = std::move(group.refPattern());
      this->visit(node);
      break;
    case GroupNode::Type::MODIFIER: {
      auto newModifier = this->modifiers();
      if (auto set = group.getSetModifiers(); set != Modifier::NONE) {
        setFlag(newModifier, set);
      }
      if (auto unset = group.getUnsetModifiers(); unset != Modifier::NONE) {
        unsetFlag(newModifier, unset);
      }
      this->modifierStack.push(newModifier);
      node = std::move(group.refPattern());
      this->visit(node);
      this->modifierStack.pop();
      break;
    }
    }
    break;
  case NodeKind::FoldChar:
  case NodeKind::CharSet:
  case NodeKind::StrSet:
    break; // unreachable
  }
}

void Lowering::visit(SeqNode &node) {
  if (this->direction()) {
    auto iter = node.refPatterns().begin();
    auto end = node.refPatterns().end();
    for (; iter != end; ++iter) {
      this->visit(*iter);
    }
  } else {
    auto iter = node.refPatterns().rbegin();
    auto end = node.refPatterns().rend();
    for (; iter != end; ++iter) {
      this->visit(*iter);
    }
  }
}

void transform(SyntaxTree &tree) {
  Lowering(tree.getFlag(), tree.getCaptureGroupCount(),
           tree.getNamedCaptureGroups())(tree.refPattern());
}

} // namespace arsh::regex