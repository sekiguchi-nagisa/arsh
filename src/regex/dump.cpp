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

#include "dump.h"
#include "misc/unicode.hpp"
#include "node.h"

#include <algorithm>
#include <cstdarg>

namespace arsh::regex {

// ########################
// ##     TreeDumper     ##
// ########################

void TreeDumper::dumpAs(const char *fieldName, const char *fmt, ...) { // NOLINT
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    abort();
  }
  va_end(arg);
  this->dump(fieldName, str);
  free(str);
}

static void format(const NamedCaptureEntry &entry, std::string &out) {
  if (entry.hasMultipleIndices()) {
    out += '[';
    const unsigned int size = entry.getSize();
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        out += ", ";
      }
      out += std::to_string(entry[i]);
    }
    out += ']';
  } else {
    out += std::to_string(entry.getIndex());
  }
}

static std::string format(const NamedCaptureGroups &namedCaptureGroups) {
  std::string ret = "[";
  unsigned int count = 0;
  for (auto &[name, entry] : namedCaptureGroups.getEntries()) {
    if (count++ > 0) {
      ret += ", ";
    }
    ret += name;
    ret += ": ";
    format(entry, ret);
  }
  ret += ']';
  return ret;
}

std::string TreeDumper::operator()(const SyntaxTree &tree) {
  this->indentLevel = 0;
  this->dump("flag", tree.getFlag());
  this->dump("captureGroupCount", std::to_string(tree.getCaptureGroupCount()));
  this->dump("namedCaptureGroups", format(tree.getNamedCaptureGroups()));
  this->dump("pattern", *tree.getPattern());
  return std::move(this->buf);
}

static void toStringModifier(Modifier m, std::string &out) {
#define GEN_IF(E, S, D)                                                                            \
  if (hasFlag(m, Modifier::E)) {                                                                   \
    out += (S);                                                                                    \
  }
  EACH_RE_MODIFIER(GEN_IF)
#undef GEN_TABLE
}

void TreeDumper::dump(const char *fieldName, Flag flag) {
  std::string str = "(mode = ";
  switch (flag.mode()) {
  case Mode::BMP:
    break;
  case Mode::UNICODE:
    str += 'u';
    break;
  case Mode::UNICODE_SET:
    str += 'v';
    break;
  }
  str += ", modifier = ";
  toStringModifier(flag.modifiers(), str);
  str += ')';
  this->dump(fieldName, str);
}

void TreeDumper::dump(const char *fieldName, const std::vector<std::unique_ptr<Node>> &nodes) {
  this->dumpNodesHead(fieldName);
  for (auto &e : nodes) {
    this->dumpNodesBody(*e);
  }
  this->dumpNodesTail();
}

static const char *toString(NodeKind kind) {
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

void TreeDumper::dumpNodeHeader(const Node &node, const bool inArray) {
  if (inArray) {
    this->append("- ");
  }
  this->append("kind: ");
  this->append(toString(node.getKind()));
  this->newline();
  if (inArray) {
    this->enterIndent();
  }
  this->dump("token", node.getToken());
  if (inArray) {
    this->leaveIndent();
  }
}

#define GEN_ENUM_STR(P, E)                                                                         \
  case P::E:                                                                                       \
    str__ = #E;                                                                                    \
    break;

#define ENUM_TO_STR(val, EACH_ENUM)                                                                \
  ({                                                                                               \
    const char *str__ = "";                                                                        \
    switch (val) { EACH_ENUM(GEN_ENUM_STR) }                                                       \
    str__;                                                                                         \
  })

#define DUMP_ENUM(f, val, EACH_ENUM)                                                               \
  do {                                                                                             \
    auto v__ = ENUM_TO_STR(val, EACH_ENUM);                                                        \
    this->dump(f, v__);                                                                            \
  } while (false)

void TreeDumper::dumpRaw(const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Empty:
  case NodeKind::Any:
    break;
  case NodeKind::Char: {
    int codePoint = cast<CharNode>(node).getCodePoint();
    std::string str;
    if (codePoint <= 32 || codePoint == 127) {
      char d[16];
      snprintf(d, std::size(d), "\\x%02X", codePoint);
      str += d;
    } else {
      char data[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, data);
      assert(len);
      str += StringRef(data, len);
    }
    this->dumpAs("codePoint", "U+%04X, %s", codePoint, str.c_str());
    break;
  }
  case NodeKind::CharClass:
#define EACH_RE_CHAR_CLASS(E)                                                                      \
  E(CharClassNode::Type, UNION)                                                                    \
  E(CharClassNode::Type, RANGE)                                                                    \
  E(CharClassNode::Type, INTERSECT)                                                                \
  E(CharClassNode::Type, SUBTRACT)
    DUMP_ENUM("class", cast<CharClassNode>(node).getType(), EACH_RE_CHAR_CLASS);
#undef EACH_RE_CHAR_CLASS
    this->dump("invert", cast<CharClassNode>(node).isInvert());
    this->dump("chars", cast<CharClassNode>(node).getChars());
    break;
  case NodeKind::Property:
    this->dumpRaw(cast<PropertyNode>(node));
    break;
  case NodeKind::Boundary:
#define EACH_RE_BOUNDARY(E)                                                                        \
  E(BoundaryNode::Type, START)                                                                     \
  E(BoundaryNode::Type, END)                                                                       \
  E(BoundaryNode::Type, WORD)                                                                      \
  E(BoundaryNode::Type, NOT_WORD)
    DUMP_ENUM("boundary", cast<BoundaryNode>(node).getType(), EACH_RE_BOUNDARY);
#undef EACH_RE_BOUNDARY
    break;
  case NodeKind::BackRef: {
    auto &n = cast<BackRefNode>(node);
    this->dump("name", n.getName());
    this->dumpAs("index", "%d", n.getIndex());
    break;
  }
  case NodeKind::Repeat: {
    auto &n = cast<RepeatNode>(node);
    std::string limit;
    if (n.isUnlimited()) {
      limit = "unlimited";
    } else {
      limit = std::to_string(n.getMax());
    }
    this->dumpAs("repeat", "(min = %d, max = %s)", n.getMin(), limit.c_str());
    this->dump("greedy", n.isGreedy());
    this->dump("pattern", *n.getPattern());
    break;
  }
  case NodeKind::Seq:
    this->dump("patterns", cast<SeqNode>(node).getPatterns());
    break;
  case NodeKind::Alt:
    this->dump("patterns", cast<AltNode>(node).getPatterns());
    break;
  case NodeKind::LookAround: {
    auto &n = cast<LookAroundNode>(node);
#define EACH_RE_LOOK(E)                                                                            \
  E(LookAroundNode::Type, LOOK_AHEAD)                                                              \
  E(LookAroundNode::Type, LOOK_AHEAD_NOT)                                                          \
  E(LookAroundNode::Type, LOOK_BEHIND)                                                             \
  E(LookAroundNode::Type, LOOK_BEHIND_NOT)
    DUMP_ENUM("type", n.getType(), EACH_RE_LOOK);
#undef EACH_RE_LOOK
    this->dump("pattern", *n.getPattern());
    break;
  }
  case NodeKind::Group: {
    auto &n = cast<GroupNode>(node);
#define EACH_RE_GROUP(E)                                                                           \
  E(GroupNode::Type, CAPTURE)                                                                      \
  E(GroupNode::Type, NON_CAPTURE)                                                                  \
  E(GroupNode::Type, MODIFIER)
    DUMP_ENUM("group", n.getType(), EACH_RE_GROUP);
#undef EACH_RE_GROUP
    this->dumpAs("index", "%d", n.getGroupIndex());
    std::string str;
    toStringModifier(n.getSetModifiers(), str);
    this->dump("setModifiers", str);
    str.clear();
    toStringModifier(n.getUnsetModifiers(), str);
    this->dump("unsetModifiers", str);
    this->dump("pattern", *n.getPattern());
    break;
  }
  }
}

void TreeDumper::dumpRaw(const PropertyNode &node) {
#define EACH_RE_PROPERTY(E)                                                                        \
  E(PropertyNode::Type, RANGE)                                                                     \
  E(PropertyNode::Type, INTERSECT)                                                                 \
  E(PropertyNode::Type, SUBTRACT)                                                                  \
  E(PropertyNode::Type, DIGIT)                                                                     \
  E(PropertyNode::Type, NOT_DIGIT)                                                                 \
  E(PropertyNode::Type, WORD)                                                                      \
  E(PropertyNode::Type, NOT_WORD)                                                                  \
  E(PropertyNode::Type, SPACE)                                                                     \
  E(PropertyNode::Type, NOT_SPACE)                                                                 \
  E(PropertyNode::Type, UNICODE)                                                                   \
  E(PropertyNode::Type, NOT_UNICODE)                                                               \
  E(PropertyNode::Type, EMOJI)
  DUMP_ENUM("property", node.getType(), EACH_RE_PROPERTY);
#undef EACH_RE_PROPERTY

  std::string str;
  if (auto t = node.getNormalizedType(); t == PropertyNode::Type::UNICODE) {
    auto p = node.getUCP();
    switch (p.getName()) {
    case ucp::Property::Name::General_Category:
      str = "gc=";
      break;
    case ucp::Property::Name::Script:
      str = "sc=";
      break;
    case ucp::Property::Name::Script_Extensions:
      str = "scx=";
      break;
    case ucp::Property::Name::Lone:
      break;
    }
    str += p.toStringValue(true);
  } else if (t == PropertyNode::Type::EMOJI) {
    str = ucp::toString(node.getEmojiSeq());
  } else {
    str = "0";
  }
  this->dump("value", str.c_str());
  this->dump("invert", node.isInvert());
}

} // namespace arsh::regex