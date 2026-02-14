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

#ifndef ARSH_REGEX_NODE_H
#define ARSH_REGEX_NODE_H

#include <memory>
#include <vector>

#include "capture.h"
#include "flag.h"
#include "misc/enum_util.hpp"
#include "misc/noncopyable.h"
#include "misc/rtti.hpp"
#include "misc/token.hpp"
#include "unicode/property.h"

namespace arsh::regex {

#define EACH_RE_NODE_KIND(OP)                                                                      \
  OP(Empty)                                                                                        \
  OP(Any)                                                                                          \
  OP(Char)                                                                                         \
  OP(CharClass)                                                                                    \
  OP(Property)                                                                                     \
  OP(Boundary)                                                                                     \
  OP(BackRef)                                                                                      \
  OP(Repeat)                                                                                       \
  OP(Seq)                                                                                          \
  OP(Alt)                                                                                          \
  OP(LookAround)                                                                                   \
  OP(Group)

/**
 * for LLVM-style RTTI
 * see. https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html
 */
enum class NodeKind : unsigned char {
#define GEN_ENUM(E) E,
  EACH_RE_NODE_KIND(GEN_ENUM)
#undef GEN_ENUM
};

class Node {
protected:
  const NodeKind kind;
  unsigned char u8{0};
  unsigned short u16{0};
  unsigned int u32{0};
  Token token; // for error reporting

  NON_COPYABLE(Node);

  Node(NodeKind kind, Token token) : kind(kind), token(token) {}

public:
  virtual ~Node() = default;

  NodeKind getKind() const { return this->kind; }

  Token getToken() const { return this->token; }

  void updateToken(Token t) {
    if (t.endPos() > this->token.endPos()) {
      this->token.size = t.endPos() - this->token.pos;
    }
  }
};

template <NodeKind K>
class NodeWithRtti : public Node {
protected:
  explicit NodeWithRtti(Token token) : Node(K, token) {}

public:
  static constexpr auto KIND = K;

  static bool classof(const Node *node) { return node->getKind() == K; }
};

class NestedNode : public Node {
protected:
  std::unique_ptr<Node> pattern;

  NestedNode(NodeKind kind, Token token, std::unique_ptr<Node> &&pattern)
      : Node(kind, token), pattern(std::move(pattern)) {}

  NestedNode(NodeKind kind, std::unique_ptr<Node> &&pattern)
      : Node(kind, pattern->getToken()), pattern(std::move(pattern)) {}

public:
  static bool classof(const Node *node) {
    switch (node->getKind()) {
    case NodeKind::Empty:
    case NodeKind::Any:
    case NodeKind::Char:
    case NodeKind::CharClass:
    case NodeKind::Property:
    case NodeKind::Boundary:
    case NodeKind::BackRef:
    case NodeKind::Seq:
    case NodeKind::Alt:
      break;
    case NodeKind::Repeat:
    case NodeKind::LookAround:
    case NodeKind::Group:
      return true;
    }
    return false;
  }

  const auto &getPattern() const { return this->pattern; }

  auto &refPattern() { return this->pattern; }
};

template <NodeKind K>
class NestedNodeWithRtti : public NestedNode {
protected:
  NestedNodeWithRtti(Token token, std::unique_ptr<Node> &&pattern)
      : NestedNode(K, token, std::move(pattern)) {}

  explicit NestedNodeWithRtti(std::unique_ptr<Node> &&pattern)
      : NestedNode(K, std::move(pattern)) {}

public:
  static constexpr auto KIND = K;

  static bool classof(const Node *node) { return node->getKind() == K; }
};

class ListNode : public Node {
protected:
  std::vector<std::unique_ptr<Node>> patterns;

  ListNode(NodeKind kind, Token token) : Node(kind, token) {}

public:
  static bool classof(const Node *node) {
    switch (node->getKind()) {
    case NodeKind::Empty:
    case NodeKind::Any:
    case NodeKind::Char:
    case NodeKind::Property:
    case NodeKind::Boundary:
    case NodeKind::BackRef:
    case NodeKind::Repeat:
    case NodeKind::LookAround:
    case NodeKind::Group:
      break;
    case NodeKind::CharClass:
    case NodeKind::Seq:
    case NodeKind::Alt:
      return true;
    }
    return false;
  }

  const auto &getPatterns() const { return this->patterns; }

  auto &refPatterns() { return this->patterns; }
};

template <NodeKind K>
class ListNodeWithRtti : public ListNode {
protected:
  explicit ListNodeWithRtti(Token token) : ListNode(K, token) {}

public:
  static constexpr auto KIND = K;

  static bool classof(const Node *node) { return node->getKind() == K; }
};

class EmptyNode : public NodeWithRtti<NodeKind::Empty> {
public:
  explicit EmptyNode(unsigned int pos) : NodeWithRtti({pos, 0}) {}
};

class AnyNode : public NodeWithRtti<NodeKind::Any> {
public:
  explicit AnyNode(Token token) : NodeWithRtti(token) {}
};

class CharNode : public NodeWithRtti<NodeKind::Char> {
public:
  CharNode(Token token, int codePoint) : NodeWithRtti(token) { this->u32 = codePoint; }

  int getCodePoint() const { return static_cast<int>(this->u32); }
};

class PropertyNode : public NodeWithRtti<NodeKind::Property> {
public:
  enum class Type : unsigned char {
    RANGE,       // - (dummy property for char class range)
    INTERSECT,   // && (dummy property for char class set operation)
    SUBTRACT,    // -- (dummy property for char class set operation)
    DIGIT,       // \d
    NOT_DIGIT,   // \D
    WORD,        // \w
    NOT_WORD,    // \W
    SPACE,       // \s
    NOT_SPACE,   // \S
    UNICODE,     // \p{C}
    NOT_UNICODE, // \P{C}
    EMOJI,       // \p{RGI_Emoji}
  };

  PropertyNode(Token token, Type t) : NodeWithRtti(token) { this->u8 = toUnderlying(t); }

  PropertyNode(Token token, ucp::Property p, bool invert) : NodeWithRtti(token) {
    this->u8 = toUnderlying(invert ? Type::NOT_UNICODE : Type::UNICODE);

    auto n = toUnderlying(p.getName());
    static_assert(sizeof(n) == sizeof(uint8_t));
    auto v = p.getValue();
    static_assert(sizeof(v) == sizeof(uint8_t));
    this->u16 = n << 8 | v;
  }

  PropertyNode(Token token, RGIEmojiSeq seq) : NodeWithRtti(token) {
    this->u8 = toUnderlying(Type::EMOJI);
    static_assert(sizeof(RGIEmojiSeq) <= sizeof(uint16_t));
    this->u16 = toUnderlying(seq);
  }

  Type getType() const { return static_cast<Type>(this->u8); }

  ucp::Property getUCP() const {
    auto n = static_cast<ucp::Property::Name>(this->u16 >> 8);
    auto v = static_cast<unsigned char>(this->u16 & 0xFF);
    return {n, v};
  }

  RGIEmojiSeq getEmojiSeq() const { return static_cast<RGIEmojiSeq>(this->u16); }

  bool isInvert() const {
    switch (this->getType()) {
    case Type::NOT_DIGIT:
    case Type::NOT_WORD:
    case Type::NOT_SPACE:
    case Type::NOT_UNICODE:
      return true;
    default:
      return false;
    }
  }

  Type getNormalizedType() const {
    switch (this->getType()) {
    case Type::NOT_DIGIT:
      return Type::DIGIT;
    case Type::NOT_WORD:
      return Type::WORD;
    case Type::NOT_SPACE:
      return Type::SPACE;
    case Type::NOT_UNICODE:
      return Type::UNICODE;
    default:
      return this->getType();
    }
  }

  bool isCharClassOp() const {
    switch (this->getType()) {
    case Type::RANGE:
    case Type::INTERSECT:
    case Type::SUBTRACT:
      return true;
    default:
      return false;
    }
  }
};

inline bool isProperty(const Node &node, PropertyNode::Type t) {
  return isa<PropertyNode>(node) && cast<PropertyNode>(node).getType() == t;
}

inline bool isCharClassOp(const Node &node) {
  return isa<PropertyNode>(node) && cast<PropertyNode>(node).isCharClassOp();
}

class CharClassNode : public ListNodeWithRtti<NodeKind::CharClass> {
public:
  enum class Type : unsigned char {
    UNION,     // default
    RANGE,     // have char ranges
    INTERSECT, // &&
    SUBTRACT,  // --
  };

  explicit CharClassNode(Token token, bool invert) : ListNodeWithRtti(token) {
    this->u8 = invert ? 1 : 0;
    this->setType(Type::UNION);
  }

  void add(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->patterns.push_back(std::move(node));
  }

  bool isInvert() const { return this->u8 == 1; }

  void setType(const Type t) { this->u16 = toUnderlying(t); }

  Type getType() const { return static_cast<Type>(this->u16); }

  bool mayContainStrings() const { return this->u32 > 0; }

  bool isUnionOrRange() const {
    return this->getType() == Type::UNION || this->getType() == Type::RANGE;
  }

  const auto &getChars() const { return this->patterns; }

  auto &refChars() { return this->patterns; }

  /**
   * [a-ba-] [b--a--] [b&&a&&]
   * @return
   */
  bool hasUnterminatedCharOp() const {
    return !this->getChars().empty() && isa<PropertyNode>(*this->getChars().back()) &&
           cast<PropertyNode>(*this->getChars().back()).isCharClassOp();
  }

  bool isCompatible(const Type other) const {
    if (this->getChars().size() == 1) {
      return true;
    }
    if (this->getType() == other) {
      return true;
    }
    if (this->getType() == Type::UNION && other == Type::RANGE) {
      return true;
    }
    if (this->getType() == Type::RANGE && other == Type::UNION) {
      return true;
    }
    return false;
  }

  bool finalize(Token endToken);
};

class BoundaryNode : public NodeWithRtti<NodeKind::Boundary> {
public:
  enum class Type : unsigned char {
    START,    // ^
    END,      // $
    WORD,     // \b
    NOT_WORD, // \B
  };

public:
  BoundaryNode(Token token, Type type) : NodeWithRtti(token) { this->u8 = toUnderlying(type); }

  Type getType() const { return static_cast<Type>(this->u8); }
};

class BackRefNode : public NodeWithRtti<NodeKind::BackRef> {
private:
  /**
   * capture group name
   */
  std::string name;

public:
  BackRefNode(Token token, std::string &&name) : NodeWithRtti(token), name(std::move(name)) {
    this->setIndex(0);
  }

  BackRefNode(Token token, unsigned int groupIndex) : NodeWithRtti(token) {
    this->setIndex(groupIndex);
  }

  void setIndex(unsigned int index) { this->u32 = index; }

  /**
   * if named-reference, index indicates offset of named capture group entry
   * otherwise, indicates group index
   * @return
   */
  unsigned int getIndex() const { return this->u32; }

  const auto &getName() const { return this->name; }

  bool isNamed() const { return !this->getName().empty(); }
};

class RepeatNode : public NestedNodeWithRtti<NodeKind::Repeat> {
public:
  static constexpr unsigned int QUANTIFIER_MAX = UINT16_MAX;

  static constexpr unsigned int UNLIMIT = UINT32_MAX;

  RepeatNode(std::unique_ptr<Node> &&pattern, unsigned short min, unsigned int max, bool greedy,
             Token end)
      : NestedNodeWithRtti(pattern->getToken(), std::move(pattern)) {
    this->updateToken(end);
    this->u8 = greedy ? 1 : 0;
    this->u16 = min;
    this->u32 = max;
  }

  static std::unique_ptr<RepeatNode> option(std::unique_ptr<Node> &&pattern, bool greedy,
                                            Token end) {
    return std::make_unique<RepeatNode>(std::move(pattern), 0, 1, greedy, end);
  }

  static std::unique_ptr<RepeatNode> zeroOrMore(std::unique_ptr<Node> &&pattern, bool greedy,
                                                Token end) {
    return std::make_unique<RepeatNode>(std::move(pattern), 0, UNLIMIT, greedy, end);
  }

  static std::unique_ptr<RepeatNode> oneOrMore(std::unique_ptr<Node> &&pattern, bool greedy,
                                               Token end) {
    return std::make_unique<RepeatNode>(std::move(pattern), 1, UNLIMIT, greedy, end);
  }

  bool isGreedy() const { return this->u8 == 1; }

  unsigned short getMin() const { return this->u16; }

  unsigned int getMax() const { return this->u32; }

  bool isUnlimited() const { return this->getMax() == UNLIMIT; }
};

class SeqNode : public ListNodeWithRtti<NodeKind::Seq> {
public:
  SeqNode(std::unique_ptr<Node> &&left, std::unique_ptr<Node> &&right)
      : ListNodeWithRtti(left->getToken()) {
    this->append(std::move(left));
    this->append(std::move(right));
  }

  void append(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->patterns.push_back(std::move(node));
  }
};

class AltNode : public ListNodeWithRtti<NodeKind::Alt> {
public:
  explicit AltNode(std::unique_ptr<Node> &&node) : ListNodeWithRtti(node->getToken()) {
    this->append(std::move(node));
  }

  void append(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->patterns.push_back(std::move(node));
  }

  void appendNull() { this->patterns.push_back(nullptr); }

  void appendToLast(std::unique_ptr<Node> &&node);
};

class LookAroundNode : public NestedNodeWithRtti<NodeKind::LookAround> {
public:
  enum class Type : unsigned char {
    LOOK_AHEAD,      // (?=pattern)
    LOOK_AHEAD_NOT,  // (?!pattern)
    LOOK_BEHIND,     // (?<=pattern)
    LOOK_BEHIND_NOT, // (?<!pattern)
  };

  LookAroundNode(Token start, Type type, std::unique_ptr<Node> &&pattern, Token end)
      : NestedNodeWithRtti(start, std::move(pattern)) {
    this->u8 = toUnderlying(type);
    this->updateToken(end);
  }

  Type getType() const { return static_cast<Type>(this->u8); }
};

class GroupNode : public NestedNodeWithRtti<NodeKind::Group> {
public:
  static_assert(sizeof(Modifier) == sizeof(uint8_t));

  static constexpr unsigned int CAPTURE_GROUP_INDEX_MAX = UINT16_MAX;

  enum class Type : unsigned char {
    CAPTURE,     // (pattern), (?<name>pattern)
    NON_CAPTURE, // (?:pattern)
    MODIFIER,    // (?ims-ims:pattern)
  };

  GroupNode(Token start, Type type, unsigned int groupIndex, Modifier set, Modifier unset,
            std::unique_ptr<Node> &&pattern, Token end)
      : NestedNodeWithRtti(start, std::move(pattern)) {
    this->u8 = toUnderlying(type);
    this->u16 = (toUnderlying(set) << 8) | toUnderlying(unset);
    this->u32 = groupIndex;
    this->updateToken(end);
  }

  Type getType() const { return static_cast<Type>(this->u8); }

  Modifier getSetModifiers() const { return static_cast<Modifier>(this->u16 >> 8); }

  Modifier getUnsetModifiers() const { return static_cast<Modifier>(this->u16 & 0xFF); }

  /**
   *
   * @return   * if not a capture group, return 0
   */
  unsigned int getGroupIndex() const { return this->u32; }
};

class SyntaxTree {
private:
  Flag flag;
  unsigned int captureGroupCount;
  std::unique_ptr<Node> pattern;
  NamedCaptureGroups namedCaptureGroups;

public:
  SyntaxTree(Flag flag, unsigned int count, std::unique_ptr<Node> pattern,
             NamedCaptureGroups &&namedCaptureGroups)
      : flag(flag), captureGroupCount(count), pattern(std::move(pattern)),
        namedCaptureGroups(std::move(namedCaptureGroups)) {}

  Flag getFlag() const { return this->flag; }

  const auto &getPattern() const { return this->pattern; }

  unsigned int getCaptureGroupCount() const { return this->captureGroupCount; }

  const auto &getNamedCaptureGroups() const { return this->namedCaptureGroups; }

  auto takeNamedCaptureGroups() && { return std::move(this->namedCaptureGroups); }

  explicit operator bool() const { return static_cast<bool>(this->pattern); }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_NODE_H
