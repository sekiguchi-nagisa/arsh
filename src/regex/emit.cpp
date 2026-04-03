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

#include "emit.h"

#include "misc/unicode.hpp"
#include "unicode/case_fold.h"

namespace arsh::regex {

// #####################
// ##     CodeGen     ##
// #####################

Optional<Regex> CodeGen::operator()(SyntaxTree &&tree) {
  // prepare
  this->mode = tree.getFlag().mode();
  this->err.clear();
  this->modifierStack.clear();
  this->modifierStack.push(tree.getFlag().modifiers());
  this->directions.clear();
  this->directions.push_back(true);

  if (!this->generate(*tree.getPattern())) {
    return {};
  }

  // finalize
  this->builder.emit<MatchIns>();
  auto flag = tree.getFlag();
  auto count = tree.getCaptureGroupCount();
  auto loopCount = tree.getLoopCount();
  return Regex(flag, loopCount, std::move(this->builder).build(), std::move(this->matchers),
               std::move(tree).takeNamedCaptureGroups(), count);
}

static const char *toString(NodeKind kind) { // TODO: remove
  switch (kind) {
#define GEN_CASE(E)                                                                                \
  case NodeKind::E:                                                                                \
    return #E;
    EACH_RE_NODE_KIND(GEN_CASE)
#undef GEN_CASE
  default:
    break;
  }
  return "";
}

void CodeGen::todo(const Node &node, const char *str) {
  this->err += "[todo] ";
  this->err += toString(node.getKind());
  if (str) {
    this->err += ": ";
    this->err += str;
  }
}

int CodeGen::mayBeSimpleCaseFolding(int codePoint) const {
  if (this->needSimpleCaseFolding()) {
    return doSimpleCaseFolding(codePoint);
  }
  return codePoint;
}

void CodeGen::mayBeSimpleCaseFolding(CodePointSetBuilder &setBuilder) const {
  if (this->needSimpleCaseFolding()) {
    setBuilder.foldCase();
  }
}

void CodeGen::complement(CodePointSetBuilder &setBuilder) const {
  if (this->needSimpleCaseFolding()) {
    CodePointSetBuilder newBuilder;
    newBuilder.addRange(0, UnicodeUtil::CODE_POINT_MAX);
    newBuilder.foldCase();
    newBuilder.sub(setBuilder.build().ref());
    setBuilder = std::move(newBuilder);
  } else {
    setBuilder.complement();
  }
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool CodeGen::generate(const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Empty:
    this->builder.emit<NopIns>();
    break;
  case NodeKind::Any:
    if (this->inLookBehind()) {
      this->builder.emit<LBAnyIns>(this->has(Modifier::DOT_ALL));
    } else if (this->has(Modifier::DOT_ALL)) {
      this->builder.emit<AnyIns>();
    } else {
      this->builder.emit<AnyExceptNLIns>();
    }
    break;
  case NodeKind::Char:
    if (this->inLookBehind()) {
      int codePoint = cast<CharNode>(node).getCodePoint();
      if (this->has(Modifier::IGNORE_CASE)) {
        codePoint = doSimpleCaseFolding(codePoint);
      }
      this->builder.emit<LBCharIns>(codePoint, this->has(Modifier::IGNORE_CASE));
    } else if (this->has(Modifier::IGNORE_CASE)) {
      this->builder.emit<ICharIns>(doSimpleCaseFolding(cast<CharNode>(node).getCodePoint()));
    } else {
      this->builder.emit<CharIns>(cast<CharNode>(node).getCodePoint());
    }
    break;
  case NodeKind::CharClass:
    return this->generateCharClass(cast<CharClassNode>(node));
  case NodeKind::Property:
    return this->generateProperty(cast<PropertyNode>(node));
  case NodeKind::Boundary: {
    auto t = cast<BoundaryNode>(node).getType();
    switch (t) {
    case BoundaryNode::Type::START:
      this->builder.emit<StartIns>(this->has(Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::END:
      this->builder.emit<EndIns>(this->has(Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::WORD:
    case BoundaryNode::Type::NOT_WORD:
      if (this->has(Modifier::IGNORE_CASE)) {
        this->builder.emit<IWordIns>(t == BoundaryNode::Type::NOT_WORD);
      } else {
        this->builder.emit<WordIns>(t == BoundaryNode::Type::NOT_WORD);
      }
      return true;
    }
    break;
  }
  case NodeKind::BackRef: {
    auto &backRefNode = cast<BackRefNode>(node);
    if (this->inLookBehind()) {
      this->builder.emit<LBBackRefIns>(backRefNode.getIndex(), backRefNode.isNamed(),
                                       this->has(Modifier::IGNORE_CASE));
    } else if (this->has(Modifier::IGNORE_CASE)) {
      this->builder.emit<IBackRefIns>(backRefNode.getIndex(), backRefNode.isNamed());
    } else {
      this->builder.emit<BackRefIns>(backRefNode.getIndex(), backRefNode.isNamed());
    }
    break;
  }
  case NodeKind::Repeat:
    return this->generateRepeat(cast<RepeatNode>(node));
  case NodeKind::Seq:
    return this->generateSeq(cast<SeqNode>(node));
  case NodeKind::Alt:
    return this->generateAlt(cast<AltNode>(node));
  case NodeKind::LookAround:
    return this->generateLookAround(cast<LookAroundNode>(node));
  case NodeKind::Group:
    return this->generateGroup(cast<GroupNode>(node));
  }
  return true;
}

bool CodeGen::generateSeq(const SeqNode &node) {
  if (this->direction()) {
    for (auto &e : node.getPatterns()) {
      TRY(this->generate(*e));
    }
  } else {
    const auto end = node.getPatterns().rend();
    for (auto iter = node.getPatterns().rbegin(); iter != end; ++iter) {
      TRY(this->generate(**iter));
    }
  }
  return true;
}

bool CodeGen::generateAlt(const AltNode &node) {
  const unsigned int size = node.getPatterns().size();
  std::vector<InstructionBuilder::ReservedPoint> jumps;
  for (unsigned int i = 0; i < size; i++) {
    if (i == size - 1) {
      TRY(this->generate(*node.getPatterns()[i]));
      continue;
    }
    auto point = this->builder.emitReservedPoint<AltIns>();
    TRY(this->generate(*node.getPatterns()[i]));
    jumps.push_back(this->builder.emitReservedPoint<JumpIns>());
    auto addr = this->builder.currentAddr();
    this->builder.emitAt<AltIns>(point, addr);
  }
  unsigned int addr = this->builder.currentAddr();
  for (auto &e : jumps) {
    this->builder.emitAt<JumpIns>(e, addr);
  }
  return true;
}

bool CodeGen::generateGroup(const GroupNode &node) {
  switch (node.getType()) {
  case GroupNode::Type::CAPTURE:
    assert(node.getGroupIndex());
    this->builder.emit<BeginCaptureIns>(node.getGroupIndex());
    TRY(this->generate(*node.getPattern()));
    if (this->inLookBehind()) {
      this->builder.emit<LBEndCaptureIns>(node.getGroupIndex());
    } else {
      this->builder.emit<EndCaptureIns>(node.getGroupIndex());
    }
    return true;
  case GroupNode::Type::NON_CAPTURE:
    return this->generate(*node.getPattern());
  case GroupNode::Type::MODIFIER: {
    auto newModifier = this->modifiers();
    if (auto set = node.getSetModifiers(); set != Modifier::NONE) {
      setFlag(newModifier, set);
    }
    if (auto unset = node.getUnsetModifiers(); unset != Modifier::NONE) {
      unsetFlag(newModifier, unset);
    }
    this->modifierStack.push(newModifier);
    TRY(this->generate(*node.getPattern()));
    this->modifierStack.pop();
    return true;
  }
  }
  return true;
}

template <typename Func>
constexpr bool range_walker_requirement_v =
    std::is_same_v<void, std::invoke_result_t<Func, int, int>>;

template <typename Func, enable_when<range_walker_requirement_v<Func>> = nullptr>
static bool intoCharRange(const CharClassNode &node, unsigned int offset, Func func) {
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

static bool isAsciiSet(const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Char: {
    const auto codePoint = cast<CharNode>(node).getCodePoint();
    return Matcher::withinAsciiSet(codePoint);
  }
  case NodeKind::Property: {
    auto &propertyNode = cast<PropertyNode>(node);
    return propertyNode.onlyAscii();
  }
  case NodeKind::CharClass: {
    auto &classNode = cast<CharClassNode>(node);
    for (auto &e : classNode.getChars()) {
      if (isProperty(*e, PropertyNode::Type::RANGE)) {
        continue;
      }
      if (!isAsciiSet(*e)) {
        return false;
      }
    }
    return !classNode.isInvert();
  }
  default:
    return false;
  }
}

static void appendToAsciiSet(AsciiSet &set, const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Char: {
    auto &charNode = cast<CharNode>(node);
    assert(Matcher::withinAsciiSet(charNode.getCodePoint()));
    set.add(charNode.getCodePoint());
    break;
  }
  case NodeKind::Property: {
    auto &propertyNode = cast<PropertyNode>(node);
    assert(propertyNode.onlyAscii());
    if (propertyNode.getType() == PropertyNode::Type::DIGIT) {
      for (char i = 0; i < 10; i++) {
        set.add('0' + i);
      }
    } else {
      assert(propertyNode.getType() == PropertyNode::Type::WORD);
      for (char i = 0; i < 10; i++) {
        set.add('0' + i);
      }
      for (char i = 'a'; i <= 'z'; i++) {
        set.add(i);
        set.add('A' + (i - 'a'));
      }
      set.add('_');
    }
    break;
  }
  case NodeKind::CharClass: {
    auto &classNode = cast<CharClassNode>(node);
    assert(!classNode.isInvert());
    assert(classNode.getType() == CharClassNode::Type::UNION ||
           classNode.getType() == CharClassNode::Type::RANGE);
    const unsigned int size = classNode.getChars().size();
    for (unsigned int i = 0; i < size; i++) {
      auto rangeConsumer = [&set](int first, int last) {
        assert(Matcher::withinAsciiSet(first));
        assert(Matcher::withinAsciiSet(last));
        for (int c = first; c <= last; c++) {
          set.add(c);
        }
      };
      if (intoCharRange(classNode, i, rangeConsumer)) {
        i += 2;
      } else {
        auto &e = classNode.getChars()[i];
        appendToAsciiSet(set, *e);
      }
    }
    break;
  }
  default:
    assert(false);
    break; // unreachable
  }
}

bool CodeGen::toCodePointSet(ucp::BuilderOrSet builderOrSet, const PropertyNode &node) const {
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
    return ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassDigit), builderOrSet);
  case PropertyNode::Type::WORD: {
    const auto p = this->has(Modifier::IGNORE_CASE) ? ucp::Lone::ESRegexClassExtendWord
                                                    : ucp::Lone::ESRegexClassWord;
    return ucp::getPropertySet(ucp::Property::lone(p), builderOrSet);
  }
  case PropertyNode::Type::SPACE:
    return ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassSpace), builderOrSet);
  case PropertyNode::Type::UNICODE:
    return ucp::getPropertySet(node.getUCP(), builderOrSet);
  }
  return true;
}

static AsciiSet foldCase(const AsciiSet set) {
  AsciiSet newSet;
  for (int i = 0; i < 128; i++) {
    int code = i;
    if (set.contains(code)) {
      code = doSimpleCaseFolding(code);
      assert(code >= 0 && code < 128);
      newSet.add(code);
    }
  }
  return newSet;
}

bool CodeGen::generateProperty(const PropertyNode &node) {
  if (node.onlyAscii()) {
    AsciiSet set;
    appendToAsciiSet(set, node);
    if (this->has(Modifier::IGNORE_CASE)) {
      set = foldCase(set);
    }
    if (auto index = this->emitMatcher(Matcher(set)); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), false);
      return true;
    }
  } else if (node.mayContainString()) {
    if (node.getNormalizedType() == PropertyNode::Type::GRAPHEME) {
      this->builder.emit<GraphemeIns>();
      return true;
    }
    if (this->inLookBehind()) {
      this->todo(node, "look-behind");
      return false;
    }
    if (this->has(Modifier::IGNORE_CASE)) {
      this->todo(node, "ignore-case");
      return false;
    }
    this->todo(node, "support emoji seq");
    return false;
  } else {
    CodePointSet set;
    CodePointSetBuilder setBuilder;
    const bool useBuilder = node.isInvert() || this->has(Modifier::IGNORE_CASE);
    const bool r = this->toCodePointSet(
        useBuilder ? ucp::BuilderOrSet(setBuilder) : ucp::BuilderOrSet(set), node);
    assert(r);
    static_cast<void>(r);
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
    if (auto index = this->emitMatcher(Matcher(std::move(set))); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), false);
      return true;
    }
  }
  return false;
}

void CodeGen::appendToCodePointSet(CodePointSetBuilder &setBuilder, const unsigned int level,
                                   const Node &node) const {
  switch (node.getKind()) {
  case NodeKind::Char: {
    int codePoint = cast<CharNode>(node).getCodePoint();
    codePoint = this->mayBeSimpleCaseFolding(codePoint);
    setBuilder.addRange(codePoint, codePoint);
    break;
  }
  case NodeKind::Property: {
    auto &p = cast<PropertyNode>(node);
    const bool useSubBuilder = p.isInvert() || this->has(Modifier::IGNORE_CASE);
    CodePointSetBuilder subBuilder;
    CodePointSetBuilder *builderPtr = useSubBuilder ? &subBuilder : &setBuilder;
    const bool r = this->toCodePointSet(ucp::BuilderOrSet(*builderPtr), p);
    assert(r);
    static_cast<void>(r);
    if (useSubBuilder) {
      this->mayBeSimpleCaseFolding(subBuilder);
      if (p.isInvert()) {
        this->complement(subBuilder);
      }
      setBuilder.add(subBuilder);
    }
    break;
  }
  case NodeKind::CharClass: {
    auto &classNode = cast<CharClassNode>(node);
    assert(!classNode.mayContainStrings());
    const unsigned int size = classNode.getChars().size();
    CodePointSetBuilder classBuilder;
    CodePointSetBuilder *builderPtr = level ? &classBuilder : &setBuilder;
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
          this->appendToCodePointSet(*builderPtr, level + 1, *classNode.getChars()[i]);
        }
      }
      break;
    }
    case CharClassNode::Type::INTERSECT:
    case CharClassNode::Type::SUBTRACT: {
      appendToCodePointSet(*builderPtr, level + 1, *classNode.getChars()[0]);
      for (unsigned int i = 1; i < size; i++) {
        auto &e = *classNode.getChars()[i];
        if (isProperty(e, PropertyNode::Type::INTERSECT) ||
            isProperty(e, PropertyNode::Type::SUBTRACT)) {
          continue;
        }
        CodePointSetBuilder sub;
        this->appendToCodePointSet(sub, 0, e);
        auto set = sub.build();
        if (classNode.getType() == CharClassNode::Type::INTERSECT) {
          builderPtr->intersect(set.ref());
        } else {
          assert(classNode.getType() == CharClassNode::Type::SUBTRACT);
          builderPtr->sub(set.ref());
        }
      }
      break;
    }
    }
    if (classNode.isInvert() && this->mode == Mode::UNICODE_SET) {
      this->complement(*builderPtr);
    }
    if (level) {
      setBuilder.add(classBuilder);
    }
    break;
  }
  case NodeKind::Alt: { // for \q{A}, \q{A|B}
    auto &altNode = cast<AltNode>(node);
    for (auto &e : altNode.getPatterns()) {
      assert(isa<CharNode>(*e));
      this->appendToCodePointSet(setBuilder, level + 1, *e);
    }
    break;
  }
  default:
    assert(false); // unreachable
    break;
  }
}

static bool isSingleCharClass(const CharClassNode &node) {
  if (node.getChars().size() == 1 && !node.isInvert()) {
    if (auto *altNode = checked_cast<AltNode>(node.getChars()[0].get())) {
      if (altNode->getPatterns().size() == 1 && isa<CharNode>(*altNode->getPatterns()[0])) {
        return true; // [\q{A}]
      }
    } else {
      return true;
    }
  }
  return false;
}

bool CodeGen::generateCharClass(const CharClassNode &node) {
  if (isSingleCharClass(node)) {
    return this->generate(*node.getChars()[0]);
  }
  if (node.getChars().empty() && node.isInvert()) { // [^]
    if (this->inLookBehind()) {
      this->builder.emit<LBAnyIns>(true);
    } else {
      this->builder.emit<AnyIns>();
    }
    return true;
  }
  if (isAsciiSet(node)) {
    AsciiSet set;
    appendToAsciiSet(set, node);
    if (this->has(Modifier::IGNORE_CASE)) {
      set = foldCase(set);
    }
    if (auto index = this->emitMatcher(Matcher(set)); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), false);
      return true;
    }
  } else if (node.mayContainStrings()) {
    if (this->inLookBehind()) {
      this->todo(node, "look-behind");
      return false;
    }
    if (this->has(Modifier::IGNORE_CASE)) {
      this->todo(node, "ignore-case");
      return false;
    }
    this->todo(node, "support string class");
    return false;
  } else {
    CodePointSetBuilder setBuilder;
    this->appendToCodePointSet(setBuilder, 0, node);
    bool invert = false;
    if (node.isInvert() && this->mode != Mode::UNICODE_SET) {
      invert = true;
    }
    if (this->has(Modifier::IGNORE_CASE)) {
      setBuilder.foldCase();
    }
    if (auto index = this->emitMatcher(Matcher(setBuilder.build())); index.hasValue()) {
      this->emitCharSetIns(index.unwrap(), invert);
      return true;
    }
  }
  return false;
}

bool CodeGen::generateRepeat(const RepeatNode &node) {
  if (node.getMax() == 0) { // do nothing
    return true;
  }
  if (node.getMin() == 1 && node.getMax() == 1) {
    return this->generate(*node.getPattern());
  }
  const unsigned int beginAddr = this->builder.currentAddr();
  const auto p = this->builder.emitReservedPoint<BeginLoopIns>();
  if (node.getFirstGroupIndex()) {
    assert(node.getFirstGroupIndex() <= node.getLastGroupIndex());
    this->builder.emit<ResetCapturesIns>(node.getFirstGroupIndex(), node.getLastGroupIndex());
  }
  TRY(this->generate(*node.getPattern()));
  this->builder.emit<EndLoopIns>(beginAddr);
  const unsigned int outerAddr = this->builder.currentAddr();
  this->builder.emitAt<BeginLoopIns>(p, node.getLoopIndex(), node.getMin(), node.getMax(),
                                     node.isGreedy(), outerAddr);
  return true;
}

bool CodeGen::generateLookAround(const LookAroundNode &node) {
  const auto p = this->builder.emitReservedPoint<BeginLookAroundIns>();
  this->directions.push_back(node.isLookAhead());
  TRY(this->generate(*node.getPattern()));
  this->directions.pop_back();
  const auto endAddr = this->builder.currentAddr();
  this->builder.emit<EndLookAroundIns>();
  this->builder.emitAt<BeginLookAroundIns>(p, endAddr, node.isNegate());
  return true;
}

Optional<unsigned short> CodeGen::emitMatcher(Matcher &&matcher) {
  unsigned int index = this->matchers.size();
  if (index == UINT16_MAX) {
    this->err += "number of matcher index reaches limit";
    return {};
  }
  this->matchers.emplace_back(std::move(matcher));
  return static_cast<unsigned short>(index);
}

} // namespace arsh::regex