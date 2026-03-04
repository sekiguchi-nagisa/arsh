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

namespace arsh::regex {

// #####################
// ##     CodeGen     ##
// #####################

Optional<Regex> CodeGen::operator()(SyntaxTree &&tree) {
  // prepare
  this->mode = tree.getFlag().mode();
  this->modifierStack.clear();
  this->modifierStack.push(tree.getFlag().modifiers());

  if (!this->generate(*tree.getPattern())) {
    return {};
  }

  // finalize
  this->builder.emit<MatchIns>();
  auto flag = tree.getFlag();
  auto count = tree.getCaptureGroupCount();
  return Regex(flag, count, std::move(this->builder).build(), std::move(this->matchers),
               std::move(tree).takeNamedCaptureGroups());
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
    if (hasFlag(this->modifiers(), Modifier::DOT_ALL)) {
      this->builder.emit<AnyIns>();
    } else {
      this->builder.emit<AnyExceptNLIns>();
    }
    break;
  case NodeKind::Char:
    if (hasFlag(this->modifiers(), Modifier::IGNORE_CASE)) {
      this->todo(node, "ignore-case");
      return false;
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
      this->builder.emit<StartIns>(hasFlag(this->modifiers(), Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::END:
      this->builder.emit<EndIns>(hasFlag(this->modifiers(), Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::WORD:
    case BoundaryNode::Type::NOT_WORD:
      this->builder.emit<WordIns>(t == BoundaryNode::Type::NOT_WORD);
      return true;
    }
    break;
  }
  case NodeKind::BackRef:
  case NodeKind::Repeat:
    this->todo(node);
    return false;
  case NodeKind::Seq:
    for (auto &e : cast<SeqNode>(node).getPatterns()) {
      TRY(this->generate(*e));
    }
    break;
  case NodeKind::Alt:
    return this->generateAlt(cast<AltNode>(node));
  case NodeKind::LookAround:
    this->todo(node);
    return false;
  case NodeKind::Group:
    return this->generateGroup(cast<GroupNode>(node));
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
    this->builder.emit<EndCaptureIns>(node.getGroupIndex());
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

bool CodeGen::generateProperty(const PropertyNode &node) {
  if (node.onlyAscii()) {
    AsciiSet set;
    TRY(this->appendToAsciiSet(set, node));
    if (auto index = this->emitMatcher(Matcher(set)); index.hasValue()) {
      this->builder.emit<CharSetIns>(index.unwrap(), false);
      return true;
    }
  } else if (node.mayContainString()) {
    this->todo(node, "support emoji seq");
    return false;
  } else if (node.isInvert()) {
    CodePointSetBuilder setBuilder;
    TRY(this->toCodePointSet(ucp::BuilderOrSet(setBuilder), node));
    setBuilder.complement();
    if (auto index = this->emitMatcher(Matcher(setBuilder.build())); index.hasValue()) {
      this->builder.emit<CharSetIns>(index.unwrap(), false);
      return true;
    }
  } else {
    CodePointSet set;
    TRY(this->toCodePointSet(ucp::BuilderOrSet(set), node));
    if (auto index = this->emitMatcher(Matcher(std::move(set))); index.hasValue()) {
      this->builder.emit<CharSetIns>(index.unwrap(), false);
      return true;
    }
  }
  return false;
}

bool CodeGen::toCodePointSet(ucp::BuilderOrSet builderOrSet, const PropertyNode &node) {
  switch (node.getNormalizedType()) {
  case PropertyNode::Type::RANGE:
  case PropertyNode::Type::INTERSECT:
  case PropertyNode::Type::SUBTRACT:
  case PropertyNode::Type::NOT_DIGIT:
  case PropertyNode::Type::NOT_WORD:
  case PropertyNode::Type::NOT_SPACE:
  case PropertyNode::Type::NOT_UNICODE:
  case PropertyNode::Type::EMOJI:
    assert(false);
    break; // normally unreachable
  case PropertyNode::Type::DIGIT:
    return ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassDigit), builderOrSet);
  case PropertyNode::Type::WORD:
    return ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassWord), builderOrSet);
  case PropertyNode::Type::SPACE:
    return ucp::getPropertySet(ucp::Property::lone(ucp::Lone::ESRegexClassSpace), builderOrSet);
  case PropertyNode::Type::UNICODE:
    return ucp::getPropertySet(node.getUCP(), builderOrSet);
  }
  return true;
}

bool CodeGen::generateCharClass(const CharClassNode &node) {
  if (node.getChars().size() == 1 && !node.isInvert()) {
    return this->generate(*node.getChars()[0]);
  }
  if (node.getChars().empty() && node.isInvert()) { // [^]
    this->builder.emit<AnyIns>();
    return true;
  }
  if (isAsciiSet(node)) {
    AsciiSet set;
    TRY(this->appendToAsciiSet(set, node));
    if (auto index = this->emitMatcher(Matcher(set)); index.hasValue()) {
      this->builder.emit<CharSetIns>(index.unwrap(), false);
      return true;
    }
  } else if (node.mayContainStrings()) {
    this->todo(node, "support string class");
    return false;
  } else {
    this->todo(node, "char class");
    // CodePointSetBuilder setBuilder;
    // TRY(this->appendToCodePointSet(ucp::BuilderOrSet(setBuilder), CharClassNode::Type::UNION,
    //                                node));
    // if (auto index = this->emitMatcher(Matcher(setBuilder.build())); index.hasValue()) {
    //   this->builder.emit<CharSetIns>(index.unwrap(),
    //                                  node.isInvert() && this->mode != Mode::UNICODE_SET);
    //   return true;
    // }
  }
  return false;
}

bool CodeGen::appendToAsciiSet(AsciiSet &set, const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Char: {
    auto &charNode = cast<CharNode>(node);
    assert(Matcher::withinAsciiSet(charNode.getCodePoint()));
    set.add(charNode.getCodePoint());
    return true;
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
    return true;
  }
  case NodeKind::CharClass: {
    // auto &classNode = cast<CharClassNode>(node);
    // assert(!classNode.isInvert());
    // if (classNode.getType() == CharClassNode::Type::UNION) {
    //   for (auto &e : classNode.getChars()) {
    //     TRY(this->appendToAsciiSet(set, *e));
    //   }
    // } else {
    //   assert(classNode.getType() == CharClassNode::Type::RANGE);
    //   // TODO:
    // }
    // return true;
    this->todo(node, "ascii class");
    return false;
  }
  default:
    return false; // unreachable
  }
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