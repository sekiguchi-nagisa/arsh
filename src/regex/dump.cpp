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
#include "misc/format.hpp"
#include "misc/unicode.hpp"
#include "node.h"

#include <algorithm>
#include <cstdarg>

namespace arsh::regex {

// ########################
// ##     DumperBase     ##
// ########################

void DumperBase::dumpAs(const char *fieldName, const char *fmt, ...) { // NOLINT
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

static void toStringModifier(Modifier m, std::string &out) {
#define GEN_IF(E, S, D)                                                                            \
  if (hasFlag(m, Modifier::E)) {                                                                   \
    out += (S);                                                                                    \
  }
  EACH_RE_MODIFIER(GEN_IF)
#undef GEN_TABLE
}

void DumperBase::dump(const char *fieldName, Flag flag) {
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

// ########################
// ##     TreeDumper     ##
// ########################

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
  this->base.indentLevel = 0;
  this->base.dump("flag", tree.getFlag());
  this->base.dumpAs("loopCount", "%d", tree.getLoopCount());
  this->base.dump("captureGroupCount", std::to_string(tree.getCaptureGroupCount()));
  this->base.dump("namedCaptureGroups", format(tree.getNamedCaptureGroups()));
  this->dump("pattern", *tree.getPattern());
  return std::move(this->base.buf);
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
    this->base.append("- ");
  }
  this->base.append("kind: ");
  this->base.append(toString(node.getKind()));
  this->base.newline();
  if (inArray) {
    this->base.enterIndent();
  }
  this->dump("token", node.getToken());
  if (inArray) {
    this->base.leaveIndent();
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
    this->base.dump(f, v__);                                                                       \
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
    this->base.dumpAs("codePoint", "U+%04X, %s", codePoint, str.c_str());
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
    this->base.dumpBool("invert", cast<CharClassNode>(node).isInvert());
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
    this->base.dump("name", n.getName());
    this->base.dumpAs("index", "%d", n.getIndex());
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
    this->base.dumpAs("repeat", "(min = %d, max = %s)", n.getMin(), limit.c_str());
    this->base.dumpBool("greedy", n.isGreedy());
    this->base.dumpAs("loopIndex", "%d", n.getLoopIndex());
    this->base.dumpAs("firstGroupIndex", "%d", n.getFirstGroupIndex());
    this->base.dumpAs("lastGroupIndex", "%d", n.getLastGroupIndex());
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
    this->base.dumpAs("index", "%d", n.getGroupIndex());
    std::string str;
    toStringModifier(n.getSetModifiers(), str);
    this->base.dump("setModifiers", str);
    str.clear();
    toStringModifier(n.getUnsetModifiers(), str);
    this->base.dump("unsetModifiers", str);
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
  this->base.dump("value", str.c_str());
  this->base.dumpBool("invert", node.isInvert());
}

// #########################
// ##     RegexDumper     ##
// #########################

std::string RegexDumper::operator()(const Regex &regex) {
  this->base.indentLevel = 0;
  this->base.dump("flag", regex.getFlag());
  this->base.dumpAs("loopCount", "%d", regex.getLoopCount());
  this->base.dump("captureGroupCount", std::to_string(regex.getCaptureGroupCount()));
  this->base.dump("namedCaptureGroups", format(regex.getNamedCaptureGroups()));
  this->dump("instructions", regex.getInstSeq());
  this->dump("matchers", regex.getMatchers());
  return std::move(this->base.buf);
}

class Padding {
private:
  unsigned int width;

public:
  explicit Padding(unsigned int maxNum) {
    this->width = countDigits(maxNum > 0 ? maxNum - 1 : maxNum);
  }

  std::string operator()(unsigned int n) const { return padLeft(n, this->width, ' '); }
};

static const char *toString(const OpCode op) {
  switch (op) {
#define GEN_CASE(E)                                                                                \
  case OpCode::E:                                                                                  \
    return #E;
    EACH_RE_OPCODE(GEN_CASE)
#undef GEN_CASE
  }
  return "";
}

static void appendBool(std::string &out, bool b) { out += b ? "true" : "false"; }

void RegexDumper::dump(const FlexBuffer<Inst> &ins) {
  Padding padding(ins.size());
  for (auto *inst = ins.begin(); inst != ins.end();) {
    std::string lineNum = padding(inst - ins.begin());
    std::string str = toString(inst->op);
    switch (inst->op) {
    case OpCode::Nop:
      inst += sizeof(NopIns);
      break;
    case OpCode::Match:
      inst += sizeof(MatchIns);
      break;
    case OpCode::Jump:
      str += "(target=";
      str += std::to_string(cast<JumpIns>(*inst).getTarget());
      str += ')';
      inst += sizeof(JumpIns);
      break;
    case OpCode::Alt:
      str += "(second=";
      str += std::to_string(cast<AltIns>(*inst).getSecond());
      str += ')';
      inst += sizeof(AltIns);
      break;
    case OpCode::Start:
      str += "(multiline=";
      appendBool(str, cast<StartIns>(*inst).multiline);
      str += ')';
      inst += sizeof(StartIns);
      break;
    case OpCode::End:
      str += "(multiline=";
      appendBool(str, cast<EndIns>(*inst).multiline);
      str += ')';
      inst += sizeof(EndIns);
      break;
    case OpCode::Word:
    case OpCode::IWord:
      str += "(invert=";
      appendBool(str,
                 isa<WordIns>(*inst) ? cast<WordIns>(*inst).invert : cast<IWordIns>(*inst).invert);
      str += ')';
      inst += sizeof(WordIns);
      break;
    case OpCode::Any:
      inst += sizeof(AnyIns);
      break;
    case OpCode::AnyExceptNL:
      inst += sizeof(AnyExceptNLIns);
      break;
    case OpCode::Char:
    case OpCode::IChar: {
      const int codePoint = isa<CharIns>(*inst) ? cast<CharIns>(*inst).getCodePoint()
                                                : cast<ICharIns>(*inst).getCodePoint();
      char b[16];
      snprintf(b, std::size(b), "U+%04X", codePoint);
      str += "(codePoint=";
      str += b;
      str += ":";
      unsigned int len = UnicodeUtil::codePointToUtf8(codePoint, b);
      assert(len);
      str += StringRef(b, len);
      str += ')';
      inst += sizeof(CharIns);
      break;
    }
    case OpCode::CharSet: {
      auto &charSet = cast<CharSetIns>(*inst);
      str += "(matcherIndex=";
      str += std::to_string(charSet.getMatcherIndex());
      str += ", invert=";
      appendBool(str, charSet.invert);
      str += ')';
      inst += sizeof(CharSetIns);
      break;
    }
    case OpCode::ICharSet: {
      auto &charSet = cast<ICharSetIns>(*inst);
      str += "(matcherIndex=";
      str += std::to_string(charSet.getMatcherIndex());
      str += ", invert=";
      appendBool(str, charSet.invert);
      str += ')';
      inst += sizeof(CharSetIns);
      break;
    }
    case OpCode::BeginCapture:
      str += "(captureIndex=";
      str += std::to_string(cast<BeginCaptureIns>(*inst).getCaptureIndex());
      str += ')';
      inst += sizeof(BeginCaptureIns);
      break;
    case OpCode::EndCapture:
      str += "(captureIndex=";
      str += std::to_string(cast<EndCaptureIns>(*inst).getCaptureIndex());
      str += ')';
      inst += sizeof(EndCaptureIns);
      break;
    case OpCode::ResetCaptures:
      str += "(firstIndex=";
      str += std::to_string(cast<ResetCapturesIns>(*inst).getFirstIndex());
      str += ", lastIndex=";
      str += std::to_string(cast<ResetCapturesIns>(*inst).getLastIndex());
      str += ')';
      inst += sizeof(ResetCapturesIns);
      break;
    case OpCode::BackRef: {
      auto &backRef = cast<BackRefIns>(*inst);
      str += "(index=";
      str += std::to_string(backRef.getRefIndex());
      str += ", named=";
      appendBool(str, backRef.named);
      str += ')';
      inst += sizeof(BackRefIns);
      break;
    }
    case OpCode::IBackRef: {
      auto &backRef = cast<IBackRefIns>(*inst);
      str += "(index=";
      str += std::to_string(backRef.getRefIndex());
      str += ", named=";
      appendBool(str, backRef.named);
      str += ')';
      inst += sizeof(IBackRefIns);
      break;
    }
    case OpCode::BeginLoop: {
      auto &loop = cast<BeginLoopIns>(*inst);
      str += "(loopIndex=";
      str += std::to_string(loop.getLoopIndex());
      str += ", greedy=";
      appendBool(str, loop.greedy);
      str += ", min=";
      str += std::to_string(loop.getMin());
      str += ", max=";
      str += std::to_string(loop.getMax());
      str += ", outer=";
      str += std::to_string(loop.getOuter());
      str += ')';
      inst += sizeof(BeginLoopIns);
      break;
    }
    case OpCode::EndLoop:
      str += "(target=";
      str += std::to_string(cast<EndLoopIns>(*inst).getTarget());
      str += ')';
      inst += sizeof(EndLoopIns);
      break;
    case OpCode::BeginLookAhead:
      str += "(negate=";
      appendBool(str, cast<BeginLookAheadIns>(*inst).negate);
      str += ", target=";
      str += std::to_string(cast<BeginLookAheadIns>(*inst).getTarget());
      str += ')';
      inst += sizeof(BeginLookAheadIns);
      break;
    case OpCode::EndLookAhead:
      inst += sizeof(EndLookAheadIns);
      break;
    }
    this->base.dump(lineNum.c_str(), str);
  }
}

void RegexDumper::dump(ArrayRef<Matcher> matchers) {
  Padding padding(matchers.size());
  for (unsigned int i = 0; i < matchers.size(); i++) {
    std::string lineNum = padding(i);
    std::string str;
    toString(matchers[i], str);
    if (!str.empty() && str.back() == '\n') {
      str.pop_back();
    }
    this->base.dump(lineNum.c_str(), str);
  }
}

static void toString(const CodePointSetRef ref, std::string &out) {
  std::vector<std::pair<int, int>> ranges;
  for (auto &e : ref.getBMPRanges()) {
    ranges.emplace_back(e.firstBMP(), e.lastBMP());
  }
  for (auto &e : ref.getPackedNonBMPRanges()) {
    ranges.emplace_back(e.firstNonBMP(), e.lastNonBMP());
  }
  for (auto &e : ref.getNonBMPRanges()) {
    ranges.emplace_back(e.firstNonBMP(), e.lastNonBMP());
  }
  std::sort(ranges.begin(), ranges.end(), CodePointSetBuilder::Compare());

  for (auto &[first, last] : ranges) {
    char buf[128];
    snprintf(buf, std::size(buf), "{ 0x%04X, 0x%04X },\n", first, last);
    out += buf;
  }
}

void toString(const Matcher &matcher, std::string &out, bool putHeader) {
  if (putHeader) {
    switch (matcher.type()) {
    case MatcherType::ASCII:
      out += "AsciiSet\n";
      break;
    case MatcherType::OWNED_CODE_POINT_SET:
      out += "CodePointSet(owned)\n";
      break;
    case MatcherType::BORROWED_CODE_POINT_SET:
      out += "CodePointSet(borrowed)\n";
      break;
    }
  }
  CodePointSet set;
  if (matcher.type() == MatcherType::ASCII) {
    std::vector<int> codes;
    auto asciiSet = matcher.asAsciiSet();
    for (int c = 0; c <= 127; c++) {
      if (asciiSet.contains(c)) {
        codes.push_back(c);
      }
    }
    CodePointSetBuilder builder;
    builder.add(codes.data(), codes.size());
    set = builder.build();
  } else {
    auto ref = matcher.asCodePointSetRef();
    set = CodePointSet::borrow(ref);
  }
  toString(set.ref(), out);
}

} // namespace arsh::regex