/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#include "parser.h"
#include "brace.h"
#include "complete.h"
#include "signals.h"

// helper macro
#define CUR_KIND() (this->curKind)

#define START_POS() (this->curToken.pos)

#define GEN_LA_CASE(CASE) case TokenKind::CASE:
#define GEN_LA_ALTER(CASE) TokenKind::CASE,

#define E_ALTER(...)                                                                               \
  do {                                                                                             \
    this->reportNoViableAlterError((TokenKind[]){__VA_ARGS__}, false);                             \
    return nullptr;                                                                                \
  } while (false)

#define E_ALTER_OR_COMP(...)                                                                       \
  do {                                                                                             \
    this->reportNoViableAlterError((TokenKind[]){__VA_ARGS__}, true);                              \
    return nullptr;                                                                                \
  } while (false)

#define E_DETAILED(k, ...)                                                                         \
  do {                                                                                             \
    this->reportDetailedError(k, (TokenKind[]){__VA_ARGS__});                                      \
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

namespace ydsh {

#define GUARD_DEEP_NESTING(name)                                                                   \
  CallCounter name(this->callCount);                                                               \
  if (unlikely(this->callCount == MAX_NESTING_DEPTH)) {                                            \
    this->reportDeepNestingError();                                                                \
    return nullptr;                                                                                \
  }                                                                                                \
  (void)name

// ####################
// ##     Parser     ##
// ####################

Parser::Parser(Lexer &lexer, ParserOption option, ObserverPtr<CodeCompletionHandler> handler)
    : ccHandler(handler), option(option) {
  this->consumedKind = TokenKind::EOS;
  this->lexer = &lexer;
  if (this->ccHandler) {
    this->lexer->setComplete(true);
  }
  this->fetchNext();
}

static bool isNamedFuncOrUdc(const std::unique_ptr<Node> &node) {
  if (!node) {
    return false;
  }

  if (isa<UserDefinedCmdNode>(*node)) {
    return true;
  }
  if (isa<FunctionNode>(*node)) {
    auto &funcNode = cast<FunctionNode>(*node);
    return funcNode.isNamedFunc() || funcNode.isMethod();
  }
  return false;
}

std::vector<std::unique_ptr<Node>> Parser::operator()() {
  this->skippableNewlines.clear();
  this->skippableNewlines.push_back(false);

  std::vector<std::unique_ptr<Node>> nodes;
  if (hasFlag(this->option, ParserOption::SINGLE_EXPR)) {
    if (this->curKind != TokenKind::EOS) {
      auto exprNode = this->parse_expression();
      if (!this->hasError()) {
        this->parse_statementEnd();
        if (!this->hasError()) {
          this->expect(TokenKind::EOS);
        }
      }
      if (this->hasError()) {
        nodes.push_back(std::make_unique<ErrorNode>(this->getError().getErrorToken()));
      } else {
        NameInfo nameInfo({exprNode->getPos(), 0}, "");
        auto funcNode = std::make_unique<FunctionNode>(exprNode->getPos(), std::move(nameInfo),
                                                       FunctionNode::SINGLE_EXPR);
        funcNode->setFuncBody(std::move(exprNode));
        nodes.push_back(std::move(funcNode));
      }
    }
  } else {
    while (this->curKind != TokenKind::EOS) {
      auto node = this->parse_statement();
      bool stop = false;
      if (this->incompleteNode) {
        this->clear(); // force ignore parse error
        this->lexer->setComplete(false);
        node = std::move(this->incompleteNode);
        stop = true;
      } else if (this->hasError()) {
        node = std::make_unique<ErrorNode>(this->getError().getErrorToken());
        stop = true;
      }

      if (nodes.empty()) {
        nodes.push_back(std::move(node));
      } else if (isa<FuncListNode>(*nodes.back()) && isNamedFuncOrUdc(node)) {
        cast<FuncListNode>(*nodes.back()).addNode(std::move(node));
      } else if (isNamedFuncOrUdc(nodes.back()) && isNamedFuncOrUdc(node)) {
        auto last = std::move(nodes.back());
        nodes.pop_back();
        auto funcList = std::make_unique<FuncListNode>(std::move(last), std::move(node));
        nodes.push_back(std::move(funcList));
      } else {
        nodes.push_back(std::move(node));
      }

      if (stop) {
        break;
      }
    }
  }

  if (nodes.empty()) {
    nodes.push_back(std::make_unique<EmptyNode>()); // dummy
  }
  return nodes;
}

void Parser::refetch(LexerCond cond) {
  this->lexer->setPos(START_POS());
  this->lexer->setLexerCond(cond);
  this->fetchNext();
}

void Parser::popLexerMode() {
  bool prevNewline = this->lexer->isPrevNewLine();
  bool prevSpace = this->lexer->isPrevSpace();
  this->lexer->popLexerMode();
  this->lexer->setPos(START_POS());
  this->fetchNext();
  this->lexer->setPrevNewline(prevNewline);
  this->lexer->setPrevSpace(prevSpace);
}

void Parser::changeLexerModeToSTMT() {
  const auto prev = this->lexer->getPrevMode().cond();
  if (prev == yycSTMT) { // already statement mode
    return;
  }
  switch (CUR_KIND()) {
  case TokenKind::LP:
  case TokenKind::LB:
  case TokenKind::LBC:
    return;
  case TokenKind::START_INTERP:
  case TokenKind::START_SUB_CMD:
  case TokenKind::OPEN_DQUOTE:
  case TokenKind::APPLIED_NAME_WITH_BRACKET:
  case TokenKind::SPECIAL_NAME_WITH_BRACKET:
  case TokenKind::APPLIED_NAME_WITH_PAREN:
  case TokenKind::START_IN_SUB:
  case TokenKind::START_OUT_SUB:
    this->lexer->popLexerMode();
    break;
  default:
    break;
  }
  this->refetch(yycSTMT);
}

Token Parser::expect(TokenKind kind, bool fetchNext) {
  if (this->inCompletionPoint() && !this->ccHandler->hasCompRequest()) {
    this->ccHandler->addExpectedTokenRequest(this->lexer->toTokenText(this->curToken), kind);
  }
  if (isUnclosedToken(this->curKind)) {
    this->createError(this->curKind, this->curToken, INVALID_TOKEN, toString(this->curKind));
    return this->curToken;
  }
  return parse_base_type::expect(kind, fetchNext);
}

Token Parser::expectAndChangeMode(TokenKind kind, LexerCond cond, bool fetchNext) {
  Token token = this->expect(kind, false);
  if (!this->hasError()) {
    this->lexer->setLexerCond(cond);
    if (fetchNext) {
      this->fetchNext();
    }
  }
  return token;
}

bool Parser::tryCompleteInfixKeywords(unsigned int size, const TokenKind *kinds) {
  if (this->inCompletionPoint() && this->lexer->getCompTokenKind() == TokenKind::INVALID) {
    auto tokenRef = this->lexer->toStrRef(this->curToken);
    if (!isKeyword(tokenRef)) {
      return false;
    }
    TokenKind alters[32];
    assert(size < std::size(alters));
    unsigned int count = 0;
    for (unsigned int i = 0; i < size; i++) {
      TokenKind k = kinds[i];
      StringRef kindStr = toString(k);
      if (kindStr.startsWith(tokenRef)) {
        alters[count++] = k;
      }
    }
    if (count > 0) {
      this->reportNoViableAlterError(count, alters, true);
      return true;
    }
  }
  return false;
}

bool Parser::inVarNameCompletionPoint() const {
  if (this->inCompletionPoint()) {
    auto compTokenKind = this->lexer->getCompTokenKind();
    if (compTokenKind == TokenKind::APPLIED_NAME) {
      auto ref = this->lexer->toStrRef(this->curToken);
      return !ref.contains('{');
    }
  }
  return false;
}

bool Parser::inTypeNameCompletionPoint() const {
  if (!this->inCompletionPoint()) {
    return false;
  }
  switch (this->lexer->getCompTokenKind()) {
  case TokenKind::TYPE_NAME:
  case TokenKind::FUNC:
  case TokenKind::TYPEOF:
  case TokenKind::EOS:
    if (this->consumedKind == TokenKind::IS || this->consumedKind == TokenKind::AS) {
      if (this->hasSpace()) {
        return true;
      }
    } else {
      return true;
    }
    break;
  default:
    break;
  }
  return false;
}

template <typename Iterate>
static constexpr bool iterate_requirement_v =
    std::is_same_v<bool, std::invoke_result_t<Iterate, Token, bool>>;

template <typename Func, enable_when<iterate_requirement_v<Func>> = nullptr>
static void iteratePathList(const Lexer &lex, const Token token, const char delim, Func func) {
  StringRef ref = lex.toStrRef(token);
  const unsigned int size = ref.size();
  unsigned int startPos = 0;
  unsigned int pos = 0;
  while (pos < size) {
    char ch = ref[pos++];
    if (ch == '\\' && pos < size) {
      pos++;
    }
    if (ch == delim || pos == size) {
      Token sub = token.slice(startPos, pos);
      if (!func(sub, ch == delim)) {
        return;
      }
      startPos = pos;
    }
  }
}

void Parser::tryCompleteFileNames(CmdArgParseOpt opt) {
  Token token = this->curToken;
  if (hasFlag(opt, CmdArgParseOpt::ASSIGN)) {
    bool lastSplit = false;
    iteratePathList(*this->lexer, token, ':', [&token, &lastSplit](Token subToken, bool s) {
      token = subToken;
      lastSplit = s;
      return true;
    });
    if (lastSplit) { // for `AAA=BBB:'
      token = Token{token.endPos(), 0};
    }
    if (token.pos != this->curToken.pos || hasFlag(opt, CmdArgParseOpt::FIRST)) {
      auto op = CodeCompOp::FILE;
      if (this->lexer->startsWith(token, '~')) {
        setFlag(op, CodeCompOp::TILDE);
      }
      this->ccHandler->addCompRequest(op, this->lexer->toCmdArg(token));
    }
  } else if (hasFlag(opt, CmdArgParseOpt::FIRST)) {
    auto op = hasFlag(opt, CmdArgParseOpt::MODULE) ? CodeCompOp::MODULE : CodeCompOp::FILE;
    if (this->lexer->startsWith(token, '~')) {
      setFlag(op, CodeCompOp::TILDE);
    }

    if (hasFlag(opt, CmdArgParseOpt::REDIR) || hasFlag(opt, CmdArgParseOpt::MODULE) ||
        hasFlag(op, CodeCompOp::TILDE)) {
      this->ccHandler->addCompRequest(op, this->lexer->toCmdArg(token));
    } else {
      assert(op == CodeCompOp::FILE);
      Token prefixToken = token;
      bool lastSplit = false;
      iteratePathList(*this->lexer, token, '=', [&prefixToken, &lastSplit](Token subToken, bool s) {
        prefixToken = subToken;
        lastSplit = s;
        return false;
      });
      auto compWord = this->lexer->toCmdArg(prefixToken);
      if (token != prefixToken) { // for `echo if=./' or `echo if=~'
        auto remainToken = token.sliceFrom(prefixToken.size);
        if (prefixToken.size > 1) { // ignore `echo =./', `echo =~'
          if (this->lexer->startsWith(remainToken, '~')) {
            setFlag(op, CodeCompOp::TILDE);
          }
          this->ccHandler->setCompWordOffset(compWord.size());
        }
        compWord += this->lexer->toCmdArg(remainToken);
      } else if (lastSplit && prefixToken.size > 1) { // for `echo AAA=' (except for `echo =')
        this->ccHandler->setCompWordOffset(compWord.size());
      }
      this->ccHandler->addCompRequest(op, std::move(compWord));
    }
  }
}

void Parser::reportNoViableAlterError(unsigned int size, const TokenKind *alters, bool allowComp) {
  if (allowComp && this->inCompletionPoint()) {
    this->ccHandler->addExpectedTokenRequests(this->lexer->toTokenText(this->curToken), size,
                                              alters);
  }
  if (isUnclosedToken(this->curKind)) {
    this->createError(this->curKind, this->curToken, INVALID_TOKEN, toString(this->curKind));
    return;
  }
  parse_base_type::reportNoViableAlterError(size, alters);
}

void Parser::reportDetailedError(ParseErrorKind kind, unsigned int size, const TokenKind *alters,
                                 const char *messageSuffix) {
  struct ERROR {
    const char *kind;
    const char *message;
  } table[] = {
#define GEN_TABLE(E, S) {#E, S},
      EACH_PARSE_ERROR_KIND(GEN_TABLE)
#undef GEN_TABLE
  };
  auto &e = table[static_cast<unsigned int>(kind)];
  std::string message;
  if (isInvalidToken(this->curKind)) {
    message += "invalid token, ";
  } else if (isUnclosedToken(this->curKind)) {
    this->createError(this->curKind, this->curToken, INVALID_TOKEN, toString(this->curKind));
    return;
  } else if (!isEOSToken(this->curKind)) {
    message += "mismatched token `";
    message += toString(this->curKind);
    message += "', ";
  }
  message += "expected ";
  message += e.message;
  if (messageSuffix) {
    message += messageSuffix;
  }

  std::vector<TokenKind> expectedTokens(alters, alters + size);
  this->createError(this->curKind, this->curToken, e.kind, std::move(expectedTokens),
                    std::move(message));
}

// parse rule definition
std::unique_ptr<FunctionNode> Parser::parse_function(bool needBody) {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::FUNCTION);
  unsigned int startPos = START_POS();
  this->consume(); // FUNCTION

  NameInfo nameInfo({startPos, 0}, "");
  if (CUR_KIND() == TokenKind::IDENTIFIER) { // named function
    nameInfo = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toName));
  } else { // anonymous function
    this->refetch(yycEXPR);
  }
  auto node = std::make_unique<FunctionNode>(startPos, std::move(nameInfo));
  TRY(this->expectAndChangeMode(TokenKind::LP, yycPARAM));

  for (unsigned int count = 0; CUR_KIND() != TokenKind::RP; count++) {
    auto ctx = this->inSkippableNLCtx();
    if (count > 0) {
      if (CUR_KIND() != TokenKind::COMMA) {
        E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RP);
      }
      TRY(this->expectAndChangeMode(TokenKind::COMMA, yycPARAM));
    }

    if (CUR_KIND() == TokenKind::PARAM_NAME) {
      auto param = this->expectName(TokenKind::PARAM_NAME, &Lexer::toName); // always success
      std::unique_ptr<TypeNode> typeNode;
      if (!node->isAnonymousFunc() || CUR_KIND() == TokenKind::COLON) {
        TRY(this->expect(TokenKind::COLON, false));
        typeNode = TRY(this->parse_typeName());
      }
      node->addParamNode(std::move(param), std::move(typeNode));
    } else {
      E_ALTER(TokenKind::PARAM_NAME, TokenKind::RP);
    }
  }
  this->expect(TokenKind::RP); // always success
  node->updateToken(this->curToken);

  std::unique_ptr<TypeNode> retTypeNode;
  if (CUR_KIND() == TokenKind::COLON) {
    this->expect(TokenKind::COLON, false); // always success
    retTypeNode = TRY(this->parse_typeName());
  } else if (!node->isAnonymousFunc()) {
    retTypeNode = newVoidTypeNode();
  }
  node->setReturnTypeNode(std::move(retTypeNode));

  if (!needBody) { // for function declaration
    return node;
  }

  std::unique_ptr<Node> exprNode;
  if (node->isAnonymousFunc()) {
    TRY(this->expect(TokenKind::CASE_ARM));
    exprNode = this->parse_expression();
  } else {
    switch (CUR_KIND()) {
    case TokenKind::FOR: {
      this->expect(TokenKind::FOR, false); // always success
      auto type = TRY(this->parse_typeName());
      node->setRecvTypeNode(std::move(type));
      break;
    }
    case TokenKind::LBC:
      break;
    default:
      E_ALTER_OR_COMP(TokenKind::FOR, TokenKind::LBC);
    }
    exprNode = this->parse_block();
  }
  if (this->incompleteNode) {
    node->setFuncBody(std::move(this->incompleteNode));
    this->incompleteNode = std::move(node);
    return nullptr;
  } else if (this->hasError()) {
    return nullptr;
  } else {
    node->setFuncBody(std::move(exprNode));
    return node;
  }
}

std::unique_ptr<TypeNode> Parser::parse_basicOrReifiedType(Token token) {
  GUARD_DEEP_NESTING(guard);

  auto typeToken = std::make_unique<BaseTypeNode>(token, this->lexer->toName(token));
  if (!this->hasLineTerminator()) {
    if (CUR_KIND() == TokenKind::TYPE_OPEN) {
      this->consume();
      std::vector<std::unique_ptr<TypeNode>> types;
      types.push_back(TRY(this->parse_typeName(false)));

      while (CUR_KIND() == TokenKind::TYPE_SEP) {
        this->consume();
        types.push_back(TRY(this->parse_typeName(false)));
      }
      token = TRY(this->expect(TokenKind::TYPE_CLOSE));
      return std::make_unique<ReifiedTypeNode>(std::move(typeToken), std::move(types), token);
    } else if (CUR_KIND() == TokenKind::TYPE_DOT) {
      std::unique_ptr<TypeNode> typeNode = std::move(typeToken);
      while (!this->hasLineTerminator() && CUR_KIND() == TokenKind::TYPE_DOT) {
        this->consume(); // TYPE_DOT
        if (this->inTypeNameCompletionPoint()) {
          this->makeCodeComp(CodeCompNode::TYPE, std::move(typeNode), this->curToken);
        }
        Token nameToken = TRY(this->expect(TokenKind::TYPE_NAME));
        typeNode = std::make_unique<QualifiedTypeNode>(
            std::move(typeNode),
            std::make_unique<BaseTypeNode>(nameToken, this->lexer->toName(nameToken)));
      }
      return typeNode;
    }
  }
  return typeToken;
}

static std::unique_ptr<TypeNode>
createTupleOrBasicType(Token open, std::vector<std::unique_ptr<TypeNode>> &&types, Token close,
                       unsigned int commaCount) {
  if (commaCount == 0) {
    auto type = std::move(types[0]);
    type->setParenPos(open.pos, close);
    return type;
  }
  return std::make_unique<ReifiedTypeNode>(std::make_unique<BaseTypeNode>(open, TYPE_TUPLE),
                                           std::move(types), close);
}

std::unique_ptr<TypeNode> Parser::parse_typeNameImpl() {
  switch (CUR_KIND()) {
  case TokenKind::TYPE_NAME: {
    Token token = this->expect(TokenKind::TYPE_NAME); // always success
    return this->parse_basicOrReifiedType(token);
  }
  case TokenKind::PTYPE_OPEN: {
    Token openToken = this->expect(TokenKind::PTYPE_OPEN); // always success
    unsigned int count = 0;
    std::vector<std::unique_ptr<TypeNode>> types;
    while (CUR_KIND() != TokenKind::PTYPE_CLOSE) {
      types.push_back(TRY(this->parse_typeName(false)));
      if (CUR_KIND() == TokenKind::TYPE_SEP) {
        this->consume(); // COMMA
        count++;
      } else if (CUR_KIND() != TokenKind::PTYPE_CLOSE) {
        E_ALTER(TokenKind::TYPE_SEP, TokenKind::PTYPE_CLOSE);
      }
    }
    Token closeToken = TRY(this->expect(TokenKind::PTYPE_CLOSE));

    if (types.empty() || CUR_KIND() == TokenKind::TYPE_ARROW) {
      TRY(this->expect(TokenKind::TYPE_ARROW));
      return std::make_unique<FuncTypeNode>(openToken.pos, std::move(types),
                                            TRY(this->parse_typeName(false)));
    } else {
      return createTupleOrBasicType(openToken, std::move(types), closeToken, count);
    }
  }
  case TokenKind::ATYPE_OPEN: {
    Token token = this->expect(TokenKind::ATYPE_OPEN); // always success
    std::vector<std::unique_ptr<TypeNode>> types;
    types.push_back(TRY(this->parse_typeName(false)));
    bool isMap = CUR_KIND() == TokenKind::TYPE_MSEP;
    auto tempNode = std::make_unique<BaseTypeNode>(token, isMap ? TYPE_MAP : TYPE_ARRAY);
    if (isMap) {
      this->consume();
      types.push_back(TRY(this->parse_typeName(false)));
    }
    token = TRY(this->expect(TokenKind::ATYPE_CLOSE));
    return std::make_unique<ReifiedTypeNode>(std::move(tempNode), std::move(types), token);
  }
  case TokenKind::TYPEOF: {
    Token token = this->expect(TokenKind::TYPEOF); // always success
    if (CUR_KIND() == TokenKind::PTYPE_OPEN) {
      auto ctx = this->inSkippableNLCtx();

      this->expect(TokenKind::PTYPE_OPEN, false); // always success
      this->pushLexerMode(LexerMode(yycSTMT, true));

      unsigned int startPos = token.pos;
      auto exprNode(TRY(this->parse_expression()));

      token = TRY(this->expect(TokenKind::RP));

      return std::make_unique<TypeOfNode>(startPos, std::move(exprNode), token);
    }
    return this->parse_basicOrReifiedType(token);
  }
  case TokenKind::FUNC: {
    Token token = this->expect(TokenKind::FUNC); // always success
    if (!this->hasLineTerminator() && CUR_KIND() == TokenKind::TYPE_OPEN) {
      this->expect(TokenKind::TYPE_OPEN); // always success

      // parse return type
      unsigned int pos = token.pos;
      auto retNode = TRY(this->parse_typeName(false));
      std::vector<std::unique_ptr<TypeNode>> types;

      if (CUR_KIND() == TokenKind::TYPE_SEP) { // ,[
        this->consume();                       // TYPE_SEP
        TRY(this->expect(TokenKind::ATYPE_OPEN));

        // parse first arg type
        types.push_back(TRY(this->parse_typeName(false)));

        // rest arg type
        while (CUR_KIND() == TokenKind::TYPE_SEP) {
          this->consume();
          types.push_back(TRY(this->parse_typeName(false)));
        }
        TRY(this->expect(TokenKind::ATYPE_CLOSE));
      }

      token = TRY(this->expect(TokenKind::TYPE_CLOSE));
      return std::make_unique<FuncTypeNode>(pos, std::move(retNode), std::move(types), token);
    }
    return std::make_unique<BaseTypeNode>(token, this->lexer->toName(token));
  }
  default:
    if (this->inTypeNameCompletionPoint()) {
      this->makeCodeComp(CodeCompNode::TYPE, nullptr, this->curToken);
    }
    E_DETAILED(ParseErrorKind::TYPE, EACH_LA_typeName(GEN_LA_ALTER));
  }
}

std::unique_ptr<TypeNode> Parser::parse_typeName(bool enterTYPEMode) {
  GUARD_DEEP_NESTING(guard);

  if (enterTYPEMode) { // change lexer state to TYPE
    this->pushLexerMode(yycTYPE);
  }

  auto typeNode = TRY(this->parse_typeNameImpl());
  while (!this->hasLineTerminator() && CUR_KIND() == TokenKind::TYPE_OPT) {
    Token token = this->expect(TokenKind::TYPE_OPT); // always success
    typeNode = std::make_unique<ReifiedTypeNode>(
        std::move(typeNode), std::make_unique<BaseTypeNode>(token, TYPE_OPTION));
  }

  if (enterTYPEMode) {
    if (this->hasNewline()) {
      TRY(this->parse_hereDocBody());
    }
    this->popLexerMode();
  }
  return typeNode;
}

std::unique_ptr<Node> Parser::parse_statementImpl() {
  GUARD_DEEP_NESTING(guard);

  this->changeLexerModeToSTMT();

  if (this->inCompletionPoint()) {
    this->inStmtCompCtx = true;
  }
  auto cleanup = finally([&] { this->inStmtCompCtx = false; });

  switch (CUR_KIND()) {
  case TokenKind::LINE_END: {
    Token token = this->curToken; // not consume LINE_END token
    return std::make_unique<EmptyNode>(token);
  }
  case TokenKind::ASSERT: {
    unsigned int pos = START_POS();
    this->consume(); // ASSERT
    auto condNode = TRY(this->parse_expression());
    std::unique_ptr<Node> messageNode;
    if (!this->hasLineTerminator() && CUR_KIND() == TokenKind::COLON) {
      TRY(this->expectAndChangeMode(TokenKind::COLON, yycSTMT));
      messageNode = TRY(this->parse_expression());
    } else {
      std::string msg = "`";
      msg += this->lexer->toTokenText(condNode->getToken());
      msg += "'";
      messageNode = std::make_unique<StringNode>(condNode->getToken(), std::move(msg));
    }
    return std::make_unique<AssertNode>(pos, std::move(condNode), std::move(messageNode));
  }
  case TokenKind::DEFER: {
    unsigned int pos = START_POS();
    this->consume(); // DEFER
    auto blockNode = TRY(this->parse_block());
    return std::make_unique<DeferNode>(pos, std::move(blockNode));
  }
  case TokenKind::EXPORT_ENV: {
    unsigned int startPos = START_POS();
    this->consume(); // EXPORT_ENV
    auto nameInfo = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toName));
    TRY(this->expect(TokenKind::ASSIGN));
    return std::make_unique<VarDeclNode>(startPos, std::move(nameInfo),
                                         TRY(this->parse_expression()), VarDeclNode::EXPORT_ENV);
  }
  case TokenKind::IMPORT_ENV: {
    unsigned int startPos = START_POS();
    this->consume();                                        // IMPORT_ENV
    if (this->inCompletionPointAt(TokenKind::IDENTIFIER)) { // complete env name
      this->ccHandler->addCompRequest(CodeCompOp::ENV, this->lexer->toTokenText(this->curToken));
    }
    auto nameInfo = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toName));
    Token token = nameInfo.getToken();
    std::unique_ptr<Node> exprNode;
    if (!this->hasLineTerminator() && CUR_KIND() == TokenKind::COLON) {
      TRY(this->expectAndChangeMode(TokenKind::COLON, yycSTMT));
      exprNode = TRY(this->parse_expression());
    }

    auto node = std::make_unique<VarDeclNode>(startPos, std::move(nameInfo), std::move(exprNode),
                                              VarDeclNode::IMPORT_ENV);
    node->updateToken(token);
    return node;
  }
  case TokenKind::SOURCE:
  case TokenKind::SOURCE_OPT: {
    bool optional = CUR_KIND() == TokenKind::SOURCE_OPT;
    unsigned int startPos = START_POS();
    this->consume(); // always success
    auto pathNode = TRY(this->parse_cmdArg(CmdArgParseOpt::MODULE));
    auto node = std::make_unique<SourceListNode>(startPos, std::move(pathNode), optional);
    if (!optional && CUR_KIND() == TokenKind::CMD_ARG_PART &&
        this->lexer->toStrRef(this->curToken) == "as") {
      this->curKind = TokenKind::AS;                     // force change token kind for highlight
      this->expectAndChangeMode(TokenKind::AS, yycNAME); // always success
      Token token = TRY(this->expectAndChangeMode(TokenKind::IDENTIFIER, yycSTMT));
      node->setName(token, this->lexer->toName(token));
    } else if (CUR_KIND() == TokenKind::CMD_ARG_PART &&
               this->lexer->toStrRef(this->curToken) == "inlined") {
      this->curKind = TokenKind::INLINED;             // force change token kind for highlight
      Token token = this->expect(TokenKind::INLINED); // always success
      node->setInlined(true);
      node->updateToken(token);
    } else if (this->inCompletionPointAt(TokenKind::CMD_ARG_PART)) {
      auto ref = this->lexer->toStrRef(this->curToken);
      if (!optional && StringRef("as").startsWith(ref) && !ref.empty()) {
        TRY(this->expect(TokenKind::AS)); // FIXME:
      } else if (StringRef("inlined").startsWith(ref) && !ref.empty()) {
        TRY(this->expect(TokenKind::INLINED)); // FIXME:
      }
    }
    return node;
  }
  case TokenKind::TYPEDEF:
    return this->parse_typedef();
    // clang-format off
  EACH_LA_varDecl(GEN_LA_CASE) return this->parse_variableDeclaration();
  EACH_LA_expression(GEN_LA_CASE) return this->parse_expression();
    // clang-format on
  default:
    E_DETAILED(ParseErrorKind::STMT, EACH_LA_statement(GEN_LA_ALTER));
  }
}

std::unique_ptr<Node> Parser::parse_statement() {
  auto node = TRY(this->parse_statementImpl());
  TRY(this->parse_statementEnd());
  return node;
}

std::unique_ptr<Node> Parser::parse_statementEnd(bool onlyLineEnd) {
  bool checkHere = false;
  if (onlyLineEnd) {
    TRY(this->expect(TokenKind::LINE_END));
    checkHere = true;
  } else {
    switch (CUR_KIND()) {
    case TokenKind::EOS:
    case TokenKind::RBC:
      break;
    case TokenKind::LINE_END:
    case TokenKind::NEW_LINE:
      this->consume();
      checkHere = true;
      break;
    default:
      if (this->consumedKind == TokenKind::BACKGROUND ||
          this->consumedKind == TokenKind::DISOWN_BG) {
        break;
      }
      if (this->hasLineTerminator()) {
        if (this->hasNewline()) {
          checkHere = true;
        }
      } else {
        TRY(this->expect(TokenKind::NEW_LINE));
      }
      break;
    }
  }
  if (checkHere) {
    TRY(this->parse_hereDocBody());
  }
  return nullptr;
}

std::unique_ptr<Node> Parser::parse_typedef() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::TYPEDEF);
  const unsigned int startPos = START_POS();
  this->consume(); // TYPEDEF
  auto nameInfo = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toTokenText));
  switch (CUR_KIND()) {
  case TokenKind::ASSIGN: {
    TRY(this->expect(TokenKind::ASSIGN, false));
    auto typeToken = TRY(this->parse_typeName());
    return TypeDefNode::alias(startPos, std::move(nameInfo), std::move(typeToken));
  }
  case TokenKind::COLON: {
    TRY(this->expect(TokenKind::COLON, false));
    auto typeToken = TRY(this->parse_typeName());
    return TypeDefNode::errorDef(startPos, std::move(nameInfo), std::move(typeToken));
  }
  case TokenKind::LP: { // explicit constructor
    auto node = std::make_unique<FunctionNode>(startPos, std::move(nameInfo),
                                               FunctionNode::EXPLICIT_CONSTRUCTOR);
    TRY(this->expectAndChangeMode(TokenKind::LP, yycPARAM));
    for (unsigned int count = 0; CUR_KIND() != TokenKind::RP; count++) {
      auto ctx = this->inSkippableNLCtx();

      if (count > 0) {
        if (CUR_KIND() != TokenKind::COMMA) {
          E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RP);
        }
        TRY(this->expectAndChangeMode(TokenKind::COMMA, yycPARAM));
      }

      if (CUR_KIND() == TokenKind::PARAM_NAME) {
        auto param = this->expectName(TokenKind::PARAM_NAME, &Lexer::toName); // always success
        TRY(this->expect(TokenKind::COLON, false));
        auto type = this->parse_typeName();
        if (this->incompleteNode) {
          Token token = this->incompleteNode->getToken();
          auto typeofNode =
              std::make_unique<TypeOfNode>(token.pos, std::move(this->incompleteNode), token);
          node->addParamNode(std::move(param), std::move(typeofNode));
          node->setFuncBody(std::make_unique<EmptyNode>(token));
          this->incompleteNode = std::move(node);
          return nullptr;
        } else if (this->hasError()) {
          return nullptr;
        }
        node->addParamNode(std::move(param), std::move(type));
      } else {
        E_ALTER(TokenKind::PARAM_NAME, TokenKind::RP);
      }
    }
    this->expect(TokenKind::RP); // always success
    auto blockNode = this->parse_block();
    if (this->incompleteNode) {
      node->setFuncBody(std::move(this->incompleteNode));
      this->incompleteNode = std::move(node);
      return nullptr;
    } else if (this->hasError()) {
      return nullptr;
    }
    node->setFuncBody(std::move(blockNode));
    return node;
  }
  case TokenKind::LBC: { // implicit constructor
    auto node = std::make_unique<FunctionNode>(startPos, std::move(nameInfo),
                                               FunctionNode::IMPLICIT_CONSTRUCTOR);
    Token lbcToken = this->expect(TokenKind::LBC); // always success
    while (CUR_KIND() != TokenKind::RBC) {
      this->changeLexerModeToSTMT();
      if (CUR_KIND() == TokenKind::VAR || CUR_KIND() == TokenKind::LET) {
        unsigned int pos = START_POS();
        const bool readOnly = this->scan() == TokenKind::LET;
        auto param = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toName));
        TRY(this->expect(TokenKind::COLON, false));
        auto type = this->parse_typeName();
        if (this->incompleteNode) {
          Token token = this->incompleteNode->getToken();
          auto typeofNode =
              std::make_unique<TypeOfNode>(token.pos, std::move(this->incompleteNode), token);
          node->addParamNode(std::move(param), std::move(typeofNode));
          node->setFuncBody(std::make_unique<EmptyNode>(token));
          this->incompleteNode = std::move(node);
          return nullptr;
        } else if (this->hasError()) {
          return nullptr;
        }
        TRY(this->parse_statementEnd());
        node->addParamNode(pos, readOnly, std::move(param), std::move(type));
      } else {
        E_ALTER(TokenKind::VAR, TokenKind::LET, TokenKind::RBC);
      }
    }
    this->expect(TokenKind::RBC); // always success
    node->setFuncBody(std::make_unique<BlockNode>(lbcToken.pos));
    return node;
  }
  default:
    E_ALTER_OR_COMP(TokenKind::ASSIGN, TokenKind::COLON, TokenKind::LP, TokenKind::LBC);
  }
}

std::unique_ptr<BlockNode> Parser::parse_block() {
  GUARD_DEEP_NESTING(guard);

  auto ctx = this->inSkippableNLCtx(false);
  Token token = TRY(this->expect(TokenKind::LBC));
  auto blockNode = std::make_unique<BlockNode>(token.pos);
  while (CUR_KIND() != TokenKind::RBC) {
    auto node = this->parse_statement();
    if (this->incompleteNode) {
      blockNode->addNode(std::move(this->incompleteNode));
      this->incompleteNode = std::move(blockNode);
      return nullptr;
    } else if (this->hasError()) {
      return nullptr;
    }
    blockNode->addNode(std::move(node));
  }
  token = TRY(this->expect(TokenKind::RBC));
  blockNode->updateToken(token);
  return blockNode;
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::VAR || CUR_KIND() == TokenKind::LET);
  unsigned int startPos = START_POS();
  auto readOnly = VarDeclNode::VAR;
  if (CUR_KIND() == TokenKind::VAR) {
    this->consume();
  } else if (CUR_KIND() == TokenKind::LET) {
    this->consume();
    readOnly = VarDeclNode::LET;
  }

  auto nameInfo = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toName));
  std::unique_ptr<Node> exprNode;
  switch (CUR_KIND()) {
  case TokenKind::ASSIGN:
    this->consume(); // ASSIGN
    exprNode = TRY(this->parse_expression());
    break;
  case TokenKind::COLON: {
    this->expect(TokenKind::COLON, false);
    auto typeNode = TRY(this->parse_typeName());
    exprNode = std::make_unique<NewNode>(std::move(typeNode));
    break;
  }
  default:
    E_ALTER_OR_COMP(TokenKind::ASSIGN, TokenKind::COLON);
  }
  return std::make_unique<VarDeclNode>(startPos, std::move(nameInfo), std::move(exprNode),
                                       readOnly);
}

std::unique_ptr<Node> Parser::parse_ifExpression(bool asElif) {
  GUARD_DEEP_NESTING(guard);

  unsigned int startPos = START_POS();
  TRY(this->expect(asElif ? TokenKind::ELIF : TokenKind::IF));

  std::unique_ptr<Node> condNode;
  const bool ifLet = CUR_KIND() == TokenKind::LET;
  if (ifLet) {
    condNode = TRY(this->parse_variableDeclaration());
  } else {
    condNode = TRY(this->parse_expression());
  }
  auto thenNode = this->parse_block();
  if (this->incompleteNode) {
    assert(isa<BlockNode>(*this->incompleteNode));
    thenNode.reset(cast<BlockNode>(this->incompleteNode.release()));
    this->incompleteNode = std::make_unique<IfNode>(startPos, asElif, std::move(condNode),
                                                    std::move(thenNode), nullptr, ifLet);
    return nullptr;
  } else if (this->hasError()) {
    return nullptr;
  }

  // parse else
  std::unique_ptr<Node> elseNode;
  if (CUR_KIND() == TokenKind::ELIF) {
    elseNode = TRY(this->parse_ifExpression(true));
  } else if (CUR_KIND() == TokenKind::ELSE && this->lexer->getPrevMode().cond() == yycEXPR) {
    this->consume(); // ELSE
    elseNode = TRY(this->parse_block());
  }

  if (this->tryCompleteInfixKeywords({TokenKind::ELIF, TokenKind::ELSE})) {
    return nullptr;
  }
  return std::make_unique<IfNode>(startPos, asElif, std::move(condNode), std::move(thenNode),
                                  std::move(elseNode), ifLet);
}

std::unique_ptr<Node> Parser::parse_caseExpression() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::CASE);
  unsigned int pos = START_POS();
  this->consume(); // CASE

  auto caseNode = std::make_unique<CaseNode>(pos, TRY(this->parse_expression()));
  TRY(this->expect(TokenKind::LBC));
  do {
    caseNode->addArmNode(TRY(this->parse_armExpression()));
  } while (CUR_KIND() != TokenKind::RBC);
  Token token = this->expect(TokenKind::RBC); // always success
  caseNode->updateToken(token);
  return caseNode;
}

std::unique_ptr<ArmNode> Parser::parse_armExpression() {
  GUARD_DEEP_NESTING(guard);

  this->changeLexerModeToSTMT();

  std::unique_ptr<ArmNode> armNode;
  if (CUR_KIND() == TokenKind::ELSE) {
    unsigned int pos = START_POS();
    this->consume(); // ELSE
    armNode = std::make_unique<ArmNode>(pos);
  } else {
    if (this->inCompletionPoint()) {
      auto ref = this->lexer->toStrRef(this->curToken);
      if (StringRef("else").startsWith(ref) && !ref.empty()) {
        TRY(this->expect(TokenKind::ELSE));
      }
    }
    auto base = getPrecedence(TokenKind::PIPE) + 1;
    armNode = std::make_unique<ArmNode>(TRY(this->parse_expression(base)));
    while (CUR_KIND() == TokenKind::PIPE) {
      this->expect(TokenKind::PIPE); // always success
      armNode->addPatternNode(TRY(this->parse_expression(base)));
    }
  }

  TRY(this->expect(TokenKind::CASE_ARM));
  armNode->setActionNode(TRY(this->parse_expression()));
  TRY(this->parse_statementEnd());

  return armNode;
}

std::unique_ptr<Node> Parser::parse_forExpression() {
  GUARD_DEEP_NESTING(guard);

  unsigned int startPos = START_POS();
  TRY(this->expectAndChangeMode(TokenKind::FOR, yycPARAM));

  if (CUR_KIND() == TokenKind::LP) { // for
    auto ctx = this->inSkippableNLCtx();

    this->expectAndChangeMode(TokenKind::LP, yycSTMT); // always success

    auto initNode = TRY(this->parse_statementImpl());
    TRY(this->parse_statementEnd(true));

    auto condNode = TRY(this->parse_forCond());
    TRY(this->parse_statementEnd(true));

    auto iterNode = TRY(this->parse_forIter());

    TRY(this->expect(TokenKind::RP));
    auto blockNode = this->parse_block();
    bool comp = false;
    if (this->incompleteNode) {
      assert(isa<BlockNode>(*this->incompleteNode));
      comp = true;
      blockNode.reset(cast<BlockNode>(this->incompleteNode.release()));
    } else if (this->hasError()) {
      return nullptr;
    }

    auto node = std::make_unique<LoopNode>(startPos, std::move(initNode), std::move(condNode),
                                           std::move(iterNode), std::move(blockNode));
    if (comp) {
      this->incompleteNode = std::move(node);
    }
    return node;
  } else { // for-in
    auto key = TRY(this->expectName(TokenKind::PARAM_NAME, &Lexer::toName));
    NameInfo value({0, 0}, "");
    if (CUR_KIND() == TokenKind::COMMA) { // for k, v
      this->expectAndChangeMode(TokenKind::COMMA, yycPARAM);
      value = TRY(this->expectName(TokenKind::PARAM_NAME, &Lexer::toName));
    }
    TRY(this->expect(TokenKind::IN));
    auto exprNode = TRY(this->parse_expression());
    auto blockNode = this->parse_block();
    bool comp = false;
    if (this->incompleteNode) {
      assert(isa<BlockNode>(*this->incompleteNode));
      comp = true;
      blockNode.reset(cast<BlockNode>(this->incompleteNode.release()));
    } else if (this->hasError()) {
      return nullptr;
    }

    auto node = createForInNode(startPos, std::move(key), std::move(value), std::move(exprNode),
                                std::move(blockNode));
    if (comp) {
      this->incompleteNode = std::move(node);
    }
    return node;
  }
}

static bool lookahead_expression(TokenKind kind) {
  switch (kind) {
    // clang-format off
  EACH_LA_expression(GEN_LA_CASE) return true;
    // clang-format on
  default:
    return false;
  }
}

std::unique_ptr<Node> Parser::parse_forCond() {
  GUARD_DEEP_NESTING(guard);

  if (lookahead_expression(CUR_KIND())) {
    return this->parse_expression();
  } else if (CUR_KIND() != TokenKind::LINE_END) {
    E_DETAILED(ParseErrorKind::EXPR_END, EACH_LA_expression(GEN_LA_ALTER) TokenKind::LINE_END);
  } else {
    return nullptr;
  }
}

std::unique_ptr<Node> Parser::parse_forIter() {
  GUARD_DEEP_NESTING(guard);

  if (lookahead_expression(CUR_KIND())) {
    return this->parse_expression();
  } else if (CUR_KIND() != TokenKind::RP) {
    E_DETAILED(ParseErrorKind::EXPR_RP, EACH_LA_expression(GEN_LA_ALTER) TokenKind::RP);
  } else {
    return std::make_unique<EmptyNode>();
  }
}

std::unique_ptr<CatchNode> Parser::parse_catchBlock() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::CATCH);
  unsigned int startPos = START_POS();
  this->consume(); // CATCH

  bool paren = CUR_KIND() == TokenKind::LP;
  if (paren) {
    TRY(this->expect(TokenKind::LP));
  }

  auto nameInfo = TRY(this->expectName(TokenKind::PARAM_NAME, &Lexer::toName));
  std::unique_ptr<TypeNode> typeToken;
  if (CUR_KIND() == TokenKind::COLON) {
    this->expect(TokenKind::COLON, false); // always success
    typeToken = TRY(this->parse_typeName());
  }

  if (paren) {
    TRY(this->expect(TokenKind::RP));
  }

  auto blockNode = this->parse_block();
  bool comp = false;
  if (this->incompleteNode) {
    assert(isa<BlockNode>(*this->incompleteNode));
    comp = true;
    blockNode.reset(cast<BlockNode>(this->incompleteNode.release()));
  } else if (this->hasError()) {
    return nullptr;
  }

  auto node = std::make_unique<CatchNode>(startPos, std::move(nameInfo), std::move(typeToken),
                                          std::move(blockNode));
  if (comp) {
    this->incompleteNode = std::move(node);
  }
  return node;
}

// command
std::unique_ptr<Node> Parser::parse_command() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::COMMAND);
  Token token = this->expect(TokenKind::COMMAND); // always success

  if (CUR_KIND() == TokenKind::LP) { // command definition
    this->consume();                 // LP
    TRY(this->expect(TokenKind::RP));
    std::unique_ptr<TypeNode> returnTypeNode;
    if (CUR_KIND() == TokenKind::COLON) {
      TRY(this->expect(TokenKind::COLON, false));
      returnTypeNode = TRY(this->parse_typeName());
    }
    auto blockNode = TRY(this->parse_block());
    NameInfo nameInfo(token, this->lexer->toCmdArg(token));
    return std::make_unique<UserDefinedCmdNode>(token.pos, std::move(nameInfo),
                                                std::move(returnTypeNode), std::move(blockNode));
  }

  auto kind = this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
  auto node = std::make_unique<CmdNode>(
      std::make_unique<StringNode>(token, this->lexer->toCmdArg(token), kind));

  for (bool next = true; next && (this->hasSpace() || this->hasNewline());) {
    switch (CUR_KIND()) {
      // clang-format off
    EACH_LA_cmdArg_LP(GEN_LA_CASE)
      // clang-format on
      {
        auto argNode = this->parse_cmdArg();
        if (this->hasError()) {
          if (this->inCompletionPoint() &&
              hasFlag(this->ccHandler->getCompOp(), CodeCompOp::FILE)) {
            this->ccHandler->addCompHookRequest(*this->lexer, std::move(node));
          }
          return nullptr;
        }
        node->addArgNode(std::move(argNode));
        break;
      }
      // clang-format off
    EACH_LA_redir(GEN_LA_CASE)
      // clang-format on
      {
        node->addRedirNode(TRY(this->parse_redirOption()));
        break;
      }
    case TokenKind::INVALID:
      E_DETAILED(ParseErrorKind::CMD_ARG, EACH_LA_cmdArgs(GEN_LA_ALTER));
    default:
      next = false;
      break;
    }
  }

  switch (CUR_KIND()) {
    // clang-format off
  EACH_LA_redir(GEN_LA_CASE)
    // clang-format on
    {
      this->createError(this->curKind, this->curToken, REDIR_NEED_SPACE,
                        "require space before redirection");
    }
  default:
    break;
  }
  return node;
}

std::unique_ptr<RedirNode> Parser::parse_redirOption() {
  GUARD_DEEP_NESTING(guard);

  switch (CUR_KIND()) {
    // clang-format off
  EACH_LA_redir(GEN_LA_CASE)
    // clang-format on
    {
      Token token = this->curToken;
      TokenKind kind = this->scan();
      const bool hereDoc =
          kind == TokenKind::REDIR_HERE_DOC || kind == TokenKind::REDIR_HERE_DOC_DASH;
      auto parseOpt = CmdArgParseOpt::REDIR;
      if (hereDoc) {
        this->hereOp = {
            .kind = kind,
            .pos = token.pos,
        };
        setFlag(parseOpt, CmdArgParseOpt::HERE_START);
      }
      auto node = std::make_unique<RedirNode>(kind, token, this->lexer->toStrRef(token),
                                              TRY(this->parse_cmdArg(parseOpt)));
      if (hereDoc) {
        auto &argNode = node->getTargetNode();
        Token startToken = argNode.getToken();
        {
          StringRef ref = this->lexer->toStrRef(startToken);
          if (ref[0] == '\'') {
            ref.removePrefix(1);
            ref.removeSuffix(1);
          }
          node->setHereStart(NameInfo(startToken, ref.toString()));
        }
        argNode.refSegmentNodes().pop_back();
        argNode.addSegmentNode(std::make_unique<StringExprNode>(startToken.endPos()));
        this->hereDocNodes.push_back(makeObserver(*node));
      }
      return node;
    }
  default:
    E_ALTER_OR_COMP(EACH_LA_redir(GEN_LA_ALTER));
  }
}

size_t Parser::findHereDocNodeIndex(unsigned int pos) const {
  struct Compare {
    bool operator()(const ObserverPtr<RedirNode> &x, unsigned int y) const {
      return x->getPos() < y;
    }

    bool operator()(unsigned int x, const ObserverPtr<RedirNode> &y) const {
      return x < y->getPos();
    }
  };
  auto iter =
      std::lower_bound(this->hereDocNodes.begin(), this->hereDocNodes.end(), pos, Compare());
  if (iter != this->hereDocNodes.end() && (*iter)->getPos() == pos) {
    return static_cast<size_t>(iter - this->hereDocNodes.begin());
  }
  return static_cast<size_t>(this->hereDocNodes.size()); // normally unreachable
}

static bool shouldIgnoreTab(const StringExprNode &node) {
  if (node.getExprNodes().empty()) {
    return true; // first
  }
  auto &last = node.getExprNodes().back();
  if (isa<StringNode>(*last)) {
    auto &value = cast<StringNode>(*last).getValue();
    if (!value.empty() && value.back() == '\n') { // after newline
      return true;
    }
  }
  return false;
}

std::unique_ptr<Node> Parser::parse_hereDocBody() {
  GUARD_DEEP_NESTING(guard);

  if (this->hereDocNodes.empty()) {
    return nullptr;
  }

  while (this->inHereDocBody()) {
    if (CUR_KIND() == TokenKind::HERE_END) { // already exit here doc mode
      this->consume();
      continue;
    }

    const unsigned int pos = this->lexer->getHereDocState().pos;
    const auto attr = this->lexer->getHereDocState().attr;
    auto index = this->findHereDocNodeIndex(pos);
    assert(index < this->hereDocNodes.size());
    auto hereDocNode = this->hereDocNodes[index];
    auto *strExprNode =
        ({ cast<StringExprNode>(hereDocNode->getTargetNode().getSegmentNodes()[0].get()); });
    while (CUR_KIND() != TokenKind::HERE_END) {
      switch (CUR_KIND()) {
        // clang-format off
      EACH_LA_interpolation(GEN_LA_CASE)
        // clang-format on
        {
          auto interp = TRY(this->parse_interpolation(EmbedNode::STR_EXPR));
          strExprNode->addExprNode(std::move(interp));
          break;
        }
      case TokenKind::START_SUB_CMD: {
        auto subNode = TRY(this->parse_cmdSubstitution(true));
        strExprNode->addExprNode(std::move(subNode));
        break;
      }
      case TokenKind::BACKQUOTE_LITERAL: {
        auto subNode = TRY(this->parse_backquoteLiteral());
        strExprNode->addExprNode(std::move(subNode));
        break;
      }
      case TokenKind::STR_ELEMENT: {
        Token token = this->curToken;
        this->consume(); // always success
        auto newAttr = attr;
        if (hasFlag(newAttr, HereDocState::Attr::IGNORE_TAB) && !shouldIgnoreTab(*strExprNode)) {
          unsetFlag(newAttr, HereDocState::Attr::IGNORE_TAB);
        }
        auto subNode = std::make_unique<StringNode>(
            token, this->lexer->toHereDocBody(token, newAttr), StringNode::STRING);
        strExprNode->addExprNode(std::move(subNode));
        break;
      }
      case TokenKind::EOS:
        if (hasFlag(this->option, ParserOption::NEED_HERE_END) ||
            this->lexer->hereDocStateDepth() > 1) {
          TokenKind kinds[] = {TokenKind::HERE_END};
          std::string suffix = ": `";
          suffix += this->lexer->toStrRef(this->lexer->getHereDocState().token);
          suffix += "'";
          this->reportDetailedError(ParseErrorKind::HERE_END, 1, kinds, suffix.c_str());
        }
        return nullptr; // here-doc reach end even if EOS
      default:
        if (hasFlag(attr, HereDocState::Attr::EXPAND)) {
          if (this->inVarNameCompletionPoint()) {
            this->makeCodeComp(CodeCompNode::VAR, nullptr, this->curToken);
          } else if (this->inCompletionPointAt(TokenKind::EOS)) {
            TokenKind kinds[] = {EACH_LA_hereExpand(GEN_LA_ALTER)};
            this->ccHandler->addExpectedTokenRequests(std::string(), kinds);
          }
        }
        E_ALTER(EACH_LA_hereExpand(GEN_LA_ALTER)); // FIXME: completion in no-expand
      }
    }
    auto token = TRY(this->expect(TokenKind::HERE_END));
    token.size--; // skip last newline
    hereDocNode->setHereEnd(token);
    this->hereDocNodes.erase(this->hereDocNodes.begin() + index);
  }
  return nullptr;
}

static bool lookahead_cmdArg_LP(TokenKind kind) {
  switch (kind) {
    // clang-format off
  EACH_LA_cmdArg_LP(GEN_LA_CASE) return true;
    // clang-format on
  default:
    return false;
  }
}

std::unique_ptr<CmdArgNode> Parser::parse_cmdArg(CmdArgParseOpt opt) {
  GUARD_DEEP_NESTING(guard);

  assert(!hasFlag(opt, CmdArgParseOpt::FIRST));
  auto node = std::make_unique<CmdArgNode>(this->curToken);
  TRY(this->parse_cmdArgSeg(*node, opt | CmdArgParseOpt::FIRST));

  while (!this->hasSpace() && !this->hasNewline() && lookahead_cmdArg_LP(CUR_KIND())) {
    if (hasFlag(opt, CmdArgParseOpt::HERE_START)) {
      this->createError(this->curKind, this->curToken, HERE_START_NEED_SPACE,
                        "require space after here doc start word");
      return nullptr;
    }
    TRY(this->parse_cmdArgSeg(*node, opt));
  }
  return node;
}

static bool isBrace(TokenKind kind) {
  switch (kind) {
  case TokenKind::BRACE_OPEN:
  case TokenKind::BRACE_SEP:
  case TokenKind::BRACE_CLOSE:
    return true;
  default:
    return false;
  }
}

static bool isHereDocStart(StringRef ref) {
  if (ref.size() > 2 && ref[0] == '\'' && ref.back() == '\'') {
    ref.removePrefix(1);
    ref.removeSuffix(1);
  }
  unsigned int count = 0;
  for (auto ch : ref) {
    if (isalnum(ch) || ch == '-' || ch == '_') {
      count++;
    } else {
      return false;
    }
  }
  return count > 0;
}

std::unique_ptr<Node> Parser::parse_cmdArgSeg(CmdArgNode &argNode, CmdArgParseOpt opt) {
  GUARD_DEEP_NESTING(guard);

  if (hasFlag(opt, CmdArgParseOpt::HERE_START)) {
    if (CUR_KIND() != TokenKind::CMD_ARG_PART && CUR_KIND() != TokenKind::STRING_LITERAL) {
      this->reportHereDocStartError(this->curKind, this->curToken);
      return nullptr;
    }
  }

  switch (CUR_KIND()) {
  case TokenKind::CMD_ARG_PART: {
    Token token = this->curToken;
    if (hasFlag(opt, CmdArgParseOpt::HERE_START)) {
      if (isHereDocStart(this->lexer->toStrRef(token))) {
        this->lexer->setHereDocStart(this->hereOp.kind, token, this->hereOp.pos);
      } else {
        this->reportHereDocStartError(this->curKind, token);
        return nullptr;
      }
      this->curKind = TokenKind::HERE_START;
    }
    const auto prevKind = this->consumedKind;
    this->consume(); // always success
    if (hasFlag(opt, CmdArgParseOpt::ASSIGN)) {
      iteratePathList(*this->lexer, token, ':', [&](Token subToken, bool) {
        auto kind = StringNode::STRING;
        if (this->lexer->startsWith(subToken, '~') &&
            (subToken.pos > token.pos || hasFlag(opt, CmdArgParseOpt::FIRST))) {
          kind = StringNode::TILDE;
        }
        this->addCmdArgSeg(argNode, subToken, kind);
        return true;
      });
    } else if (hasFlag(opt, CmdArgParseOpt::FIRST) && !hasFlag(opt, CmdArgParseOpt::MODULE) &&
               !hasFlag(opt, CmdArgParseOpt::REDIR) &&
               !this->lexer->startsWith(token, '~')) { // for 'dd if=path' style argument
      Token prefixToken = token;
      iteratePathList(*this->lexer, token, '=', [&](Token subToken, bool) {
        prefixToken = subToken;
        return false;
      });
      this->addCmdArgSeg(argNode, prefixToken, StringNode::STRING);
      if (prefixToken != token) { // prefix='if=', remain='path'
        auto remainToken = token.sliceFrom(prefixToken.size);
        auto kind = StringNode::STRING;
        if (this->lexer->startsWith(remainToken, '~') && prefixToken.size > 1) {
          kind = StringNode::TILDE;
        }
        this->addCmdArgSeg(argNode, remainToken, kind);
      }
    } else {
      auto kind = StringNode::STRING;
      if (hasFlag(opt, CmdArgParseOpt::FIRST) || isBrace(prevKind)) {
        if (this->lexer->startsWith(token, '~')) {
          kind = StringNode::TILDE;
        }
      }
      this->addCmdArgSeg(argNode, token, kind);
    }
    return nullptr;
  }
  case TokenKind::BRACE_CHAR_SEQ:
  case TokenKind::BRACE_INT_SEQ: {
    Token token = this->curToken;
    if (!this->parse_braceSeq(argNode)) {
      this->addCmdArgSeg(argNode, token, StringNode::STRING);
    }
    return nullptr;
  }
  default: {
    auto node = TRY(this->parse_cmdArgSegImpl(opt));
    argNode.addSegmentNode(std::move(node));
    return nullptr;
  }
  }
}

std::unique_ptr<Node> Parser::parse_cmdArgSegImpl(CmdArgParseOpt opt) {
  GUARD_DEEP_NESTING(guard);

  switch (CUR_KIND()) {
  case TokenKind::GLOB_ANY: {
    Token token = this->curToken;
    this->consume();
    return std::make_unique<WildCardNode>(token, ExpandMeta::ANY);
  }
  case TokenKind::GLOB_ZERO_OR_MORE: {
    Token token = this->curToken;
    this->consume();
    return std::make_unique<WildCardNode>(token, ExpandMeta::ZERO_OR_MORE);
  }
  case TokenKind::BRACE_OPEN:
  case TokenKind::BRACE_CLOSE:
  case TokenKind::BRACE_SEP: {
    Token token = this->curToken;
    TokenKind kind = this->scan();
    ExpandMeta meta = ExpandMeta::BRACE_SEP;
    if (kind == TokenKind::BRACE_OPEN) {
      meta = ExpandMeta::BRACE_OPEN;
    } else if (kind == TokenKind::BRACE_CLOSE) {
      meta = ExpandMeta::BRACE_CLOSE;
    }
    auto node = std::make_unique<WildCardNode>(token, meta);
    node->setExpand(false);
    return node;
  }
  case TokenKind::STRING_LITERAL:
    return this->parse_stringLiteral(hasFlag(opt, CmdArgParseOpt::HERE_START));
  case TokenKind::OPEN_DQUOTE:
    return this->parse_stringExpression();
  case TokenKind::START_SUB_CMD:
    return this->parse_cmdSubstitution();
  case TokenKind::BACKQUOTE_LITERAL:
    return this->parse_backquoteLiteral();
  case TokenKind::START_IN_SUB:
  case TokenKind::START_OUT_SUB:
    return this->parse_procSubstitution();
    // clang-format off
  EACH_LA_paramExpansion(GEN_LA_CASE) return this->parse_paramExpansion();
    // clang-format on
  default:
    if (this->inVarNameCompletionPoint()) {
      this->makeCodeComp(CodeCompNode::VAR_IN_CMD_ARG, nullptr, this->curToken);
    } else if (this->inCompletionPointAt(TokenKind::CMD_ARG_PART)) {
      this->tryCompleteFileNames(opt);
    }
    E_DETAILED(hasFlag(opt, CmdArgParseOpt::MODULE)  ? ParseErrorKind::MOD_PATH
               : hasFlag(opt, CmdArgParseOpt::REDIR) ? ParseErrorKind::REDIR
                                                     : ParseErrorKind::CMD_ARG,
               EACH_LA_cmdArg(GEN_LA_ALTER));
  }
}

bool Parser::parse_braceSeq(CmdArgNode &argNode) {
  assert(CUR_KIND() == TokenKind::BRACE_CHAR_SEQ || CUR_KIND() == TokenKind::BRACE_INT_SEQ);
  const Token token = this->curToken;
  const TokenKind kind = this->scan();
  const Token seqToken = token.slice(1, token.size - 1); // skip '{' '}'

  argNode.addSegmentNode(
      std::make_unique<WildCardNode>(token.slice(0, 1), ExpandMeta::BRACE_SEQ_OPEN));
  auto node = std::make_unique<BraceSeqNode>(seqToken, kind == TokenKind::BRACE_CHAR_SEQ
                                                           ? BraceRange::Kind::UNINIT_CHAR
                                                           : BraceRange::Kind::UNINIT_INT);
  argNode.addSegmentNode(std::move(node));
  argNode.addSegmentNode(
      std::make_unique<WildCardNode>(token.sliceFrom(token.size - 1), ExpandMeta::BRACE_SEQ_CLOSE));
  return true;
}

static std::unique_ptr<Node> createBinaryNode(std::unique_ptr<Node> &&leftNode, TokenKind op,
                                              Token token, std::unique_ptr<Node> &&rightNode) {
  if (op == TokenKind::PIPE) {
    if (isa<PipelineNode>(*leftNode)) {
      cast<PipelineNode>(leftNode.get())->addNode(std::move(rightNode));
      return std::move(leftNode);
    }
    return std::make_unique<PipelineNode>(std::move(leftNode), std::move(rightNode));
  }
  if (isAssignOp(op)) {
    return createAssignNode(std::move(leftNode), op, token, std::move(rightNode));
  }
  return std::make_unique<BinaryOpNode>(std::move(leftNode), op, token, std::move(rightNode));
}

/**
 * see. https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing
 * @param basePrecedence
 * @return
 */
std::unique_ptr<Node> Parser::parse_expressionImpl(unsigned int basePrecedence) {
  GUARD_DEEP_NESTING(guard);

  auto node = TRY(this->parse_unaryExpression());
  while (!this->hasLineTerminator()) {
    if (this->tryCompleteInfixKeywords({
#define GEN_TABLE(E) TokenKind::E,
            EACH_INFIX_OPERATOR_KW(GEN_TABLE)
#undef GEN_TABLE
        })) {
      return nullptr;
    }

    const auto info = getOpInfo(this->curKind);
    if (!hasFlag(info.attr, OperatorAttr::INFIX) || info.prece < basePrecedence) {
      break;
    }

    switch (this->curKind) {
    case TokenKind::AS: {
      this->expect(TokenKind::AS, false); // always success
      auto type = TRY(this->parse_typeName());
      node = std::make_unique<TypeOpNode>(std::move(node), std::move(type), TypeOpNode::NO_CAST);
      break;
    }
    case TokenKind::AS_OPT: {
      this->expect(TokenKind::AS_OPT, false); // always success
      auto type = TRY(this->parse_typeName());
      node = std::make_unique<TypeOpNode>(std::move(node), std::move(type),
                                          TypeOpNode::CHECK_CAST_OPT);
      break;
    }
    case TokenKind::IS: {
      this->expect(TokenKind::IS, false); // always success
      auto type = TRY(this->parse_typeName());
      node =
          std::make_unique<TypeOpNode>(std::move(node), std::move(type), TypeOpNode::ALWAYS_FALSE);
      break;
    }
    case TokenKind::WITH: {
      this->consume(); // WITH
      auto redirNode = TRY(this->parse_redirOption());
      auto withNode = std::make_unique<WithNode>(std::move(node), std::move(redirNode));
      for (bool next = true; next && this->hasSpace();) {
        switch (CUR_KIND()) {
          // clang-format off
        EACH_LA_redir(GEN_LA_CASE)
          // clang-format on
          {
            withNode->addRedirNode(TRY(this->parse_redirOption()));
            break;
          }
        case TokenKind::INVALID:
        case TokenKind::COMPLETION:
          E_ALTER_OR_COMP(EACH_LA_redir(GEN_LA_ALTER));
        default:
          next = false;
          break;
        }
      }
      node = std::move(withNode);
      break;
    }
    case TokenKind::TERNARY: {
      this->consume(); // TERNARY
      auto tleftNode = this->parse_expression(getPrecedence(TokenKind::TERNARY));
      std::unique_ptr<Node> trightNode;
      bool comp = false;
      if (this->incompleteNode) {
        comp = true;
        tleftNode = std::move(this->incompleteNode);
      } else if (this->hasError()) {
        return nullptr;
      } else {
        TRY(this->expectAndChangeMode(TokenKind::COLON, yycSTMT));
        trightNode = TRY(this->parse_expression(getPrecedence(TokenKind::TERNARY)));
      }
      unsigned int pos = node->getPos();
      node = std::make_unique<IfNode>(pos, false, std::move(node), std::move(tleftNode),
                                      std::move(trightNode), false);
      if (comp) {
        this->incompleteNode = std::move(node);
        return nullptr;
      }
      break;
    }
    case TokenKind::BACKGROUND:
    case TokenKind::DISOWN_BG: {
      Token token = this->curToken;
      bool disown = this->scan() == TokenKind::DISOWN_BG;
      return ForkNode::newBackground(std::move(node), token, disown);
    }
    default: {
      Token token = this->curToken;
      TokenKind op = this->scan();
      unsigned int nextPrece = info.prece + (hasFlag(info.attr, OperatorAttr::RASSOC) ? 0 : 1);
      auto rightNode = this->parse_expression(nextPrece);
      bool comp = false;
      if (this->incompleteNode) {
        comp = true;
        rightNode = std::move(this->incompleteNode);
      } else if (this->hasError()) {
        return nullptr;
      }
      node = createBinaryNode(std::move(node), op, token, std::move(rightNode));
      if (comp) {
        this->incompleteNode = std::move(node);
        return nullptr;
      }
      break;
    }
    }
  }
  return node;
}

std::unique_ptr<Node> Parser::parse_expression(unsigned int basePrecedence) {
  auto node = TRY(this->parse_expressionImpl(basePrecedence));
  if (this->hasNewline()) {
    TRY(this->parse_hereDocBody());
  }
  return node;
}

std::unique_ptr<Node> Parser::parse_unaryExpression() {
  GUARD_DEEP_NESTING(guard);

  switch (CUR_KIND()) {
  case TokenKind::PLUS:
  case TokenKind::MINUS:
  case TokenKind::NOT: {
    Token token = this->curToken;
    TokenKind op = this->scan();
    return std::make_unique<UnaryOpNode>(op, token, TRY(this->parse_unaryExpression()));
  }
  case TokenKind::COPROC: {
    auto token = this->expect(TokenKind::COPROC); // always success
    auto exprNode = TRY(this->parse_expression(getPrecedence(TokenKind::COPROC)));
    return ForkNode::newCoproc(token, std::move(exprNode));
  }
  case TokenKind::TIME: {
    auto token = this->expect(TokenKind::TIME); // always success
    auto exprNode = TRY(this->parse_expression(getPrecedence(TokenKind::TIME)));
    return std::make_unique<TimeNode>(token, std::move(exprNode));
  }
  default:
    return this->parse_suffixExpression();
  }
}

std::unique_ptr<Node> Parser::parse_suffixExpression() {
  GUARD_DEEP_NESTING(guard);

  auto node = TRY(this->parse_primaryExpression());

  for (bool next = true; !this->hasLineTerminator() && next;) {
    switch (CUR_KIND()) {
    case TokenKind::ACCESSOR: {
      this->consume(); // ACCESSOR
      if (this->inCompletionPointAt(TokenKind::IDENTIFIER)) {
        this->makeCodeComp(CodeCompNode::MEMBER, std::move(node), this->curToken);
      }
      auto nameInfo = TRY(this->expectName(TokenKind::IDENTIFIER, &Lexer::toName));
      node = std::make_unique<AccessNode>(std::move(node), std::move(nameInfo));
      if (CUR_KIND() == TokenKind::LP && !this->hasLineTerminator()) { // treat as method call
        auto argsNode = TRY(this->parse_arguments());
        node = std::make_unique<ApplyNode>(std::move(node), std::move(argsNode),
                                           ApplyNode::METHOD_CALL);
      }
      break;
    }
    case TokenKind::LB: {
      auto opToken = this->curToken;
      this->consume(); // LB
      auto indexNode = TRY(this->parse_expression());
      auto token = TRY(this->expect(TokenKind::RB));
      node = ApplyNode::newIndexCall(std::move(node), opToken, std::move(indexNode));
      node->updateToken(token);
      break;
    }
    case TokenKind::LP: {
      auto argsNode = TRY(this->parse_arguments());
      node =
          std::make_unique<ApplyNode>(std::move(node), std::move(argsNode), ApplyNode::FUNC_CALL);
      break;
    }
    case TokenKind::INC:
    case TokenKind::DEC: {
      Token token = this->curToken;
      TokenKind op = this->scan();
      node = createSuffixNode(std::move(node), op, token);
      break;
    }
    case TokenKind::UNWRAP: {
      Token token = this->curToken;
      TokenKind op = this->scan(); // UNWRAP
      node = std::make_unique<UnaryOpNode>(std::move(node), op, token);
      break;
    }
    default:
      next = false;
      break;
    }
  }
  return node;
}

static std::unique_ptr<Node> createTupleOrGroup(Token open,
                                                std::vector<std::unique_ptr<Node>> &&nodes,
                                                Token close, unsigned int commaCount) {
  if (commaCount == 0) {
    auto node = std::move(nodes[0]);
    node->setParenPos(open.pos, close);
    return node;
  } else {
    return std::make_unique<TupleNode>(open.pos, std::move(nodes), close);
  }
}

std::unique_ptr<Node> Parser::parse_primaryExpression() {
  GUARD_DEEP_NESTING(guard);

  switch (CUR_KIND()) {
  case TokenKind::COMMAND:
    return this->parse_command();
  case TokenKind::ENV_ASSIGN:
    return this->parse_prefixAssign();
  case TokenKind::NEW: {
    unsigned int startPos = START_POS();
    this->expect(TokenKind::NEW, false); // always success
    auto type = TRY(this->parse_typeName());
    auto argsNode = TRY(this->parse_arguments());
    return std::make_unique<NewNode>(startPos, std::move(type), std::move(argsNode));
  }
  case TokenKind::INT_LITERAL: {
    Token token = TRY(this->expect(TokenKind::INT_LITERAL));
    return NumberNode::newInt(token);
  }
  case TokenKind::FLOAT_LITERAL: {
    Token token = TRY(this->expect(TokenKind::FLOAT_LITERAL));
    return NumberNode::newFloat(token);
  }
  case TokenKind::STRING_LITERAL:
    return this->parse_stringLiteral();
  case TokenKind::REGEX_LITERAL:
    return this->parse_regexLiteral();
  case TokenKind::BACKQUOTE_LITERAL:
    return this->parse_backquoteLiteral();
  case TokenKind::OPEN_DQUOTE:
    return this->parse_stringExpression();
  case TokenKind::START_SUB_CMD:
    return this->parse_cmdSubstitution();
  case TokenKind::APPLIED_NAME:
  case TokenKind::SPECIAL_NAME:
    return this->parse_appliedName(CUR_KIND() == TokenKind::SPECIAL_NAME);
  case TokenKind::START_IN_SUB:
  case TokenKind::START_OUT_SUB:
    return this->parse_procSubstitution();
  case TokenKind::AT_PAREN:
    return this->parse_cmdArgArray();
  case TokenKind::LP: { // group, tuple or anonymous command
    auto ctx = this->inSkippableNLCtx();
    Token openToken = this->expect(TokenKind::LP); // always success
    unsigned int count = 0;
    std::vector<std::unique_ptr<Node>> nodes;
    while (CUR_KIND() != TokenKind::RP) {
      nodes.push_back(TRY(this->parse_expression()));
      if (CUR_KIND() == TokenKind::COMMA) {
        this->consume(); // COMMA
        count++;
      } else if (CUR_KIND() != TokenKind::RP) {
        E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RP);
      }
    }
    Token closeToken = TRY(this->expect(TokenKind::RP));
    if (nodes.empty()) { // anonymous command
      auto blockNode = TRY(this->parse_block());
      return std::make_unique<UserDefinedCmdNode>(openToken.pos, NameInfo({0, 0}, ""), nullptr,
                                                  std::move(blockNode));
    } else {
      return createTupleOrGroup(openToken, std::move(nodes), closeToken, count);
    }
  }
  case TokenKind::LB: { // array or map
    auto ctx = this->inSkippableNLCtx();
    Token token = this->expect(TokenKind::LB); // always success
    auto keyNode = TRY(this->parse_expression());
    std::unique_ptr<Node> node;
    if (CUR_KIND() == TokenKind::COMMA || CUR_KIND() == TokenKind::RB) { // array
      node = TRY(this->parse_arrayBody(token, std::move(keyNode)));
    } else if (CUR_KIND() == TokenKind::COLON) { // map
      node = TRY(this->parse_mapBody(token, std::move(keyNode)));
    } else {
      E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RB, TokenKind::COLON);
    }
    token = TRY(this->expect(TokenKind::RB));
    node->updateToken(token);
    return node;
  }
  case TokenKind::LBC:
    return this->parse_block();
  case TokenKind::FOR:
    return this->parse_forExpression();
  case TokenKind::IF:
    return this->parse_ifExpression();
  case TokenKind::CASE:
    return this->parse_caseExpression();
  case TokenKind::WHILE: {
    unsigned int startPos = START_POS();
    this->consume(); // WHILE
    auto condNode = TRY(this->parse_expression());
    auto blockNode = TRY(this->parse_block());
    return std::make_unique<LoopNode>(startPos, std::move(condNode), std::move(blockNode));
  }
  case TokenKind::DO: {
    unsigned int startPos = START_POS();
    this->consume(); // DO
    auto blockNode = TRY(this->parse_block());
    TRY(this->expect(TokenKind::WHILE));
    auto condNode = TRY(this->parse_expression());
    return std::make_unique<LoopNode>(startPos, std::move(condNode), std::move(blockNode), true);
  }
  case TokenKind::TRY: {
    unsigned int startPos = START_POS();
    this->consume(); // TRY
    auto tryNode = std::make_unique<TryNode>(startPos, TRY(this->parse_block()));

    switch (CUR_KIND()) {
    case TokenKind::CATCH:
    case TokenKind::FINALLY:
      break;
    default:
      E_ALTER_OR_COMP(TokenKind::CATCH, TokenKind::FINALLY, TokenKind::COMPLETION);
    }

    // parse catch
    while (CUR_KIND() == TokenKind::CATCH) {
      tryNode->addCatchNode(TRY(this->parse_catchBlock()));
    }

    // parse finally
    if (CUR_KIND() == TokenKind::FINALLY) {
      Token token = this->expect(TokenKind::FINALLY); // always success
      auto deferNode = std::make_unique<DeferNode>(token.pos, TRY(this->parse_block()));
      tryNode->addFinallyNode(std::move(deferNode));
    }

    if (this->tryCompleteInfixKeywords({TokenKind::CATCH, TokenKind::FINALLY})) {
      return nullptr;
    }
    return tryNode;
  }
  case TokenKind::FUNCTION:
    return this->parse_function();
  case TokenKind::BREAK: {
    Token token = this->expect(TokenKind::BREAK); // always success
    std::unique_ptr<Node> exprNode;
    if (!this->hasLineTerminator() && lookahead_expression(CUR_KIND())) {
      exprNode = TRY(this->parse_expression());
    }
    return JumpNode::newBreak(token, std::move(exprNode));
  }
  case TokenKind::CONTINUE: {
    Token token = this->expect(TokenKind::CONTINUE); // always success
    return JumpNode::newContinue(token);
  }
  case TokenKind::RETURN: {
    Token token = this->expect(TokenKind::RETURN); // always success
    std::unique_ptr<Node> exprNode;
    if (!this->hasLineTerminator() && lookahead_expression(CUR_KIND())) {
      exprNode = TRY(this->parse_expression());
    }
    return JumpNode::newReturn(token, std::move(exprNode));
  }
  case TokenKind::THROW: {
    auto token = this->expect(TokenKind::THROW); // always success
    auto exprNode = TRY(this->parse_expression());
    return JumpNode::newThrow(token, std::move(exprNode));
  }
  default:
    if (this->inCompletionPoint()) {
      if (this->inVarNameCompletionPoint()) {
        this->makeCodeComp(CodeCompNode::VAR, nullptr, this->curToken);
      } else if (!this->inCompletionPointAt(TokenKind::EOS) ||
                 this->consumedKind != TokenKind::EOS) {
        CodeCompletionHandler::CMD_OR_KW_OP op{};
        if (this->lexer->startsWith(this->curToken, '~')) {
          setFlag(op, CodeCompletionHandler::CMD_OR_KW_OP::TILDE);
        }
        if (this->inStmtCompCtx) {
          setFlag(op, CodeCompletionHandler::CMD_OR_KW_OP::STMT);
        }
        this->ccHandler->addCmdOrKeywordRequest(this->lexer->toCmdArg(this->curToken), op);
      }
    }
    E_DETAILED(ParseErrorKind::EXPR, EACH_LA_primary(GEN_LA_ALTER));
  }
}

std::unique_ptr<Node> Parser::parse_arrayBody(Token token, std::unique_ptr<Node> &&firstNode) {
  GUARD_DEEP_NESTING(guard);

  auto arrayNode = std::make_unique<ArrayNode>(token.pos, std::move(firstNode));
  for (bool next = true; next;) {
    switch (CUR_KIND()) {
    case TokenKind::COMMA:
      this->consume(); // COMMA
      if (CUR_KIND() != TokenKind::RB) {
        arrayNode->addExprNode(TRY(this->parse_expression()));
      }
      break;
    case TokenKind::RB:
      next = false;
      break;
    default:
      E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RB);
    }
  }
  return arrayNode;
}

std::unique_ptr<Node> Parser::parse_mapBody(Token token, std::unique_ptr<Node> &&keyNode) {
  GUARD_DEEP_NESTING(guard);

  this->expectAndChangeMode(TokenKind::COLON, yycSTMT); // always success

  auto valueNode = TRY(this->parse_expression());
  auto mapNode = std::make_unique<MapNode>(token.pos, std::move(keyNode), std::move(valueNode));
  for (bool next = true; next;) {
    switch (CUR_KIND()) {
    case TokenKind::COMMA:
      this->consume(); //  COMMA
      if (CUR_KIND() != TokenKind::RB) {
        keyNode = TRY(this->parse_expression());
        TRY(this->expectAndChangeMode(TokenKind::COLON, yycSTMT));
        valueNode = TRY(this->parse_expression());
        mapNode->addEntry(std::move(keyNode), std::move(valueNode));
      }
      break;
    case TokenKind::RB:
      next = false;
      break;
    default:
      E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RB);
    }
  }
  return mapNode;
}

std::unique_ptr<Node> Parser::parse_appliedName(bool asSpecialName) {
  Token token =
      TRY(this->expect(asSpecialName ? TokenKind::SPECIAL_NAME : TokenKind::APPLIED_NAME));
  return this->newVarNode(token);
}

std::unique_ptr<Node> Parser::parse_stringLiteral(bool asHereStart) {
  assert(CUR_KIND() == TokenKind::STRING_LITERAL);

  Token token = this->curToken;
  if (asHereStart) {
    if (isHereDocStart(this->lexer->toStrRef(token))) {
      this->lexer->setHereDocStart(this->hereOp.kind, token, this->hereOp.pos);
    } else {
      this->reportHereDocStartError(this->curKind, token);
      return nullptr;
    }
    this->curKind = TokenKind::HERE_START; // for syntax highlight
  }
  this->consume(); // always success
  return std::make_unique<StringNode>(token);
}

std::unique_ptr<Node> Parser::parse_regexLiteral() {
  Token token = this->expect(TokenKind::REGEX_LITERAL);           // always success
  std::string str = this->lexer->toTokenText(token.sliceFrom(2)); // skip prefix '$/'
  const char *ptr = strrchr(str.c_str(), '/');
  assert(ptr);
  std::string flag = ptr + 1;
  for (; str.back() != '/'; str.pop_back())
    ;
  str.pop_back(); // skip suffix '/'
  return std::make_unique<RegexNode>(token, std::move(str), std::move(flag));
}

std::unique_ptr<Node> Parser::parse_backquoteLiteral() {
  Token token = TRY(this->expect(TokenKind::BACKQUOTE_LITERAL));
  return std::make_unique<StringNode>(token, this->lexer->toTokenText(token),
                                      StringNode::BACKQUOTE);
}

std::unique_ptr<ArgsNode> Parser::parse_arguments(Token first) {
  GUARD_DEEP_NESTING(guard);

  auto ctx = this->inSkippableNLCtx();
  Token token = first.size == 0 ? TRY(this->expect(TokenKind::LP)) : first;

  auto argsNode = std::make_unique<ArgsNode>(token);
  for (unsigned int count = 0; CUR_KIND() != TokenKind::RP; count++) {
    if (count > 0) {
      if (CUR_KIND() != TokenKind::COMMA) {
        E_ALTER_OR_COMP(TokenKind::COMMA, TokenKind::RP);
      }
      this->consume(); // COMMA
    }
    if (lookahead_expression(CUR_KIND())) {
      argsNode->addNode(TRY(this->parse_expression()));
    } else {
      E_DETAILED(ParseErrorKind::EXPR_RP, EACH_LA_expression(GEN_LA_ALTER) TokenKind::RP);
    }
  }
  token = this->expect(TokenKind::RP); // always success
  argsNode->updateToken(token);
  return argsNode;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::OPEN_DQUOTE);
  Token token = this->expect(TokenKind::OPEN_DQUOTE); // always success
  auto node = std::make_unique<StringExprNode>(token.pos);

  for (bool next = true; next;) {
    switch (CUR_KIND()) {
    case TokenKind::STR_ELEMENT: {
      token = this->expect(TokenKind::STR_ELEMENT); // always success
      node->addExprNode(
          std::make_unique<StringNode>(token, this->lexer->doubleElementToString(token)));
      break;
    }
      // clang-format off
    EACH_LA_interpolation(GEN_LA_CASE)
      // clang-format on
      {
        auto interp = TRY(this->parse_interpolation(EmbedNode::STR_EXPR));
        node->addExprNode(std::move(interp));
        break;
      }
    case TokenKind::START_SUB_CMD: {
      auto subNode = TRY(this->parse_cmdSubstitution(true));
      node->addExprNode(std::move(subNode));
      break;
    }
    case TokenKind::BACKQUOTE_LITERAL: {
      auto subNode = TRY(this->parse_backquoteLiteral());
      node->addExprNode(std::move(subNode));
      break;
    }
    case TokenKind::CLOSE_DQUOTE:
      next = false;
      break;
    default:
      if (this->inVarNameCompletionPoint()) {
        this->makeCodeComp(CodeCompNode::VAR, nullptr, this->curToken);
      } else if (this->inCompletionPointAt(TokenKind::EOS)) {
        TokenKind kinds[] = {EACH_LA_stringExpression(GEN_LA_ALTER)};
        this->ccHandler->addExpectedTokenRequests(std::string(), kinds);
      }
      E_ALTER(EACH_LA_stringExpression(GEN_LA_ALTER));
    }
  }

  token = TRY(this->expect(TokenKind::CLOSE_DQUOTE));
  node->updateToken(token);
  return node;
}

std::unique_ptr<Node> Parser::toAccessNode(Token token) const {
  std::unique_ptr<Node> node;
  std::vector<NameInfo> names;

  const char *ptr = this->lexer->toStrRef(token).data();
  for (unsigned int index = token.size - 1; index != 0; index--) {
    if (ptr[index] == '.') {
      Token fieldToken = token.sliceFrom(index + 1);
      names.emplace_back(fieldToken, this->lexer->toName(fieldToken));
      token = token.slice(0, index);
    }
  }
  node = this->newVarNode(token);
  for (; !names.empty(); names.pop_back()) {
    node = std::make_unique<AccessNode>(std::move(node), std::move(names.back()));
  }
  return node;
}

std::unique_ptr<Node> Parser::parse_interpolation(EmbedNode::Kind kind) {
  GUARD_DEEP_NESTING(guard);

  switch (CUR_KIND()) {
  case TokenKind::APPLIED_NAME:
  case TokenKind::SPECIAL_NAME: {
    auto node = this->parse_appliedName(CUR_KIND() == TokenKind::SPECIAL_NAME);
    return std::make_unique<EmbedNode>(kind, std::move(node));
  }
  case TokenKind::APPLIED_NAME_WITH_FIELD: {
    const Token token = this->expect(TokenKind::APPLIED_NAME_WITH_FIELD);

    // split `${recv.field1.field2}'
    // split begin token `${'
    Token beginToken = token.slice(0, 2);

    // split inner names
    Token innerToken = token.slice(2, token.size - 1);

    // split end token `}'
    Token endToken = token.sliceFrom(token.size - 1);

    return std::make_unique<EmbedNode>(beginToken.pos, kind, this->toAccessNode(innerToken),
                                       endToken);
  }
  default:
    auto ctx = this->inSkippableNLCtx();
    unsigned int pos = START_POS();
    TRY(this->expect(TokenKind::START_INTERP));
    auto node = TRY(this->parse_expression());
    auto endToken = TRY(this->expect(TokenKind::RBC));
    return std::make_unique<EmbedNode>(pos, kind, std::move(node), endToken);
  }
}

std::unique_ptr<Node> Parser::parse_paramExpansion() {
  GUARD_DEEP_NESTING(guard);

  switch (CUR_KIND()) {
  case TokenKind::APPLIED_NAME_WITH_BRACKET:
  case TokenKind::SPECIAL_NAME_WITH_BRACKET: { // $name[
    auto ctx = this->inSkippableNLCtx();
    Token token = this->curToken;
    this->consume(); // always success
    auto varNode = this->newVarNode(token);
    auto indexNode = TRY(this->parse_expression());
    Token opToken = token.sliceFrom(token.size - 1); // last '['

    token = TRY(this->expect(TokenKind::RB));
    auto node = ApplyNode::newIndexCall(std::move(varNode), opToken, std::move(indexNode));
    node->updateToken(token);
    return std::make_unique<EmbedNode>(EmbedNode::CMD_ARG, std::move(node));
  }
  case TokenKind::APPLIED_NAME_WITH_PAREN: { // $func(
    Token token = this->curToken;
    this->consume(); // always success
    auto varNode = this->newVarNode(token.slice(0, token.size - 1));

    auto argsNode = TRY(this->parse_arguments(token.sliceFrom(token.size - 1)));
    auto node =
        std::make_unique<ApplyNode>(std::move(varNode), std::move(argsNode), ApplyNode::FUNC_CALL);
    return std::make_unique<EmbedNode>(EmbedNode::CMD_ARG, std::move(node));
  }
  default:
    return this->parse_interpolation(EmbedNode::CMD_ARG);
  }
}

std::unique_ptr<Node> Parser::parse_cmdSubstitution(bool strExpr) {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::START_SUB_CMD);
  auto ctx = this->inSkippableNLCtx();
  unsigned int pos = START_POS();
  this->consume(); // START_SUB_CMD
  auto exprNode = TRY(this->parse_expression());
  Token token = TRY(this->expect(TokenKind::RP));
  return ForkNode::newCmdSubstitution(pos, std::move(exprNode), token, strExpr);
}

std::unique_ptr<Node> Parser::parse_procSubstitution() {
  GUARD_DEEP_NESTING(guard);

  assert(CUR_KIND() == TokenKind::START_IN_SUB || CUR_KIND() == TokenKind::START_OUT_SUB);
  auto ctx = this->inSkippableNLCtx();
  unsigned int pos = START_POS();
  bool inPipe = this->scan() == TokenKind::START_IN_SUB;
  auto exprNode = TRY(this->parse_expression());
  Token token = TRY(this->expect(TokenKind::RP));
  return ForkNode::newProcSubstitution(pos, std::move(exprNode), token, inPipe);
}

std::unique_ptr<PrefixAssignNode> Parser::parse_prefixAssign() {
  GUARD_DEEP_NESTING(guard);

  bool comp = false;
  std::vector<std::unique_ptr<AssignNode>> envDeclNodes;
  do {
    Token token = TRY(this->expect(TokenKind::ENV_ASSIGN));
    auto nameNode = ({
      std::string envName;
      auto nameToken = token.slice(0, token.size - 1);
      if (!this->lexer->toEnvName(nameToken, envName)) {
        this->reportTokenFormatError(TokenKind::ENV_ASSIGN, nameToken, "must be identifier");
        return nullptr;
      }
      std::make_unique<VarNode>(nameToken, std::move(envName));
    });

    std::unique_ptr<Node> valueNode;
    if (!this->hasSpace() && !this->hasLineTerminator() && lookahead_cmdArg_LP(CUR_KIND())) {
      valueNode = this->parse_cmdArg(CmdArgParseOpt::ASSIGN);
      if (this->incompleteNode) {
        comp = true;
        valueNode = std::move(this->incompleteNode);
      } else if (this->hasError()) {
        return nullptr;
      }
    } else {
      valueNode = std::make_unique<CmdArgNode>(std::make_unique<StringNode>(""));
    }

    auto declNode = std::make_unique<AssignNode>(std::move(nameNode), std::move(valueNode));
    envDeclNodes.push_back(std::move(declNode));

    this->changeLexerModeToSTMT();
  } while (CUR_KIND() == TokenKind::ENV_ASSIGN && !comp);

  std::unique_ptr<Node> exprNode;
  if (comp) {
    exprNode = std::make_unique<EmptyNode>(); // dummy
  } else if (!this->hasLineTerminator() && lookahead_expression(CUR_KIND())) {
    exprNode = this->parse_expression(getPrecedence(TokenKind::WITH));
    if (this->incompleteNode) {
      comp = true;
      exprNode = std::move(this->incompleteNode);
    } else if (this->hasError()) {
      return nullptr;
    }
  }
  auto node = std::make_unique<PrefixAssignNode>(std::move(envDeclNodes), std::move(exprNode));
  if (comp) {
    this->incompleteNode = std::move(node);
  }
  return node;
}

std::unique_ptr<Node> Parser::parse_cmdArgArray() {
  GUARD_DEEP_NESTING(guard);

  auto ctx = this->inSkippableNLCtx();
  Token token = TRY(this->expect(TokenKind::AT_PAREN));
  auto node = std::make_unique<ArgArrayNode>(token);
  while (true) {
    if (lookahead_cmdArg_LP(CUR_KIND())) {
      node->addCmdArgNode(TRY(this->parse_cmdArg()));
    } else if (CUR_KIND() != TokenKind::RP) {
      E_DETAILED(ParseErrorKind::CMD_ARG, EACH_LA_cmdArgs(GEN_LA_ALTER));
    } else {
      break;
    }
  }
  token = TRY(this->expect(TokenKind::RP));
  node->updateToken(token);
  return node;
}

} // namespace ydsh
