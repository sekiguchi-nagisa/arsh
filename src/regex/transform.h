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

#ifndef ARSH_REGEX_TRANSFORM_H
#define ARSH_REGEX_TRANSFORM_H

#include "misc/codepoint_set.hpp"
#include "misc/inlined_stack.hpp"
#include "node.h"
#include "unicode/radix_tree.h"

namespace arsh::regex {

class FoldCharNode : public NodeWithRtti<NodeKind::FoldChar> {
public:
  FoldCharNode(Token token, int codePoint) : NodeWithRtti(token) { this->u32 = codePoint; }

  int getCodePoint() const { return static_cast<int>(this->u32); }
};

class CharSetNode : public NodeWithRtti<NodeKind::CharSet> {
private:
  CodePointSet set;

public:
  CharSetNode(Token token, bool invert, CodePointSet &&set, bool ignoreCase)
      : NodeWithRtti(token), set(std::move(set)) {
    this->u8 = invert ? 1 : 0;
    this->u16 = ignoreCase ? 1 : 0;
  }

  bool isInvert() const { return this->u8 == 1; }

  bool isIgnoreCase() const { return this->u16 == 1; }

  const auto &getSet() const { return this->set; }

  CodePointSet take() { return std::move(this->set); }
};

class StrSetNode : public NodeWithRtti<NodeKind::StrSet> {
private:
  RadixTree radix;
  std::unique_ptr<Node> chars; // for codepoint set (maybe null)

public:
  StrSetNode(Token token, ucp::RGIEmojiSeq emoji, RadixTree &&radix, std::unique_ptr<Node> &&chars,
             bool ignoreCase)
      : NodeWithRtti(token), radix(std::move(radix)), chars(std::move(chars)) {
    if (ignoreCase) {
      setFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
    }
    this->u16 = toUnderlying(emoji);
  }

  bool isIgnoreCase() const {
    return hasFlag(this->getRawEmojiSeq(), ucp::RGIEmojiSeq::CASE_IGNORE);
  }

  ucp::RGIEmojiSeq getEmojiSeq() const {
    auto tmp = this->getRawEmojiSeq();
    unsetFlag(tmp, ucp::RGIEmojiSeq::CASE_IGNORE);
    return tmp;
  }

  bool hasEmoji() const { return !empty(this->getEmojiSeq()); }

  const auto &getRadix() const { return this->radix; }

  const auto &getChars() const { return this->chars; }

private:
  ucp::RGIEmojiSeq getRawEmojiSeq() const { return static_cast<ucp::RGIEmojiSeq>(this->u16); }
};

struct StrSetBuilder {
  CodePointSetBuilder codePoints;
  RadixTree radix;
  ucp::RGIEmojiSeq emoji{ucp::RGIEmojiSeq::None};
  bool emptySeq{false};

  explicit StrSetBuilder(bool ignoreCase) {
    if (ignoreCase) {
      setFlag(this->emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
    }
  }

  bool hasEmoji() const { return hasEmoji(this->emoji); }

  bool inEmoji(StringRef ref) const {
    if (this->hasEmoji()) {
      auto p = ucp::getEmojiTrie().find(ref);
      return p && hasFlag(toUnderlying(this->emoji), p);
    }
    return false;
  }

  void add(ucp::RGIEmojiSeq e) { setFlag(this->emoji, e); }

  void add(int codePoint) { this->codePoints.addRange(codePoint, codePoint); }

  void add(StringRef ref);

  void addRange(int first, int last, bool fold) { this->codePoints.addRange(first, last, fold); }

  void add(StrSetBuilder &&builder);

  void intersect(StrSetBuilder &&builder);

  void sub(StrSetBuilder &&builder);

  static bool hasEmoji(ucp::RGIEmojiSeq emoji) {
    unsetFlag(emoji, ucp::RGIEmojiSeq::CASE_IGNORE);
    return !empty(emoji);
  }
};

/**
 * eliminate non-capture group / modifier group (embed modifier to Node)
 * resolve backref
 * transform CharClass to CharSet/StrSet
 */
class Lowering {
public:
  enum class CaptureState : unsigned char {
    NOT_OPENED,
    NOT_CLOSED,
    CLOSED,
  };

private:
  const Mode mode;
  InlinedStack<Modifier, 4> modifierStack;
  std::vector<bool> directions; // true: forward, false: backward
  const NamedCaptureGroups &namedCaptureGroups;
  std::vector<CaptureState> resolvedCaptures;

public:
  Lowering(Flag flag, unsigned int groupCount, const NamedCaptureGroups &namedCaptureGroups)
      : mode(flag.mode()), namedCaptureGroups(namedCaptureGroups) {
    this->modifierStack.push(flag.modifiers());
    this->resolvedCaptures.resize(groupCount + 1, CaptureState::NOT_OPENED);
    this->directions.push_back(true);
  }

  void operator()(std::unique_ptr<Node> &node) { this->visit(node); }

  void optimize(std::unique_ptr<Node> &node);

private:
  Modifier modifiers() const { return this->modifierStack.back(); }

  bool has(Modifier m) const { return hasFlag(this->modifiers(), m); }

  bool direction() const { return this->directions.back(); }

  void toCodePointSet(ucp::BuilderOrSet builderOrSet, const PropertyNode &node) const;

  void generateStrSet(StrSetBuilder &setBuilder, unsigned int level, const Node &node) const;

  bool needSimpleCaseFolding() const {
    return this->mode == Mode::UNICODE_SET && this->has(Modifier::IGNORE_CASE);
  }

  void mayBeSimpleCaseFolding(CodePointSetBuilder &setBuilder) const {
    if (this->needSimpleCaseFolding()) {
      setBuilder.foldCase();
    }
  }

  void complement(CodePointSetBuilder &setBuilder) const {
    if (this->needSimpleCaseFolding()) {
      setBuilder.complementWithFoldCase();
    } else {
      setBuilder.complement();
    }
  }

  void visit(std::unique_ptr<Node> &node);

  void visit(SeqNode &node);

  std::unique_ptr<Node> tryToGenerateCharSetNode(Token token, CodePointSetBuilder &setBuilder,
                                                 bool invert) const;

  /**
   * resolve backref
   * @param backRef
   * @return
   * if invalid backref, return false
   */
  bool checkBackRef(const BackRefNode &backRef) const;

  std::unique_ptr<Node> generateClassSet(const CharClassNode &node) const;

  std::unique_ptr<Node> generateClassSet(const PropertyNode &node) const;

  void optimizeStrAlt(std::unique_ptr<Node> &node);
};

void transform(SyntaxTree &tree);

} // namespace arsh::regex

#endif // ARSH_REGEX_TRANSFORM_H
