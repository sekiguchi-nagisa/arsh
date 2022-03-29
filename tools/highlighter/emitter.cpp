/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#include "emitter.h"

namespace ydsh {

HighlightTokenClass toTokenClass(TokenKind kind) {
  switch (kind) {
  case TokenKind::ALIAS:
  case TokenKind::ASSERT:
  case TokenKind::BREAK:
  case TokenKind::CASE:
  case TokenKind::CATCH:
  case TokenKind::CONTINUE:
  case TokenKind::DEFER:
  case TokenKind::DO:
  case TokenKind::ELIF:
  case TokenKind::ELSE:
  case TokenKind::EXPORT_ENV:
  case TokenKind::FINALLY:
  case TokenKind::FOR:
  case TokenKind::FUNCTION:
  case TokenKind::IF:
  case TokenKind::IMPORT_ENV:
  case TokenKind::LET:
  case TokenKind::NEW:
  case TokenKind::RETURN:
  case TokenKind::SOURCE:
  case TokenKind::SOURCE_OPT:
  case TokenKind::TRY:
  case TokenKind::THROW:
  case TokenKind::TYPEDEF:
  case TokenKind::VAR:
  case TokenKind::WHILE:
  case TokenKind::TYPEOF:
  case TokenKind::INLINED:
    return HighlightTokenClass::KEYWORD;
  case TokenKind::PLUS:
  case TokenKind::MINUS:
  case TokenKind::NOT:
  case TokenKind::INC:
  case TokenKind::DEC:
  case TokenKind::UNWRAP:
  case TokenKind::CASE_ARM:
    return HighlightTokenClass::OPERATOR;
#define GEN_CASE(E, P, A) case TokenKind::E:
    // clang-format off
  EACH_OPERATOR(GEN_CASE)
    // clang-format on
#undef GEN_CASE
    if (kind == TokenKind::COPROC) {
      return HighlightTokenClass::KEYWORD;
    }
    return HighlightTokenClass::OPERATOR;
  case TokenKind::COMMAND:
    return HighlightTokenClass::COMMAND;
  case TokenKind::CMD_ARG_PART:
  case TokenKind::PATH_SEP:
  case TokenKind::GLOB_ANY:
  case TokenKind::GLOB_ZERO_OR_MORE:
    return HighlightTokenClass::COMMAND_ARG;
#define GEN_CASE(E) case TokenKind::E:
    // clang-format off
  EACH_LA_redir(GEN_CASE)
    return HighlightTokenClass::REDIRECT;
    // clang-format on
#undef GEN_CASE
  case TokenKind::INT_LITERAL:
  case TokenKind::FLOAT_LITERAL:
    return HighlightTokenClass::NUMBER;
  case TokenKind::REGEX_LITERAL:
    return HighlightTokenClass::REGEX;
  case TokenKind::APPLIED_NAME:
  case TokenKind::APPLIED_NAME_WITH_BRACKET:
  case TokenKind::APPLIED_NAME_WITH_FIELD:
  case TokenKind::APPLIED_NAME_WITH_PAREN:
  case TokenKind::SPECIAL_NAME:
  case TokenKind::SPECIAL_NAME_WITH_BRACKET:
    return HighlightTokenClass::VARIABLE;
  case TokenKind::STRING_LITERAL:
  case TokenKind::OPEN_DQUOTE:
  case TokenKind::CLOSE_DQUOTE:
  case TokenKind::STR_ELEMENT:
    return HighlightTokenClass::STRING;
  case TokenKind::SIGNAL_LITERAL:
    return HighlightTokenClass::SIGNAL;
  case TokenKind::TYPE_NAME:
  case TokenKind::FUNC:
    return HighlightTokenClass::TYPE;
  default:
    break;
  }
  return HighlightTokenClass::NONE;
}

void TokenEmitter::operator()(Token token) {
  assert(this->source[token.pos] == '#');
  this->emit(HighlightTokenClass::COMMENT, token);
}

void TokenEmitter::operator()(TokenKind kind, Token token) {
  if (token.pos > this->source.size()) {
    return;
  }

  auto ref = this->source.substr(token.pos, token.size);
  TokenKind suffix = TokenKind::EOS;
  if (ref.size() > 2 && ref[0] == '$') {
    char ch = ref[ref.size() - 1];
    if (ch == '[') {
      suffix = TokenKind::LB;
      token.size--;
    } else if (ch == '(') {
      suffix = TokenKind::LP;
      token.size--;
    }
  }
  if (token.size > 0) {
    this->emit(toTokenClass(kind), token);
  }
  if (suffix != TokenKind::EOS) {
    unsigned int pos = token.endPos();
    this->emit(toTokenClass(suffix), Token{.pos = pos, .size = 1});
  }
}

} // namespace ydsh