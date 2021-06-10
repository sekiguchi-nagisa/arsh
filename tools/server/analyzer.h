/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_SERVER_ANALYZER_H
#define YDSH_TOOLS_SERVER_ANALYZER_H

#include <parser.h>
#include <type_checker.h>

#include "ast.h"

namespace ydsh::lsp {

class Analyzer {
public:
  enum class Result {
    SUSPENDED,
    TERMINATED,
  };

  enum class State {
    INIT,
    REACH_SOURCE,
    TERMINATED,
  };

private:
  ASTContextPtr ctx;
  Lexer lexer;
  Parser parser;
  TypeChecker checker;
  DSType *prevType{nullptr};
  std::unique_ptr<SourceListNode> srcListNode;
  std::vector<std::unique_ptr<Node>> nodes;
  State state{State::INIT}; // only updated by run()

public:
  explicit Analyzer(ASTContextPtr ctx);

  Result run();

private:
  bool hasUnconsumedPath() const {
    return this->srcListNode && this->srcListNode->hasUnconsumedPath();
  }

  State runParseAndCheck();

  State runImport();
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_SERVER_ANALYZER_H
