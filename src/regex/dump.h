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

#ifndef ARSH_REGEX_DUMP_H
#define ARSH_REGEX_DUMP_H

#include "misc/string_ref.hpp"
#include "misc/token.hpp"
#include "node.h"

#include <memory>
#include <vector>

namespace arsh::regex {

class TreeDumper {
private:
  std::string buf;
  unsigned int indentLevel{0};

public:
  TreeDumper() = default;

  std::string operator()(const SyntaxTree &tree);

private:
  void enterIndent() { this->indentLevel++; }

  void leaveIndent() { this->indentLevel--; }

  void newline() { this->append('\n'); }

  void indent() { this->buf.append(this->indentLevel * 2, ' '); }

  void append(StringRef ref) { this->buf += ref; }

  void append(char ch) { this->buf += ch; }

  void field(const char *name) {
    this->indent();
    this->append(name);
    this->append(':');
  }

  void dump(const char *fieldName, StringRef ref) {
    this->field(fieldName);
    this->append(' ');
    this->append(ref);
    this->newline();
  }

  void dump(const char *fieldName, const char *str) { this->dump(fieldName, StringRef(str)); }

  void dumpAs(const char *fieldName, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

  void dump(const char *fieldName, Flag flag);

  void dump(const char *fieldName, Token token) { this->dump(fieldName, token.str()); }

  void dump(const char *fieldName, bool v) { this->dump(fieldName, v ? "true" : "false"); }

  void dump(const char *fieldName, const Node &node) {
    this->field(fieldName);
    this->newline();
    this->enterIndent();
    this->dump(node, false);
    this->leaveIndent();
  }

  void dump(const Node &node, const bool inArray) {
    this->indent();
    this->dumpNodeHeader(node, inArray);
    if (inArray) {
      this->enterIndent();
    }
    this->dumpRaw(node);
    if (inArray) {
      this->leaveIndent();
    }
  }

  void dump(const char *fieldName, const std::vector<std::unique_ptr<Node>> &nodes);

  void dumpRaw(const Node &node);

  void dumpRaw(const PropertyNode &node);

  void dumpNodeHeader(const Node &node, bool inArray);

  void dumpNodesHead(const char *fieldName) {
    this->field(fieldName);
    this->newline();
    this->enterIndent();
  }

  void dumpNodesBody(const Node &node) {
    this->indent();
    this->dumpNodeHeader(node, true);
    this->enterIndent();
    this->dumpRaw(node);
    this->leaveIndent();
  }

  void dumpNodesTail() { this->leaveIndent(); }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_DUMP_H
