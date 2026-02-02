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

#include "misc/enum_util.hpp"
#include "misc/noncopyable.h"
#include "misc/rtti.hpp"
#include "misc/token.hpp"
#include "unicode/property.h"

#include "flag.hpp"

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

class CharClassNode : public NodeWithRtti<NodeKind::CharClass> {
private:
  std::vector<std::unique_ptr<Node>> chars;

public:
  explicit CharClassNode(Token token, bool invert) : NodeWithRtti(token) {
    this->u8 = invert ? 1 : 0;
  }

  void add(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->chars.push_back(std::move(node));
  }

  bool isInvert() const { return this->u8 == 1; }

  const auto &getChars() const { return this->chars; }

  auto &refChars() { return this->chars; }
};

class PropertyNode : public NodeWithRtti<NodeKind::Property> {
public:
  enum class Type : unsigned char {
    RANGE,       // - (dummy property for char class range)
    DIGIT,       // \d
    NOT_DIGIT,   // \D
    WORD,        // \w
    NOT_WORD,    // \W
    SPACE,       // \s
    NOT_SPACE,   // \S
    UNICODE,     // \p{C}
    NOT_UNICODE, // \P{C}
    EMOJI,       // \p{RGI_Emoji}
    NOT_EMOJI,   // `P{RGI_Emoji}
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

  PropertyNode(Token token, RGIEmojiSeq seq, bool invert) : NodeWithRtti(token) {
    this->u8 = toUnderlying(invert ? Type::NOT_EMOJI : Type::EMOJI);
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
    case Type::NOT_EMOJI:
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
    case Type::NOT_EMOJI:
      return Type::EMOJI;
    default:
      return this->getType();
    }
  }
};

inline bool isProperty(const Node &node, PropertyNode::Type t) {
  return isa<PropertyNode>(node) && cast<PropertyNode>(node).getType() == t;
}

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
   * capture group name or digits
   * in bmp mode, may indicate octal digits sequence
   */
  std::string name; // TODO: check group name

public:
  BackRefNode(Token token, StringRef ref, bool named) : NodeWithRtti(token), name(ref.toString()) {
    this->u8 = named ? 1 : 0;
  }

  bool isNamed() const { return this->u8 == 1; }

  const auto &getName() const { return this->name; }
};

class RepeatNode : public NodeWithRtti<NodeKind::Repeat> {
private:
  std::unique_ptr<Node> pattern;

public:
  RepeatNode(std::unique_ptr<Node> &&pattern, unsigned short min, unsigned short max, bool greedy,
             Token end)
      : NodeWithRtti(pattern->getToken()), pattern(std::move(pattern)) {
    this->updateToken(end);
    this->u8 = greedy ? 1 : 0;
    this->u16 = min;
    this->u32 = max;
  }

  bool isGreedy() const { return this->u8 == 1; }

  unsigned short getMin() const { return this->u16; }

  unsigned short getMax() const { return this->u32; }

  const auto &getPattern() const { return this->pattern; }
};

class SeqNode : public NodeWithRtti<NodeKind::Seq> {
private:
  std::vector<std::unique_ptr<Node>> patterns;

public:
  SeqNode(std::unique_ptr<Node> &&left, std::unique_ptr<Node> &&right)
      : NodeWithRtti(left->getToken()) {
    this->append(std::move(left));
    this->append(std::move(right));
  }

  void append(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->patterns.push_back(std::move(node));
  }

  const auto &getPatterns() const { return this->patterns; }
};

class AltNode : public NodeWithRtti<NodeKind::Alt> {
private:
  std::vector<std::unique_ptr<Node>> patterns;

public:
  explicit AltNode(std::unique_ptr<Node> &&node) : NodeWithRtti(node->getToken()) {
    this->append(std::move(node));
  }

  void append(std::unique_ptr<Node> &&node) {
    this->updateToken(node->getToken());
    this->patterns.push_back(std::move(node));
  }

  void appendNull() { this->patterns.push_back(nullptr); }

  void appendToLast(std::unique_ptr<Node> &&node);

  const auto &getPatterns() const { return this->patterns; }
};

class LookAroundNode : public NodeWithRtti<NodeKind::LookAround> {
public:
  enum class Type : unsigned char {
    LOOK_AHEAD,      // (?=pattern)
    LOOK_AHEAD_NOT,  // (?!pattern)
    LOOK_BEHIND,     // (?<=pattern)
    LOOK_BEHIND_NOT, // (?<!pattern)
  };

private:
  std::unique_ptr<Node> pattern;

public:
  LookAroundNode(Token start, Type type, std::unique_ptr<Node> &&pattern, Token end)
      : NodeWithRtti(start), pattern(std::move(pattern)) {
    this->u8 = toUnderlying(type);
    this->updateToken(end);
  }

  Type getType() const { return static_cast<Type>(this->u8); }

  const auto &getPattern() const { return this->pattern; }
};

class GroupNode : public NodeWithRtti<NodeKind::Group> {
public:
  enum class Type : unsigned char {
    CAPTURE,     // (pattern)
    NON_CAPTURE, // (?:pattern)
    MODIFIER,    // (?ims-ims:pattern)
  };

private:
  std::unique_ptr<Node> pattern;

public:
  GroupNode(Token start, Type type, std::unique_ptr<Node> &&pattern, Token end)
      : NodeWithRtti(start), pattern(std::move(pattern)) {
    this->u8 = toUnderlying(type);
    this->updateToken(end);
  }

  Type getType() const { return static_cast<Type>(this->u8); }

  const auto &getPattern() const { return this->pattern; }
};

class SyntaxTree {
private:
  Flag flag;
  unsigned int captureGroupCount;
  std::unique_ptr<Node> pattern;
  std::unordered_map<std::string, unsigned int> namedCaptureGroups;

public:
  SyntaxTree(Flag flag, unsigned int count, std::unique_ptr<Node> pattern,
             std::unordered_map<std::string, unsigned int> &&namedCaptureGroups)
      : flag(flag), captureGroupCount(count), pattern(std::move(pattern)),
        namedCaptureGroups(std::move(namedCaptureGroups)) {}

  Flag getFlag() const { return this->flag; }

  const auto &getPattern() const { return this->pattern; }

  unsigned int getCaptureGroupCount() const { return this->captureGroupCount; }

  const auto &getNamedCaptureGroups() const { return this->namedCaptureGroups; }

  explicit operator bool() const { return static_cast<bool>(this->pattern); }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_NODE_H
