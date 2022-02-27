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

HightlightTokenClass toTokenClass(TokenKind kind) {
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
    return HightlightTokenClass::KEYWORD;
  case TokenKind::PLUS:
  case TokenKind::MINUS:
  case TokenKind::NOT:
  case TokenKind::INC:
  case TokenKind::DEC:
  case TokenKind::UNWRAP:
  case TokenKind::CASE_ARM:
    return HightlightTokenClass::OPERATOR;
#define GEN_CASE(E, P, A) case TokenKind::E:
    // clang-format off
  EACH_OPERATOR(GEN_CASE)
    // clang-format on
#undef GEN_CASE
    if (kind == TokenKind::COPROC) {
      return HightlightTokenClass::KEYWORD;
    }
    return HightlightTokenClass::OPERATOR;
  case TokenKind::COMMAND:
    return HightlightTokenClass::COMMAND;
  case TokenKind::CMD_ARG_PART:
  case TokenKind::PATH_SEP:
  case TokenKind::GLOB_ANY:
  case TokenKind::GLOB_ZERO_OR_MORE:
    return HightlightTokenClass::COMMAND_ARG;
#define GEN_CASE(E) case TokenKind::E:
    // clang-format off
  EACH_LA_redir(GEN_CASE)
    return HightlightTokenClass::REDIRECT;
    // clang-format on
#undef GEN_CASE
  case TokenKind::INT_LITERAL:
  case TokenKind::FLOAT_LITERAL:
    return HightlightTokenClass::NUMBER;
  case TokenKind::REGEX_LITERAL:
    return HightlightTokenClass::REGEX;
  case TokenKind::APPLIED_NAME:
  case TokenKind::APPLIED_NAME_WITH_BRACKET:
  case TokenKind::APPLIED_NAME_WITH_FIELD:
  case TokenKind::APPLIED_NAME_WITH_PAREN:
  case TokenKind::SPECIAL_NAME:
  case TokenKind::SPECIAL_NAME_WITH_BRACKET:
    return HightlightTokenClass::VARIABLE;
  case TokenKind::STRING_LITERAL:
  case TokenKind::OPEN_DQUOTE:
  case TokenKind::CLOSE_DQUOTE:
  case TokenKind::STR_ELEMENT:
    return HightlightTokenClass::STRING;
  case TokenKind::SIGNAL_LITERAL:
    return HightlightTokenClass::SIGNAL;
  case TokenKind::TYPE_NAME:
    return HightlightTokenClass::TYPE;
  default:
    break;
  }
  return HightlightTokenClass::NONE;
}

void TokenEmitter::operator()(Token token) {
  assert(this->source[token.pos] == '#');
  this->emit(HightlightTokenClass::COMMENT, token);
}

void TokenEmitter::operator()(TokenKind kind, Token token) {
  assert(token.endPos() < this->source.size());

  auto ref = this->source.substr(token.pos, token.size);
  if (ref.size() > 2 && ref[0] == '$') {
    char ch = ref[ref.size() - 1];
    if (ch == '[' || ch == '(') {
      token.size--;
    }
  }
  if (token.size > 0) {
    this->emit(toTokenClass(kind), token);
  }
}

} // namespace ydsh