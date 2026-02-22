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
  this->modifierStack.clear();
  this->modifierStack.push(tree.getFlag().modifiers());

  if (!this->generate(*tree.getPattern())) {
    return {};
  }

  // finalize
  this->builder.emit<MatchIns>();
  auto flag = tree.getFlag();
  auto count = tree.getCaptureGroupCount();
  return Regex(flag, count, std::move(this->builder).build(),
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
  case NodeKind::Property:
    this->todo(node);
    return false;
  case NodeKind::Boundary: {
    switch (cast<BoundaryNode>(node).getType()) {
    case BoundaryNode::Type::START:
      this->builder.emit<StartIns>(hasFlag(this->modifiers(), Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::END:
      this->builder.emit<EndIns>(hasFlag(this->modifiers(), Modifier::MULTILINE));
      return true;
    case BoundaryNode::Type::WORD:
    case BoundaryNode::Type::NOT_WORD:
      this->todo(node, "WORD boundary");
      return false;
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
    TRY(this->generateAlt(cast<AltNode>(node)));
    break;
  case NodeKind::LookAround:
    this->todo(node);
    return false;
  case NodeKind::Group: {
    auto &groupNode = cast<GroupNode>(node);
    switch (groupNode.getType()) {
    case GroupNode::Type::CAPTURE:
      this->todo(node, "support capture group");
      return false;
    case GroupNode::Type::NON_CAPTURE:
      break;
    case GroupNode::Type::MODIFIER: {
      auto newModifier = this->modifiers();
      if (auto set = groupNode.getSetModifiers(); set != Modifier::NONE) {
        setFlag(newModifier, set);
      }
      if (auto unset = groupNode.getUnsetModifiers(); unset != Modifier::NONE) {
        unsetFlag(newModifier, unset);
      }
      this->modifierStack.push(newModifier);
      break;
    }
    }
    TRY(this->generate(*groupNode.getPattern()));
    if (groupNode.getType() == GroupNode::Type::MODIFIER) {
      this->modifierStack.pop();
    }
    break;
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

} // namespace arsh::regex