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

#include "highlighter_base.h"

namespace arsh {

HighlightTokenClass toTokenClass(TokenKind kind) {
  switch (kind) {
  case TokenKind::COMMENT:
    return HighlightTokenClass::COMMENT;
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
  case TokenKind::TYPE:
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
  case TokenKind::IN:
    return HighlightTokenClass::OPERATOR;
#define GEN_CASE(E, P, A) case TokenKind::E:
    // clang-format off
  EACH_OPERATOR(GEN_CASE)
    // clang-format on
#undef GEN_CASE
    if (kind == TokenKind::COPROC || kind == TokenKind::TIME) {
      return HighlightTokenClass::KEYWORD;
    }
    return HighlightTokenClass::OPERATOR;
  case TokenKind::COMMAND:
    return HighlightTokenClass::COMMAND;
  case TokenKind::CMD_ARG_PART:
    return HighlightTokenClass::COMMAND_ARG;
  case TokenKind::TILDE:
  case TokenKind::META_ASSIGN:
  case TokenKind::META_COLON:
  case TokenKind::GLOB_ANY:
  case TokenKind::GLOB_ZERO_OR_MORE:
  case TokenKind::GLOB_BRACKET_OPEN:
  case TokenKind::GLOB_BRACKET_CLOSE:
  case TokenKind::BRACE_OPEN:
  case TokenKind::BRACE_SEP:
  case TokenKind::BRACE_CLOSE:
  case TokenKind::BRACE_CHAR_SEQ:
  case TokenKind::BRACE_INT_SEQ:
    return HighlightTokenClass::META;
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
  case TokenKind::HERE_START:
  case TokenKind::HERE_END:
    return HighlightTokenClass::STRING;
  case TokenKind::TYPE_NAME:
  case TokenKind::FUNC:
    return HighlightTokenClass::TYPE;
  case TokenKind::ATTR_OPEN:
  case TokenKind::ATTR_CLOSE:
  case TokenKind::ATTR_NAME:
    return HighlightTokenClass::ATTRIBUTE;
  default:
    break;
  }
  return HighlightTokenClass::NONE_;
}

static constexpr HighlightTokenRange::value_type highlightTokenTable[] = {
#define GEN_TABLE(E, S) {HighlightTokenClass::E, S},
    EACH_HIGHLIGHT_TOKEN_CLASS(GEN_TABLE)
#undef GEN_TABLE
};

HighlightTokenRange getHighlightTokenRange() { return HighlightTokenRange(highlightTokenTable); }

void TokenEmitter::operator()(Token token) {
  assert(token.size > 0);
  auto ref = this->source.substr(token.pos, token.size);
  auto kind = TokenKind::COMMENT;
  if (ref[0] != '#') {
    kind = TokenKind::ESCAPED_NL;
  }
  this->emit(kind, token);
}

void TokenEmitter::operator()(TokenKind kind, Token token) {
  if (token.pos > this->source.size()) {
    return;
  }

  auto ref = this->source.substr(token.pos, token.size);
  TokenKind suffix = TokenKind::EOS;
  if (kind == TokenKind::ENV_ASSIGN) {
    assert(ref.back() == '=');
    kind = TokenKind::IDENTIFIER;
    suffix = TokenKind::ASSIGN;
    token.size--;
  } else if (ref.size() > 2 && ref[0] == '$') {
    char ch = ref.back();
    if (ch == '[') {
      suffix = TokenKind::LB;
      token.size--;
    } else if (ch == '(') {
      suffix = TokenKind::LP;
      token.size--;
    }
  }
  if (token.size > 0) {
    this->emit(kind, token);
  }
  if (suffix != TokenKind::EOS) {
    unsigned int pos = token.endPos();
    this->emit(suffix, Token{.pos = pos, .size = 1});
  }
}

std::unique_ptr<ParseError> TokenEmitter::tokenizeAndEmit() {
  StringRef content = this->getSource();
  this->lexerPtr = LexerPtr::create("<dummy>", ByteBuffer(content.begin(), content.end()), nullptr);
  this->lexerPtr->setTriviaStore(makeObserver(*this));
  Parser parser(*this->lexerPtr, ParserOption::NEED_HERE_END | ParserOption::ERROR_RECOVER);
  parser.setTracker(this);
  parser();
  auto curToken = parser.getCurToken();
  this->oldErrors = std::move(parser).takeOldErrors();
  if (!this->oldErrors.empty() &&
      curToken.endPos() == this->oldErrors.back()->getErrorToken().endPos()) {
    auto e = std::move(this->oldErrors.back());
    this->oldErrors.pop_back();
    return e;
  }
  if (parser.hasError()) {
    return std::move(parser).takeError();
  }
  return nullptr;
}

void Tokenizer::emit(TokenKind kind, Token token) { this->tokens.emplace_back(kind, token); }

} // namespace arsh