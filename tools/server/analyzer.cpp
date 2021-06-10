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

#include "analyzer.h"

namespace ydsh::lsp {

// ######################
// ##     Analyzer     ##
// ######################

static Lexer createLexer(const Source &src) {
  StringRef content(src.getContent());
  ByteBuffer buf(content.begin(), content.end());
  const char *fileName = src.getFileName().c_str();
  char *real = strdup(fileName);
  const char *ptr = strrchr(real, '/');
  real[ptr == real ? 1 : (ptr - real)] = '\0';
  CStrPtr scriptDir(real);
  return Lexer(fileName, std::move(buf), std::move(scriptDir));
}

Analyzer::Analyzer(ASTContextPtr ctx)
    : ctx(ctx), lexer(createLexer(this->ctx->getSource())), parser(this->lexer),
      checker(this->ctx->getPool(), false, &this->lexer) {}

Analyzer::Result Analyzer::run() {
  switch (this->state) {
  case State::INIT:
    this->state = this->runParseAndCheck();
    return this->state == State::REACH_SOURCE ? Result::SUSPENDED : Result::TERMINATED;
  case State::REACH_SOURCE:
    this->state = this->runParseAndCheck(); // FIXME: resolve module
    return this->state == State::REACH_SOURCE ? Result::SUSPENDED : Result::TERMINATED;
  case State::TERMINATED:
    break;
  }
  return Result::TERMINATED;
}

Analyzer::State Analyzer::runParseAndCheck() {
  while (this->parser) {
    auto node = this->parser();
    if (this->parser.hasError()) {
      break; // FIXME: error recovery
    }

    try {
      node = this->checker(this->prevType, std::move(node), this->ctx->getScope());
      this->prevType = &node->getType();
    } catch (const TypeCheckError &e) {
      break; // FIXME: error recovery
    }
  }
  return State::TERMINATED;
}

} // namespace ydsh::lsp