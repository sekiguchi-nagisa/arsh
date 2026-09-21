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

#include "js.h"
#include "js_lexer.h"
#include "js_regex.h"

#include <constant.h>
#include <misc/parser_base.hpp>

namespace arsh::re262 {

struct Node;

struct NullLiteral {};

struct BoolLiteral {
  bool value;
};

struct NumberLiteral {
  double value;
};

struct StringLiteral {
  JSStringPtr value;
};

struct RegexLiteral {
  JSRegexPtr value;
};

struct ArrayLiteral {
  std::vector<std::unique_ptr<Node>> values;
};

struct ObjectLiteral {
  std::vector<std::pair<std::string, std::unique_ptr<Node>>> values;
};

struct FuncLiteral {
  std::string name;
  std::vector<std::string> params;
  std::shared_ptr<std::vector<std::unique_ptr<Node>>> nodes;
};

struct NameExpr {
  std::string name;
};

struct AccessExpr {
  std::unique_ptr<Node> recv;
  std::string name;
};

struct IndexExpr {
  std::unique_ptr<Node> recv;
  std::unique_ptr<Node> index;
};

struct CallExpr {
  std::unique_ptr<Node> func;
  std::vector<std::unique_ptr<Node>> args;
  bool newExpr{false};
};

struct UnaryExpr {
  JSTokenKind op;
  std::unique_ptr<Node> expr;
};

struct BinaryExpr {
  std::unique_ptr<Node> left;
  JSTokenKind op;
  std::unique_ptr<Node> right;
};

struct AssignExpr {
  std::unique_ptr<Node> left;  // maybe null if prefix ++, --
  JSTokenKind op;              // in addition to assign op, maybe ++, --
  std::unique_ptr<Node> right; // maybe null if suffix ++, --
};

struct TemplateExpr {
  std::vector<std::unique_ptr<Node>> nodes;
};

struct VarDecl { // currently only support `const`
  enum class Kind : unsigned char {
    CONST,
    LET,
    VAR,
  } kind;

  std::string name;
  std::unique_ptr<Node> expr;
};

struct JumpStmt {
  JSResult::Status status;
  std::unique_ptr<Node> expr; // maybe null
};

struct BlockStmt {
  std::vector<std::unique_ptr<Node>> nodes;
};

struct TryStmt {
  std::unique_ptr<Node> tryBlock;     // must be BlockStmt
  std::string except;                 // for caught exception (maybe empty)
  std::unique_ptr<Node> catchBlock;   // must be BlockStmt. maybe null
  std::unique_ptr<Node> finallyBlock; // must be BlockStmt. maybe null
};

struct IfStmt {
  std::unique_ptr<Node> cond;
  std::unique_ptr<Node> thenStmt;
  std::unique_ptr<Node> elseStmt; // maybe null
};

struct ForStmt {
  std::unique_ptr<Node> init;  // maybe null
  std::unique_ptr<Node> cond;  // maybe null
  std::unique_ptr<Node> after; // maybe null
  std::unique_ptr<Node> body;
};

struct ForOfStmt {
  std::unique_ptr<Node> iter; // must be VarDecl
  std::unique_ptr<Node> body;
};

struct SwitchCase {
  struct Case {
    std::unique_ptr<Node> label; // if default, null
    std::vector<std::unique_ptr<Node>> body;
  };

  std::unique_ptr<Node> expr;
  std::vector<Case> caseClauses;
  bool hasDefault{false};
  unsigned int defaultIndex{0};
};

struct Node {
  unsigned int lineNum;

  using Underlying =
      std::variant<NullLiteral, BoolLiteral, NumberLiteral, StringLiteral, RegexLiteral,
                   ArrayLiteral, ObjectLiteral, FuncLiteral, NameExpr, AccessExpr, IndexExpr,
                   CallExpr, UnaryExpr, BinaryExpr, AssignExpr, TemplateExpr, VarDecl, JumpStmt,
                   BlockStmt, TryStmt, IfStmt, ForStmt, ForOfStmt, SwitchCase>;
  Underlying value;

  Node(unsigned int lineNum, Underlying v) : lineNum(lineNum), value(std::move(v)) {}
};

// ######################
// ##     JSParser     ##
// ######################

#define EACH_LA_JS_PRIMARY_NO_FUNC(OP)                                                             \
  OP(NIL)                                                                                          \
  OP(TRUE)                                                                                         \
  OP(FALSE)                                                                                        \
  OP(NUMBER)                                                                                       \
  OP(STRING)                                                                                       \
  OP(REGEX)                                                                                        \
  OP(IDENTIFIER)                                                                                   \
  OP(LB)                                                                                           \
  OP(LBC)                                                                                          \
  OP(LP)                                                                                           \
  OP(BACKTICK)

#define EACH_LA_JS_PRIMARY(OP)                                                                     \
  EACH_LA_JS_PRIMARY_NO_FUNC(OP)                                                                   \
  OP(FUNCTION)

#define EACH_LA_JS_EXPRESSION_NO_FUNC(OP)                                                          \
  OP(NOT)                                                                                          \
  OP(ADD)                                                                                          \
  OP(SUB)                                                                                          \
  OP(NEW)                                                                                          \
  OP(VOID)                                                                                         \
  OP(TYPEOF)                                                                                       \
  OP(INC)                                                                                          \
  OP(DEC)                                                                                          \
  EACH_LA_JS_PRIMARY_NO_FUNC(OP)

#define EACH_LA_JS_EXPRESSION(OP)                                                                  \
  EACH_LA_JS_PRIMARY_NO_FUNC(OP)                                                                   \
  OP(FUNCTION)

#define EACH_LA_JS_VAR_DECL(OP)                                                                    \
  OP(CONST)                                                                                        \
  OP(LET)                                                                                          \
  OP(VAR)

#define EACH_LA_JS_STATEMENT(OP)                                                                   \
  EACH_LA_JS_VAR_DECL(OP)                                                                          \
  OP(RETURN)                                                                                       \
  OP(THROW)                                                                                        \
  OP(TRY)                                                                                          \
  OP(IF)                                                                                           \
  OP(FOR)                                                                                          \
  OP(WHILE)                                                                                        \
  OP(SWITCH)                                                                                       \
  OP(BREAK)                                                                                        \
  OP(CONTINUE)                                                                                     \
  OP(FUNCTION)                                                                                     \
  EACH_LA_JS_EXPRESSION_NO_FUNC(OP)

#define GEN_LA_CASE(CASE) case JSTokenKind::CASE:
#define GEN_LA_ALTER(CASE) JSTokenKind::CASE,

#define E_ALTER(...)                                                                               \
  do {                                                                                             \
    this->reportNoViableAlterError((JSTokenKind[]){__VA_ARGS__});                                  \
    return nullptr;                                                                                \
  } while (false)

#define TRY(expr)                                                                                  \
  ({                                                                                               \
    auto v = expr;                                                                                 \
    if (unlikely(this->hasError())) {                                                              \
      return nullptr;                                                                              \
    }                                                                                              \
    std::forward<decltype(v)>(v);                                                                  \
  })

class JSParser : public ParserBase<JSTokenKind, JSLexer> {
private:
  std::shared_ptr<JSEnv> global;

public:
  struct Error {
    std::string sourceName;
    unsigned int lineNum;
    std::string message;
    std::string detail;
  };

  JSParser(const std::shared_ptr<JSEnv> &global, JSLexer &lex) : global(global) {
    this->lexer = &lex;
    this->fetchNext();
  }

  std::unique_ptr<Node> operator()() { return this->parseStatement(); }

  explicit operator bool() const { return !isEOSToken(this->curKind); }

  std::optional<Error> formatError() const;

private:
  Token expectVarDeclIdentifier();

  std::unique_ptr<Node> parseStatement();

  std::nullptr_t expectStatementEnd();

  std::unique_ptr<Node> parseBlock();

  std::unique_ptr<Node> parseTryStatement();

  std::unique_ptr<Node> parseIfStatement();

  std::unique_ptr<Node> parseForStatement();

  std::unique_ptr<Node> parseWhileStatement();

  std::unique_ptr<Node> parseSwitchCaseStatement();

  std::unique_ptr<Node> parseExpression() {
    return this->parseExpression(getOperatorInfo(JSTokenKind::ASSIGN).precedence);
  }

  std::unique_ptr<Node> parseExpression(JSOperatorPrecedence base);

  std::unique_ptr<Node> parseUnaryExpression();

  std::unique_ptr<Node> parseCallExpression();

  std::unique_ptr<Node> parseMemberExpression();

  std::unique_ptr<Node> parseMemberAccess(std::unique_ptr<Node> &&node);

  std::unique_ptr<Node> parseWithArguments(std::unique_ptr<Node> &&node, bool isNew = false);

  std::unique_ptr<Node> parsePrimary();

  std::unique_ptr<Node> parseNumber();

  std::unique_ptr<Node> parseString(bool trimQuote = true);

  std::unique_ptr<Node> parseObject();

  std::unique_ptr<Node> parseArray();

  std::unique_ptr<Node> parseFunction();

  std::unique_ptr<Node> parseTemplate();
};

Token JSParser::expectVarDeclIdentifier() {
  auto token = this->expect(JSTokenKind::IDENTIFIER);
  if (!this->hasError()) {
    if (this->lexer->toStrRef(token) == "arguments") {
      this->reportTokenFormatError(JSTokenKind::IDENTIFIER, token, "unexpected `arguments'");
    }
  }
  return token;
}

std::optional<JSParser::Error> JSParser::formatError() const {
  if (!this->hasError()) {
    return {};
  }

  auto errorToken = this->lexer->shiftEOS(this->getError().getErrorToken());
  const unsigned int lineNum = this->lexer->getLineNumByPos(errorToken.pos);
  std::string str;
  str += this->lexer->getSourceName();
  str += ':';
  str += std::to_string(lineNum);
  str += " [error] ";
  str += this->getError().getMessage();
  str += '\n';

  auto lineToken = this->lexer->getLineToken(errorToken);

  str += this->lexer->formatTokenText(lineToken);
  str += this->lexer->formatLineMarker(lineToken, errorToken);
  str += '\n';

  Error err = {
      .sourceName = this->lexer->getSourceName(),
      .lineNum = lineNum,
      .message = this->getError().getMessage(),
      .detail = std::move(str),
  };
  return err;
}

static VarDecl::Kind toVarKind(JSTokenKind kind) {
  switch (kind) {
  case JSTokenKind::CONST:
    return VarDecl::Kind::CONST;
  case JSTokenKind::LET:
    return VarDecl::Kind::LET;
  default:
    break;
  }
  return VarDecl::Kind::VAR;
}

std::unique_ptr<Node> JSParser::parseStatement() {
  switch (this->curKind) {
    EACH_LA_JS_VAR_DECL(GEN_LA_CASE) {
      const auto kind = toVarKind(this->curKind);
      this->consume();
      Token token = TRY(this->expectVarDeclIdentifier());
      std::unique_ptr<Node> expr;
      if (this->curKind == JSTokenKind::ASSIGN) {
        TRY(this->expect(JSTokenKind::ASSIGN));
        expr = TRY(this->parseExpression());
      }
      TRY(this->expectStatementEnd());
      return std::make_unique<Node>(
          this->lexer->getLineNumByPos(token.pos),
          VarDecl{kind, this->lexer->toTokenText(token), std::move(expr)});
    }
  case JSTokenKind::RETURN: {
    Token token = TRY(this->expect(JSTokenKind::RETURN));
    std::unique_ptr<Node> node;
    if (!this->lexer->hasPrevNewLine() && this->curKind != JSTokenKind::LINE_END &&
        this->curKind != JSTokenKind::RBC && !isEOSToken(this->curKind)) {
      node = TRY(this->parseExpression());
    }
    TRY(this->expectStatementEnd());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::RETURN, std::move(node)});
  }
  case JSTokenKind::THROW: {
    Token token = TRY(this->expect(JSTokenKind::THROW));
    auto node = TRY(this->parseExpression());
    TRY(this->expectStatementEnd());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::ERR, std::move(node)});
  }
  case JSTokenKind::TRY:
    return this->parseTryStatement();
  case JSTokenKind::IF:
    return this->parseIfStatement();
  case JSTokenKind::BREAK: {
    Token token = TRY(this->expect(JSTokenKind::BREAK));
    TRY(this->expectStatementEnd());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::BREAK, nullptr});
  }
  case JSTokenKind::CONTINUE: {
    Token token = TRY(this->expect(JSTokenKind::CONTINUE));
    TRY(this->expectStatementEnd());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  JumpStmt{JSResult::Status::CONTINUE, nullptr});
  }
  case JSTokenKind::WHILE:
    return this->parseWhileStatement();
  case JSTokenKind::FOR:
    return this->parseForStatement();
  case JSTokenKind::SWITCH:
    return this->parseSwitchCaseStatement();
  case JSTokenKind::FUNCTION: { // for function decl
    Token token = this->curToken;
    auto node = TRY(this->parseFunction());
    auto &func = std::get<FuncLiteral>(node->value);
    if (func.name.empty()) {
      this->reportTokenFormatError(JSTokenKind::FUNCTION, token,
                                   "function declaration requires a name");
      return nullptr;
    }
    TRY(this->expectStatementEnd());
    const unsigned int lineNum = node->lineNum;
    std::string name = func.name;
    return std::make_unique<Node>(lineNum,
                                  VarDecl{VarDecl::Kind::VAR, std::move(name), std::move(node)});
  }
    // clang-format off
  EACH_LA_JS_EXPRESSION_NO_FUNC(GEN_LA_CASE) {
    auto expr = TRY(this->parseExpression());
    TRY(this->expectStatementEnd());
    return expr;
  }
    // clang-format on
  default:
    E_ALTER(EACH_LA_JS_STATEMENT(GEN_LA_ALTER));
  }
}

std::nullptr_t JSParser::expectStatementEnd() {
  if (this->curKind == JSTokenKind::LINE_END) {
    this->consume();
  } else if (this->lexer->hasPrevNewLine() || this->curKind == JSTokenKind::RBC ||
             isEOSToken(this->curKind)) {
  } else {
    this->expect(JSTokenKind::LINE_END); // for error message
  }
  return nullptr;
}

std::unique_ptr<Node> JSParser::parseBlock() {
  std::vector<std::unique_ptr<Node>> nodes;
  auto token = TRY(this->expect(JSTokenKind::LBC));
  while (this->curKind != JSTokenKind::RBC) {
    auto node = TRY(this->parseStatement());
    nodes.push_back(std::move(node));
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                BlockStmt{std::move(nodes)});
}

std::unique_ptr<Node> JSParser::parseIfStatement() {
  auto token = TRY(this->expect(JSTokenKind::IF));
  TRY(this->expect(JSTokenKind::LP));
  auto cond = TRY(this->parseExpression());
  TRY(this->expect(JSTokenKind::RP));
  std::unique_ptr<Node> thenStmt;
  if (this->curKind == JSTokenKind::LBC) {
    thenStmt = TRY(this->parseBlock());
  } else {
    thenStmt = TRY(this->parseStatement());
  }
  std::unique_ptr<Node> elseStmt;
  if (this->curKind == JSTokenKind::ELSE) {
    this->consume();
    if (this->curKind == JSTokenKind::LBC) {
      elseStmt = TRY(this->parseBlock());
    } else {
      elseStmt = TRY(this->parseStatement());
    }
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                IfStmt{
                                    .cond = std::move(cond),
                                    .thenStmt = std::move(thenStmt),
                                    .elseStmt = std::move(elseStmt),
                                });
}

std::unique_ptr<Node> JSParser::parseForStatement() {
  Token start = TRY(this->expect(JSTokenKind::FOR));
  bool forOf = false;
  std::unique_ptr<Node> init;
  TRY(this->expect(JSTokenKind::LP));
  switch (this->curKind) {
    EACH_LA_JS_VAR_DECL(GEN_LA_CASE) {
      const auto kind = toVarKind(this->curKind);
      this->consume();
      Token token = TRY(this->expectVarDeclIdentifier());
      std::unique_ptr<Node> expr;
      if (this->curKind == JSTokenKind::OF) {
        forOf = true;
        this->consume();
        expr = TRY(this->parseExpression());
      } else if (this->curKind == JSTokenKind::ASSIGN) {
        this->consume();
        expr = TRY(this->parseExpression());
      }
      if (!forOf) {
        TRY(this->expect(JSTokenKind::LINE_END));
      }
      init =
          std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                 VarDecl{kind, this->lexer->toTokenText(token), std::move(expr)});
      break;
    }
  default:
    if (this->curKind != JSTokenKind::LINE_END) {
      init = TRY(this->parseExpression());
    }
    TRY(this->expect(JSTokenKind::LINE_END));
    break;
  }
  std::unique_ptr<Node> cond;
  std::unique_ptr<Node> after;
  if (!forOf) {
    cond = TRY(this->parseExpression());
    TRY(this->expect(JSTokenKind::LINE_END));
    if (this->curKind != JSTokenKind::RP) {
      after = TRY(this->parseExpression());
    }
  }
  TRY(this->expect(JSTokenKind::RP));
  std::unique_ptr<Node> body;
  if (this->curKind == JSTokenKind::LBC) {
    body = TRY(this->parseBlock());
  } else {
    body = TRY(this->parseStatement());
  }
  if (forOf) {
    return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos),
                                  ForOfStmt{
                                      .iter = std::move(init),
                                      .body = std::move(body),
                                  });
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos),
                                ForStmt{
                                    .init = std::move(init),
                                    .cond = std::move(cond),
                                    .after = std::move(after),
                                    .body = std::move(body),
                                });
}

std::unique_ptr<Node> JSParser::parseWhileStatement() {
  Token token = TRY(this->expect(JSTokenKind::WHILE));
  TRY(this->expect(JSTokenKind::LP));
  auto cond = TRY(this->parseExpression());
  TRY(this->expect(JSTokenKind::RP));
  std::unique_ptr<Node> body;
  if (this->curKind == JSTokenKind::LBC) {
    body = TRY(this->parseBlock());
  } else {
    body = TRY(this->parseStatement());
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                ForStmt{
                                    .init = nullptr,
                                    .cond = std::move(cond),
                                    .after = nullptr,
                                    .body = std::move(body),
                                });
}

std::unique_ptr<Node> JSParser::parseTryStatement() {
  Token token = TRY(this->expect(JSTokenKind::TRY));
  auto tryBlock = TRY(this->parseBlock());
  std::string except;
  std::unique_ptr<Node> catchBlock;
  if (this->curKind == JSTokenKind::CATCH) {
    TRY(this->expect(JSTokenKind::CATCH));
    if (this->curKind == JSTokenKind::LP) {
      TRY(this->expect(JSTokenKind::LP));
      except = this->lexer->toTokenText(TRY(this->expectVarDeclIdentifier()));
      TRY(this->expect(JSTokenKind::RP));
    }
    catchBlock = TRY(this->parseBlock());
  }
  std::unique_ptr<Node> finallyBlock;
  if (this->curKind == JSTokenKind::FINALLY) {
    TRY(this->expect(JSTokenKind::FINALLY));
    finallyBlock = TRY(this->parseBlock());
  } else if (!catchBlock) {
    E_ALTER(JSTokenKind::CATCH, JSTokenKind::FINALLY);
  }
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                TryStmt{
                                    .tryBlock = std::move(tryBlock),
                                    .except = std::move(except),
                                    .catchBlock = std::move(catchBlock),
                                    .finallyBlock = std::move(finallyBlock),
                                });
}

static bool withinCaseClause(const JSTokenKind kind) {
  return kind != JSTokenKind::CASE && kind != JSTokenKind::DEFAULT && kind != JSTokenKind::RBC;
}

std::unique_ptr<Node> JSParser::parseSwitchCaseStatement() { // TODO:
  auto token = TRY(this->expect(JSTokenKind::SWITCH));
  SwitchCase switchCase;
  TRY(this->expect(JSTokenKind::LP));
  switchCase.expr = TRY(this->parseExpression());
  TRY(this->expect(JSTokenKind::RP));
  TRY(this->expect(JSTokenKind::LBC));
  while (this->curKind != JSTokenKind::RBC) {
    switch (this->curKind) {
    case JSTokenKind::CASE: {
      this->consume();
      SwitchCase::Case caseClause;
      caseClause.label = TRY(this->parseExpression());
      TRY(this->expect(JSTokenKind::COLON));
      while (withinCaseClause(this->curKind)) {
        caseClause.body.push_back(TRY(this->parseStatement()));
      }
      switchCase.caseClauses.push_back(std::move(caseClause));
      continue;
    }
    case JSTokenKind::DEFAULT: {
      if (switchCase.hasDefault) {
        this->reportTokenFormatError(this->curKind, this->curToken,
                                     "more than one default clause in switch statement");
        return nullptr;
      }
      this->consume();
      SwitchCase::Case defaultClause;
      TRY(this->expect(JSTokenKind::COLON));
      while (withinCaseClause(this->curKind)) {
        defaultClause.body.push_back(TRY(this->parseStatement()));
      }
      switchCase.hasDefault = true;
      switchCase.defaultIndex = switchCase.caseClauses.size();
      switchCase.caseClauses.push_back(std::move(defaultClause));
      continue;
    }
    default:
      E_ALTER(JSTokenKind::CASE, JSTokenKind::DEFAULT);
    }
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), std::move(switchCase));
}

static bool isAssignable(const Node &node) {
  return std::holds_alternative<NameExpr>(node.value) ||
         std::holds_alternative<AccessExpr>(node.value) ||
         std::holds_alternative<IndexExpr>(node.value);
}

std::unique_ptr<Node> JSParser::parseExpression(JSOperatorPrecedence base) {
  auto node = TRY(this->parseUnaryExpression());
  while (isOperator(this->curKind)) {
    const auto info = getOperatorInfo(this->curKind);
    if (!hasFlag(info.attr, JSOperatorAttr::INFIX) || info.precedence < base) {
      break;
    }
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    const auto next =
        hasFlag(info.attr, JSOperatorAttr::RASSOC) ? info.precedence : advance(info.precedence);
    auto rightNode = this->parseExpression(next);
    unsigned int lineNum = node->lineNum;
    if (isAssignOp(kind)) {
      if (!isAssignable(*node)) {
        this->reportTokenFormatError(kind, token, "invalid left-hand side of assignment");
        return nullptr;
      }
      node =
          std::make_unique<Node>(lineNum, AssignExpr{std::move(node), kind, std::move(rightNode)});
    } else {
      node =
          std::make_unique<Node>(lineNum, BinaryExpr{std::move(node), kind, std::move(rightNode)});
    }
  }
  return node;
}

std::unique_ptr<Node> JSParser::parseUnaryExpression() {
  switch (this->curKind) {
  case JSTokenKind::NOT:
  case JSTokenKind::ADD:
  case JSTokenKind::SUB:
  case JSTokenKind::VOID:
  case JSTokenKind::TYPEOF: {
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    auto expr = TRY(this->parseUnaryExpression());
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  UnaryExpr{kind, std::move(expr)});
  }
  case JSTokenKind::INC:
  case JSTokenKind::DEC: {
    Token token = this->curToken;
    JSTokenKind kind = this->scan();
    auto expr = TRY(this->parseUnaryExpression());
    if (!isAssignable(*expr)) {
      this->reportTokenFormatError(kind, token,
                                   "invalid left-hand side expression in prefix operation");
      return nullptr;
    }
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  AssignExpr{nullptr, kind, std::move(expr)});
  }
  default:
    return this->parseCallExpression();
  }
}

std::unique_ptr<Node> JSParser::parseCallExpression() {
  auto node = TRY(this->parseMemberExpression());
  while (true) {
    switch (this->curKind) {
    case JSTokenKind::DOT:
    case JSTokenKind::LB:
      node = TRY(this->parseMemberAccess(std::move(node)));
      continue;
    case JSTokenKind::LP:
      node = TRY(this->parseWithArguments(std::move(node)));
      continue;
    default:
      break;
    }
    break;
  }

  // suffix op
  switch (this->curKind) {
  case JSTokenKind::INC:
  case JSTokenKind::DEC:
    if (!this->lexer->hasPrevNewLine()) {
      Token token = this->curToken;
      JSTokenKind kind = this->scan();
      if (!isAssignable(*node)) {
        this->reportTokenFormatError(kind, token,
                                     "invalid left-hand side expression in postfix operation");
        return nullptr;
      }
      unsigned int lineNum = node->lineNum;
      node = std::make_unique<Node>(lineNum, AssignExpr{std::move(node), kind, nullptr});
    }
    break;
  default:
    break;
  }
  return node;
}

std::unique_ptr<Node> JSParser::parseMemberExpression() {
  std::unique_ptr<Node> node;
  if (this->curKind == JSTokenKind::NEW) {
    this->consume();
    auto constructor = TRY(this->parseMemberExpression());
    if (this->curKind == JSTokenKind::LP) {
      node = TRY(this->parseWithArguments(std::move(constructor), true));
    } else {
      CallExpr call;
      unsigned int lineNum = constructor->lineNum;
      call.func = std::move(constructor);
      call.newExpr = true;
      node = std::make_unique<Node>(lineNum, std::move(call));
    }
  } else {
    node = TRY(this->parsePrimary());
  }
  return this->parseMemberAccess(std::move(node));
}

std::unique_ptr<Node> JSParser::parseMemberAccess(std::unique_ptr<Node> &&node) {
  while (true) {
    switch (this->curKind) {
    case JSTokenKind::DOT: {
      this->consume();
      Token token = TRY(this->expect(JSTokenKind::IDENTIFIER));
      unsigned int lineNum = node->lineNum;
      node = std::make_unique<Node>(lineNum,
                                    AccessExpr{std::move(node), this->lexer->toTokenText(token)});
      continue;
    }
    case JSTokenKind::LB: {
      this->consume();
      unsigned int lineNum = node->lineNum;
      auto expr = TRY(this->parseExpression());
      TRY(this->expect(JSTokenKind::RB));
      node = std::make_unique<Node>(lineNum, IndexExpr{std::move(node), std::move(expr)});
      continue;
    }
    default:
      return std::move(node);
    }
  }
}

std::unique_ptr<Node> JSParser::parseWithArguments(std::unique_ptr<Node> &&node, const bool isNew) {
  TRY(this->expect(JSTokenKind::LP));
  CallExpr call;
  unsigned int lineNum = node->lineNum;
  call.func = std::move(node);
  call.newExpr = isNew;
  while (this->curKind != JSTokenKind::RP) {
    call.args.push_back(TRY(this->parseExpression()));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RP) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RP);
    }
  }
  TRY(this->expect(JSTokenKind::RP));
  return std::make_unique<Node>(lineNum, std::move(call));
}

std::unique_ptr<Node> JSParser::parsePrimary() {
  switch (this->curKind) {
  case JSTokenKind::NIL: {
    Token token = this->expect(JSTokenKind::NIL);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), NullLiteral{});
  }
  case JSTokenKind::TRUE: {
    Token token = this->expect(JSTokenKind::TRUE);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), BoolLiteral{true});
  }
  case JSTokenKind::FALSE: {
    Token token = this->expect(JSTokenKind::FALSE);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), BoolLiteral{false});
  }
  case JSTokenKind::NUMBER:
    return this->parseNumber();
  case JSTokenKind::STRING:
    return this->parseString();
  case JSTokenKind::REGEX: {
    auto token = this->expect(JSTokenKind::REGEX);
    unsigned int lineNum = this->lexer->getLineNumByPos(token.pos);
    std::string err;
    auto prototype = findProperty(this->global, lineNum, this->global->findOrUndef(builtin::REGEXP),
                                  builtin::PROTOTYPE);
    assert(prototype);
    assert(std::holds_alternative<JSObjectPtr>(prototype.value));
    if (auto ret = createJSRegexFromLiteral(std::get<JSObjectPtr>(prototype.value),
                                            this->lexer->toStrRef(token), &err)) {
      return std::make_unique<Node>(lineNum, RegexLiteral{std::move(ret)});
    }
    this->reportTokenFormatError(JSTokenKind::REGEX, token, std::move(err));
    return nullptr;
  }
  case JSTokenKind::IDENTIFIER: {
    auto token = this->expect(JSTokenKind::IDENTIFIER);
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  NameExpr{this->lexer->toTokenText(token)});
  }
  case JSTokenKind::FUNCTION:
    return this->parseFunction();
  case JSTokenKind::LB:
    return this->parseArray();
  case JSTokenKind::LBC:
    return this->parseObject();
  case JSTokenKind::LP: {
    this->consume();
    auto node = this->parseExpression();
    TRY(this->expect(JSTokenKind::RP));
    return node;
  }
  case JSTokenKind::BACKTICK:
    return this->parseTemplate();
  default:
    E_ALTER(EACH_LA_JS_PRIMARY(GEN_LA_ALTER));
  }
}

std::unique_ptr<Node> JSParser::parseNumber() {
  Token token = TRY(this->expect(JSTokenKind::NUMBER));
  std::string data;
  data.reserve(token.size);
  for (char ch : this->lexer->toStrRef(token)) {
    if (ch == '_') {
      continue;
    }
    data += ch;
  }
  if (auto ret = convertToDouble(data.c_str())) {
    return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos),
                                  NumberLiteral{ret.value});
  }
  this->reportTokenFormatError(JSTokenKind::NUMBER, token, "out of range");
  return nullptr;
}

std::unique_ptr<Node> JSParser::parseString(const bool trimQuote) {
  auto token = TRY(this->expect(JSTokenKind::STRING));
  std::string err;
  if (auto str = this->lexer->toString(token, trimQuote, &err); str.has_value()) {
    return std::make_unique<Node>(
        this->lexer->getLineNumByPos(token.pos),
        StringLiteral{std::make_shared<std::u16string>(std::move(str.value()))});
  }
  this->reportTokenFormatError(JSTokenKind::STRING, token, "out of range");
  return nullptr;
}

std::unique_ptr<Node> JSParser::parseObject() {
  Token start = TRY(this->expect(JSTokenKind::LBC));
  ObjectLiteral object;
  while (this->curKind != JSTokenKind::RBC) {
    Token token = TRY(this->expect(JSTokenKind::IDENTIFIER));
    TRY(this->expect(JSTokenKind::COLON));
    auto expr = TRY(this->parseExpression());
    object.values.emplace_back(this->lexer->toTokenText(token), std::move(expr));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RBC) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RBC);
    }
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos), std::move(object));
}

std::unique_ptr<Node> JSParser::parseArray() {
  Token token = TRY(this->expect(JSTokenKind::LB));
  ArrayLiteral array;
  while (this->curKind != JSTokenKind::RB) {
    auto node = TRY(this->parseExpression());
    array.values.push_back(std::move(node));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RB) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RB);
    }
  }
  TRY(this->expect(JSTokenKind::RB));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), std::move(array));
}

std::unique_ptr<Node> JSParser::parseFunction() {
  Token token = TRY(this->expect(JSTokenKind::FUNCTION));
  FuncLiteral func;
  if (this->curKind == JSTokenKind::IDENTIFIER && !this->lexer->hasPrevNewLine()) {
    auto nameToken = TRY(this->expectVarDeclIdentifier());
    func.name = this->lexer->toTokenText(nameToken);
  }
  func.nodes = std::make_shared<std::vector<std::unique_ptr<Node>>>();
  TRY(this->expect(JSTokenKind::LP));
  while (this->curKind != JSTokenKind::RP) {
    Token nameToken = TRY(this->expectVarDeclIdentifier());
    func.params.push_back(this->lexer->toTokenText(nameToken));
    if (this->curKind == JSTokenKind::COMMA) {
      this->consume();
    } else if (this->curKind != JSTokenKind::RP) {
      E_ALTER(JSTokenKind::COMMA, JSTokenKind::RP);
    }
  }
  TRY(this->expect(JSTokenKind::RP));
  TRY(this->expect(JSTokenKind::LBC));
  while (this->curKind != JSTokenKind::RBC) {
    func.nodes->push_back(TRY(this->parseStatement()));
  }
  TRY(this->expect(JSTokenKind::RBC));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(token.pos), std::move(func));
}

std::unique_ptr<Node> JSParser::parseTemplate() {
  Token start = TRY(this->expect(JSTokenKind::BACKTICK));
  std::vector<std::unique_ptr<Node>> nodes;
  while (this->curKind != JSTokenKind::BACKTICK) {
    switch (this->curKind) {
    case JSTokenKind::STRING: {
      auto node = TRY(this->parseString(false));
      nodes.push_back(std::move(node));
      continue;
    }
    case JSTokenKind::START_INTERP: {
      this->consume();
      auto node = TRY(this->parseExpression());
      TRY(this->expect(JSTokenKind::RBC));
      nodes.push_back(std::move(node));
      continue;
    }
    default:
      E_ALTER(JSTokenKind::STRING, JSTokenKind::START_INTERP);
    }
  }
  TRY(this->expect(JSTokenKind::BACKTICK));
  return std::make_unique<Node>(this->lexer->getLineNumByPos(start.pos),
                                TemplateExpr{std::move(nodes)});
}

#undef TRY
#define TRY(...)                                                                                   \
  ({                                                                                               \
    auto v__ = (__VA_ARGS__);                                                                      \
    if (!v__) {                                                                                    \
      return v__;                                                                                  \
    }                                                                                              \
    std::move(v__.value);                                                                          \
  })

static JSResult evaluate(const Node &node, const std::shared_ptr<JSEnv> &env);

static JSResult evalArray(const ArrayLiteral &literal, const std::shared_ptr<JSEnv> &env) {
  auto array = createJSArray(env);
  array->array.reserve(literal.values.size());
  for (auto &e : literal.values) {
    auto ret = TRY(evaluate(*e, env));
    array->array.push_back(std::move(ret));
  }
  return Ok(std::move(array));
}

static JSResult evalObject(const ObjectLiteral &literal, const std::shared_ptr<JSEnv> &env) {
  JSObjectPtr object = std::make_shared<JSObject>();
  for (auto &[k, v] : literal.values) {
    auto value = TRY(evaluate(*v, env));
    object->setProperty(k, JSPropertyAttr::DEFAULT, std::move(value));
  }
  return Ok(std::move(object));
}

static JSResult evalCallExpr(const CallExpr &callExpr, const unsigned int lineNum,
                             const std::shared_ptr<JSEnv> &env) {
  JSValue callee;
  JSValue recv;
  if (callExpr.newExpr) {
    callee = TRY(evaluate(*callExpr.func, env));
  } else if (std::holds_alternative<AccessExpr>(callExpr.func->value)) {
    auto &access = std::get<AccessExpr>(callExpr.func->value);
    recv = TRY(evaluate(*access.recv, env));
    callee = TRY(findProperty(env, lineNum, recv, access.name));
  } else {
    callee = TRY(evaluate(*callExpr.func, env));
  }
  JSFunctionPtr func;
  if (std::holds_alternative<JSFunctionPtr>(callee)) {
    func = std::get<JSFunctionPtr>(callee);
    if (callExpr.newExpr) {
      recv = newObject(func);
    }
  } else {
    return throwError(env, builtin::TYPE_ERROR, lineNum, u"not a function");
  }
  std::vector<JSValue> args;
  args.reserve(callExpr.args.size());
  for (auto &e : callExpr.args) {
    args.push_back(TRY(evaluate(*e, env)));
  }
  return callJSFunction(env, lineNum, func, std::move(recv), std::move(args));
}

static JSResult evalFunc(const FuncLiteral &literal, const std::shared_ptr<JSEnv> &env) {
  auto impl = [nodes = literal.nodes, name = literal.name](
                  const JSFunctionPtr &func, const std::shared_ptr<JSEnv> &env) -> JSResult {
    if (!name.empty()) {
      env->define(name, func);
    }
    for (auto &node : *nodes) {
      switch (auto [status, value] = evaluate(*node, env); status) {
      case JSResult::Status::OK:
      case JSResult::Status::BREAK:    // unreachable
      case JSResult::Status::CONTINUE: // unreachable
        continue;
      case JSResult::Status::ERR:
        return Err(std::move(value));
      case JSResult::Status::RETURN:
        return Ok(std::move(value));
      }
    }
    return Ok(JSValue());
  };
  return Ok(createJSFunction(env, literal.name.c_str(), std::vector(literal.params), nullptr,
                             std::move(impl)));
}

static JSResult evalUnary(const UnaryExpr &unary, const std::shared_ptr<JSEnv> &env) {
  auto value = TRY(evaluate(*unary.expr, env));
  switch (unary.op) {
  case JSTokenKind::NOT:
    return Ok(!toBool(value));
  case JSTokenKind::ADD:
    return Ok(toNumber(value));
  case JSTokenKind::SUB:
    return Ok(-toNumber(value));
  case JSTokenKind::VOID:
    return Ok(JSValue()); // always `undefined`
  case JSTokenKind::TYPEOF:
    return Ok(std::make_shared<JSString>(typeOf(value)));
  default:
    fatal("unreachable: %s\n", toString(unary.op));
  }
}

static double compare(const JSValue &left, const JSValue &right) {
  if (std::holds_alternative<JSStringPtr>(left) && std::holds_alternative<JSStringPtr>(right)) {
    auto &left0 = *std::get<JSStringPtr>(left);
    auto &right0 = *std::get<JSStringPtr>(right);
    return left0.compare(right0);
  }
  double left0 = toNumber(left);
  double right0 = toNumber(right);
  if (std::isnan(left0) || std::isnan(right0)) {
    return std::nan("");
  }
  if (left0 < right0) {
    return -1;
  }
  if (left0 > right0) {
    return 1;
  }
  return 0;
}

static double jsRemainder(const double left, const double right) {
  if (std::isnan(left) || std::isnan(right) || std::isinf(left) || right == 0.0) {
    return std::nan("");
  }
  if (std::isinf(right) || left == 0.0) {
    return left;
  }
  return std::fmod(left, right);
}

static JSResult evalBinary(const BinaryExpr &binary, const std::shared_ptr<JSEnv> &env) {
  if (binary.op == JSTokenKind::COND_AND) {
    if (auto left = TRY(evaluate(*binary.left, env)); !toBool(left)) {
      return Ok(std::move(left));
    }
    return evaluate(*binary.right, env);
  }
  if (binary.op == JSTokenKind::COND_OR) {
    if (auto left = TRY(evaluate(*binary.left, env)); toBool(left)) {
      return Ok(std::move(left));
    }
    return evaluate(*binary.right, env);
  }
  auto left = TRY(evaluate(*binary.left, env));
  auto right = TRY(evaluate(*binary.right, env));
  switch (binary.op) {
  case JSTokenKind::ADD:
    if (std::holds_alternative<JSStringPtr>(left) || std::holds_alternative<JSStringPtr>(right)) {
      JSString str;
      toString(left, str);
      toString(right, str);
      return Ok(std::make_shared<JSString>(std::move(str)));
    }
    return Ok(toNumber(left) + toNumber(right));
  case JSTokenKind::SUB:
    return Ok(toNumber(left) - toNumber(right));
  case JSTokenKind::MOD:
    return Ok(jsRemainder(toNumber(left), toNumber(right)));
  case JSTokenKind::LT:
    return Ok(compare(left, right) < 0);
  case JSTokenKind::LE:
    return Ok(compare(left, right) <= 0);
  case JSTokenKind::GT:
    return Ok(compare(left, right) > 0);
  case JSTokenKind::GE:
    return Ok(compare(left, right) >= 0);
  case JSTokenKind::INSTANCEOF:
    return isInstanceOf(env, env->callerLineNum(), left, right);
  case JSTokenKind::EQ2:
    return Ok(strictlyEquals(left, right));
  case JSTokenKind::NE2:
    return Ok(!strictlyEquals(left, right));
  default:
    fatal("unreachable: %s\n", toString(binary.op));
  }
  return Ok(JSValue());
}

static JSResult evalIndex(const IndexExpr &expr, const std::shared_ptr<JSEnv> &env) {
  auto recv = TRY(evaluate(*expr.recv, env));
  auto index = TRY(evaluate(*expr.index, env));
  return findPropertyByIndex(env, recv, index);
}

static JSResult assignImpl(const Node &left, JSValue &&right, const std::shared_ptr<JSEnv> &env) {
  if (std::holds_alternative<NameExpr>(left.value)) {
    auto &nameExpr = std::get<NameExpr>(left.value);
    if (!env->assign(nameExpr.name, right)) {
      JSString str;
      toUTF16(nameExpr.name, str);
      str += u" is not defined";
      return throwError(env, builtin::REF_ERROR, std::move(str));
    }
    return Ok(std::move(right));
  }
  if (std::holds_alternative<AccessExpr>(left.value)) {
    auto &accessExpr = std::get<AccessExpr>(left.value);
    auto recv = TRY(evaluate(*accessExpr.recv, env));
    return assignProperty(env, recv, accessExpr.name, std::move(right));
  }

  // recv[index] = right
  auto &indexExpr = std::get<IndexExpr>(left.value);
  auto recv = TRY(evaluate(*indexExpr.recv, env));
  auto index = TRY(evaluate(*indexExpr.index, env));
  return assignPropertyByIndex(env, recv, index, std::move(right));
}

static JSResult evalAssign(const AssignExpr &assign, const std::shared_ptr<JSEnv> &env) {
  switch (assign.op) {
  case JSTokenKind::ASSIGN: {
    auto right = TRY(evaluate(*assign.right, env));
    return assignImpl(*assign.left, std::move(right), env);
  }
  case JSTokenKind::INC:
  case JSTokenKind::DEC: {
    double delta = assign.op == JSTokenKind::INC ? 1 : -1;
    if (assign.left) { // left++, left--
      assert(!assign.right);
      auto left = TRY(evaluate(*assign.left, env));
      const auto oldValue = toNumber(left);
      TRY(assignImpl(*assign.left, oldValue + delta, env));
      return Ok(oldValue);
    }
    // ++right, --right
    assert(assign.right);
    auto left = TRY(evaluate(*assign.right, env));
    const auto newValue = toNumber(left) + delta;
    TRY(assignImpl(*assign.right, newValue, env));
    return Ok(newValue);
  }
  default:
    break;
  }
  auto left = TRY(evaluate(*assign.left, env));
  auto right = TRY(evaluate(*assign.right, env));
  switch (assign.op) {
  case JSTokenKind::ADD_ASSIGN:
    if (std::holds_alternative<JSStringPtr>(left) || std::holds_alternative<JSStringPtr>(right)) {
      JSString str;
      toString(left, str);
      toString(right, str);
      right = std::make_shared<JSString>(std::move(str));
    } else {
      right = toNumber(left) + toNumber(right);
    }
    break;
  case JSTokenKind::SUB_ASSIGN:
    right = toNumber(left) - toNumber(right);
    break;
  case JSTokenKind::MOD_ASSIGN:
    right = jsRemainder(toNumber(left), toNumber(right));
    break;
  default:
    fatal("unsupported assign: %s\n", toString(assign.op));
  }
  return assignImpl(*assign.left, std::move(right), env);
}

static JSResult evalTemplate(const TemplateExpr &temp, const std::shared_ptr<JSEnv> &env) {
  JSString str;
  for (auto &e : temp.nodes) {
    auto v = TRY(evaluate(*e, env));
    toString(v, str);
  }
  return Ok(std::make_shared<JSString>(std::move(str)));
}

static JSResult defineVar(VarDecl::Kind kind, const std::string &name, JSValue &&value,
                          unsigned int lineNum, const std::shared_ptr<JSEnv> &env) {
  auto targetEnv = env;
  if (kind == VarDecl::Kind::VAR) {
    targetEnv = targetEnv->findGlobalOrFuncEnv();
  }
  if (!targetEnv->define(name, value)) {
    if (kind == VarDecl::Kind::VAR) {
      targetEnv->assign(name, std::move(value));
    } else { // TODO: should be syntax error
      JSString message = u"'";
      toUTF16(name, message);
      message += u"' is already defined";
      return throwError(env, builtin::TYPE_ERROR, lineNum, std::move(message));
    }
  }
  return Ok(JSValue());
}

static JSResult evalBlockWithCurrentEnv(const BlockStmt &block, const std::shared_ptr<JSEnv> &env) {
  for (auto &node : block.nodes) {
    TRY(evaluate(*node, env));
  }
  return Ok(JSValue());
}

static JSResult evalBlock(const BlockStmt &block, const std::shared_ptr<JSEnv> &env) {
  return evalBlockWithCurrentEnv(block, env->createChild());
}

static JSResult evalIf(const IfStmt &ifStmt, const std::shared_ptr<JSEnv> &env) {
  if (auto cond = TRY(evaluate(*ifStmt.cond, env)); toBool(cond)) {
    TRY(evaluate(*ifStmt.thenStmt, env));
  } else if (ifStmt.elseStmt) {
    TRY(evaluate(*ifStmt.elseStmt, env));
  }
  return Ok(JSValue());
}

static JSResult evalFor(const ForStmt &forStmt, const std::shared_ptr<JSEnv> &env) {
  auto loopInitEnv = env->createChild();
  if (forStmt.init) {
    TRY(evaluate(*forStmt.init, loopInitEnv));
  }
  while (!forStmt.cond || toBool(TRY(evaluate(*forStmt.cond, loopInitEnv)))) {
    if (forStmt.body) {
      auto loopEnv = loopInitEnv->createChild();
      JSResult ret;
      if (auto &e = forStmt.body->value; std::holds_alternative<BlockStmt>(e)) {
        ret = evalBlockWithCurrentEnv(std::get<BlockStmt>(e), loopEnv);
      } else {
        ret = evaluate(*forStmt.body, loopEnv);
      }
      switch (ret.status) {
      case JSResult::Status::OK:
        break;
      case JSResult::Status::ERR:
      case JSResult::Status::RETURN:
        return ret;
      case JSResult::Status::BREAK:
        goto BREAK;
      case JSResult::Status::CONTINUE:
        break;
      }
    }
    if (forStmt.after) {
      TRY(evaluate(*forStmt.after, loopInitEnv));
    }
  }
BREAK:
  return Ok(JSValue());
}

static std::function<std::optional<JSValue>()> toIter(const JSValue &value) {
  if (std::holds_alternative<JSStringPtr>(value)) {
    return [index = static_cast<size_t>(0),
            str = std::get<JSStringPtr>(value)]() mutable -> std::optional<JSValue> {
      if (index < str->size()) {
        JSString sub;
        auto ch = (*str)[index++]; // NOLINT
        sub += ch;
        if (UnicodeUtil::isHighSurrogate(ch) && index < str->size()) {
          sub += (*str)[index++]; // NOLINT
        }
        return std::make_shared<JSString>(std::move(sub));
      }
      return {};
    };
  }
  return [index = static_cast<size_t>(0),
          array = std::get<JSArrayPtr>(value)]() mutable -> std::optional<JSValue> {
    if (index < array->array.size()) {
      auto v = array->array[index++];
      return v;
    }
    return {};
  };
}

static JSResult evalForOf(const ForOfStmt &forOfStmt, unsigned int lineNum,
                          const std::shared_ptr<JSEnv> &env) {
  auto loopInitEnv = env->createChild();
  auto &decl = std::get<VarDecl>(forOfStmt.iter->value);
  auto iterable = TRY(evaluate(*decl.expr, loopInitEnv));
  if (!std::holds_alternative<JSStringPtr>(iterable) &&
      !std::holds_alternative<JSArrayPtr>(iterable)) {
    JSString str;
    toPrettyString(iterable, str);
    str += u" is not iterable";
    return throwError(loopInitEnv, builtin::TYPE_ERROR, std::move(str));
  }
  for (auto iter = toIter(iterable);;) {
    auto next = iter();
    if (!next) {
      break;
    }
    auto loopEnv = loopInitEnv->createChild();
    TRY(defineVar(decl.kind, decl.name, std::move(next.value()), lineNum, loopEnv));
    if (forOfStmt.body) {
      JSResult ret;
      if (auto &e = forOfStmt.body->value; std::holds_alternative<BlockStmt>(e)) {
        ret = evalBlockWithCurrentEnv(std::get<BlockStmt>(e), loopEnv);
      } else {
        ret = evaluate(*forOfStmt.body, loopEnv);
      }
      switch (ret.status) {
      case JSResult::Status::OK:
        break;
      case JSResult::Status::ERR:
      case JSResult::Status::RETURN:
        return ret;
      case JSResult::Status::BREAK:
        goto BREAK;
      case JSResult::Status::CONTINUE:
        break;
      }
    }
  }
BREAK:
  return Ok(JSValue());
}

static JSResult evalTry(const TryStmt &tryStmt, const std::shared_ptr<JSEnv> &env) {
  auto ret = evaluate(*tryStmt.tryBlock, env);
  if (ret.status == JSResult::Status::ERR && tryStmt.catchBlock) {
    auto catchEnv = env->createChild();
    if (!tryStmt.except.empty()) {
      catchEnv->define(tryStmt.except, ret.value);
    }
    ret = evalBlockWithCurrentEnv(std::get<BlockStmt>(tryStmt.catchBlock->value), catchEnv);
  }
  if (tryStmt.finallyBlock) {
    TRY(evaluate(*tryStmt.finallyBlock, env));
  }
  return ret;
}

static JSResult evalSwitchCase(const SwitchCase &switchCase, const std::shared_ptr<JSEnv> &env) {
  auto expr = TRY(evaluate(*switchCase.expr, env));
  unsigned int clauseIndex = 0;
  // find case-clause
  for (; clauseIndex < switchCase.caseClauses.size(); clauseIndex++) {
    auto &clause = switchCase.caseClauses[clauseIndex];
    if (!clause.label) {
      continue;
    }
    auto label = TRY(evaluate(*clause.label, env));
    if (strictlyEquals(label, expr)) {
      break;
    }
  }
  if (clauseIndex == switchCase.caseClauses.size() && switchCase.hasDefault) { // not found
    clauseIndex = switchCase.defaultIndex;
  }

  // eval case body
  auto switchEnv = env->createChild();
  for (; clauseIndex < switchCase.caseClauses.size(); clauseIndex++) {
    for (auto &e : switchCase.caseClauses[clauseIndex].body) {
      switch (auto ret = evaluate(*e, switchEnv); ret.status) {
      case JSResult::Status::OK:
        continue;
      case JSResult::Status::ERR:
      case JSResult::Status::RETURN:
      case JSResult::Status::CONTINUE:
        return ret;
      case JSResult::Status::BREAK:
        goto END; // break switch-case
      }
    }
  }
END:
  return Ok(JSValue());
}

static JSResult evaluate(const Node &node, const std::shared_ptr<JSEnv> &env) {
  return std::visit(
      [env, lineNum = node.lineNum](auto &&element) -> JSResult {
        using T = std::decay_t<decltype(element)>;
        if constexpr (std::is_same_v<T, NullLiteral>) {
          return Ok(nullptr);
        } else if constexpr (std::is_same_v<T, BoolLiteral> || std::is_same_v<T, NumberLiteral> ||
                             std::is_same_v<T, StringLiteral> || std::is_same_v<T, RegexLiteral>) {
          return Ok(element.value);
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
          return evalArray(element, env);
        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
          return evalObject(element, env);
        } else if constexpr (std::is_same_v<T, FuncLiteral>) {
          return evalFunc(element, env);
        } else if constexpr (std::is_same_v<T, NameExpr>) {
          if (auto *v = env->find(element.name)) {
            return Ok(JSValue(*v));
          }
          JSString message;
          toUTF16(element.name, message);
          message += u" is not defined";
          return throwError(env, builtin::REF_ERROR, lineNum, std::move(message));
        } else if constexpr (std::is_same_v<T, AccessExpr>) {
          auto recv = TRY(evaluate(*element.recv, env));
          return findProperty(env, lineNum, recv, element.name);
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
          return evalIndex(element, env);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          return evalCallExpr(element, lineNum, env);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          return evalUnary(element, env);
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
          return evalBinary(element, env);
        } else if constexpr (std::is_same_v<T, AssignExpr>) {
          return evalAssign(element, env);
        } else if constexpr (std::is_same_v<T, TemplateExpr>) {
          return evalTemplate(element, env);
        } else if constexpr (std::is_same_v<T, VarDecl>) {
          JSValue value;
          if (element.expr) {
            value = TRY(evaluate(*element.expr, env));
          }
          return defineVar(element.kind, element.name, std::move(value), lineNum, env);
        } else if constexpr (std::is_same_v<T, JumpStmt>) {
          JSValue ret;
          if (auto &n = element.expr) {
            ret = TRY(evaluate(*n, env));
          }
          return JSResult{element.status, std::move(ret)};
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          return evalBlock(element, env);
        } else if constexpr (std::is_same_v<T, TryStmt>) {
          return evalTry(element, env);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
          return evalIf(element, env);
        } else if constexpr (std::is_same_v<T, ForStmt>) {
          return evalFor(element, env);
        } else if constexpr (std::is_same_v<T, ForOfStmt>) {
          return evalForOf(element, lineNum, env);
        } else if constexpr (std::is_same_v<T, SwitchCase>) {
          return evalSwitchCase(element, env);
        } else {
          fatal("unreachable");
        }
      },
      node.value);
}

JSResult jsEval(const char *sourceName, StringRef source, std::shared_ptr<JSEnv> global,
                const bool debug, std::string *syntaxErr) {
  if (!global) {
    global = initJSEnv();
  }
  std::vector<std::unique_ptr<Node>> nodes;
  {
    auto fileName = newJSStringPtr(sourceName);
    if (!global->define(JSEnv::DEFINED_FILENAME, fileName)) {
      global->assign(JSEnv::DEFINED_FILENAME, fileName);
    }
    JSLexer lexer(sourceName, source);
    lexer.setVerbose(debug);
    JSParser parser(global, lexer);
    while (parser) {
      if (auto node = parser()) {
        nodes.push_back(std::move(node));
      } else if (auto error = parser.formatError(); error.has_value()) {
        if (syntaxErr) {
          *syntaxErr = std::move(error.value().detail);
        }
        JSString message;
        toUTF16(error.value().message, message);
        return throwError(global, builtin::SYNTAX_ERROR, error.value().lineNum, std::move(message));
      }
    }
  }
  JSValue last;
  for (auto &node : nodes) {
    last = TRY(evaluate(*node, global));
  }
  return Ok(std::move(last));
}

std::string formatEvalResult(const std::shared_ptr<JSEnv> &env, const JSResult &result) {
  JSString out;
  auto &v = result.value;
  if (!result) {
    out += u"[uncaught]\n";
  }
  if (auto ret = isInstanceOf(env, 0, v, env->findGlobalEnv()->findOrUndef(builtin::ERROR));
      ret && std::get<bool>(ret.value)) {
    if (auto r = findProperty(env, 1, v, "name")) {
      toPrettyString(r.value, out);
    }
    if (auto r = findProperty(env, 1, v, "message")) {
      out += u": ";
      toPrettyString(r.value, out);
    }
    if (auto r = findProperty(env, 1, v, "fileName")) {
      out += u"\n    at ";
      toPrettyString(r.value, out);
      out += u':';
      r = findProperty(env, 1, v, "lineNumber");
      if (r) {
        toPrettyString(r.value, out);
      }
    }
  } else {
    toPrettyString(v, out);
  }
  return toWTF8(out);
}

} // namespace arsh::re262