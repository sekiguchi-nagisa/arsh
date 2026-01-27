/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef ARSH_PARSER_H
#define ARSH_PARSER_H

#include <memory>
#include <utility>

#include "lexer.h"
#include "misc/enum_util.hpp"
#include "misc/parser_base.hpp"
#include "node.h"

namespace arsh {

enum class ParserOption : unsigned char {
  SINGLE_EXPR = 1u << 0u,
  NEED_HERE_END = 1u << 1u,     // for line continuation checking
  COLLECT_SIGNATURE = 1u << 2u, // for signature help
  IMPLICIT_BLOCK = 1u << 3u,
  ERROR_RECOVER = 1u << 4u, // ignore the error and continue parsing
};

template <>
struct allow_enum_bitop<ParserOption> : std::true_type {};

enum class CmdArgParseOpt : unsigned char {
  ARG,
  MODULE,
  ASSIGN,
  REDIR,
  HERE_START,
};

enum class StmtParseOpt : unsigned char {
  IGNORE_RBC,    // the statement may end with '}'
  DISALLOW_RBC,  // the statement must not end with '}'
  ONLY_LINE_END, // the statement must end with ';'
};

using ParseError = ParseErrorBase<TokenKind>;

struct TokenTracker {
  virtual ~TokenTracker() = default;

  virtual void operator()(TokenKind kind, Token token) = 0;
};

class CodeCompletionContext;

#define EACH_PARSE_ERROR_KIND(OP)                                                                  \
  OP(EXPR, "expression")                                                                           \
  OP(EXPR_RP, "expression or `)'")                                                                 \
  OP(EXPR_END, "expression or `;'")                                                                \
  OP(TYPE, "typename")                                                                             \
  OP(STMT, "statement")                                                                            \
  OP(CMD_ARG, "command argument")                                                                  \
  OP(REDIR, "io redirection target")                                                               \
  OP(MOD_PATH, "module path")                                                                      \
  OP(HERE_END, "here doc end word")

class Parser : public ParserBase<TokenKind, Lexer, TokenTracker> {
private:
  using parse_base_type = ParserBase<TokenKind, Lexer, TokenTracker>;

  static constexpr unsigned int MAX_NESTING_DEPTH = 1024;

  enum class ParseErrorKind : unsigned char {
#define GEN_ENUM(E, S) E,
    EACH_PARSE_ERROR_KIND(GEN_ENUM)
#undef GEN_ENUM
  };

  static constexpr const char *REDIR_NEED_SPACE = "RedirNeedSpace";
  static constexpr const char *INVALID_HERE_START = "InvalidHereStart";
  static constexpr const char *HERE_START_NEED_SPACE = "HereStartNeedSpace";
  static constexpr const char *START_INTERP_NUM_NEED_SPACE = "InterpNumNeedSpace";
  static constexpr const char *START_INTERP_CMD_NEED_SPACE = "InterpCmdNeedSpace";

  ObserverPtr<CodeCompletionContext> compCtx;

  std::unique_ptr<Node> incompleteNode; // for code completion

  bool inStmtCompCtx{false};

  const ParserOption option;

  std::vector<bool> ignorableNewlines; // if true, newline is ignorable

  std::vector<ObserverPtr<RedirNode>> hereDocNodes;

  std::vector<std::unique_ptr<ParseError>> oldErrors;

  struct HereOp {
    TokenKind kind;
    unsigned int pos;
  } hereOp{{}, 0};

  enum class ExpansionLoc : unsigned char { // for ${, $(
    NONE,
    CMD_ARG,
    STRING,
    HERE_BODY,
  };

public:
  explicit Parser(Lexer &lexer, ParserOption option = {},
                  ObserverPtr<CodeCompletionContext> compCtx = nullptr);

  ~Parser() = default;

  std::vector<std::unique_ptr<Node>> operator()();

  explicit operator bool() const { return this->curKind != TokenKind::EOS; }

  Token getCurToken() const { return this->curToken; }

  const auto &getOldErrors() const { return this->oldErrors; }

  auto takeOldErrors() && { return std::move(this->oldErrors); }

protected:
  /**
   * change lexer mode and re-fetch.
   */
  void refetch(LexerCond cond);

  void pushLexerMode(LexerMode mode) {
    this->lexer->pushLexerMode(mode);
    this->fetchNext();
  }

  void popLexerMode();

  /**
   * try to change lexer mode to STMT mode and re-fetch token.
   */
  void changeLexerModeToSTMT();

  /**
   * after called, hasError() will false except for completion
   * @return
   * if successfully clear error, return true
   */
  bool saveAndClearError();

  Token expect(TokenKind kind, bool fetchNext = true);

  /**
   * after matching token, change lexer mode and fetchNext.
   */
  Token expectAndChangeMode(TokenKind kind, LexerCond cond, bool fetchNext = true);

  [[nodiscard]] auto inIgnorableNLCtx(bool skip = true) {
    this->ignorableNewlines.push_back(skip);
    return finally([&] { this->ignorableNewlines.pop_back(); });
  }

  bool hasSpace() const { return this->lexer->isPrevSpace(); }

  bool hasNewline() const { return this->lexer->isPrevNewLine(); }

  bool hasLineTerminator() const { return this->hasNewline() && !this->ignorableNewlines.back(); }

  bool inHereDocBody() const {
    switch (this->curKind) {
    case TokenKind::HERE_END:
    case TokenKind::HERE_START_INTERP:
    case TokenKind::HERE_START_SUB_CMD:
      return true;
    case TokenKind::RP:
    case TokenKind::RBC:
      return false;
    default:
      return this->lexer->getLexerMode().isHereDoc();
    }
  }

  bool inCompletionPoint() const { return this->curKind == TokenKind::COMPLETION; }

  bool inCompletionPointAt(TokenKind kind) const {
    if (this->inCompletionPoint()) {
      auto compTokenKind = this->lexer->getCompTokenKind();
      return compTokenKind == kind || compTokenKind == TokenKind::EOS;
    }
    return false;
  }

  template <unsigned int N>
  bool tryCompleteInfixKeywords(const TokenKind (&kinds)[N]) {
    return this->tryCompleteInfixKeywords(N, kinds);
  }

  bool tryCompleteInfixKeywords(unsigned int size, const TokenKind *kinds);

  bool inVarNameCompletionPoint() const;

  bool inTypeNameCompletionPoint() const;

  void resolveFileNameCompletionTarget(const CmdArgNode &cmdArgNode, CmdArgParseOpt opt);

  template <typename... Args>
  void makeCodeComp(Args &&...args) {
    this->incompleteNode = std::make_unique<CodeCompNode>(std::forward<Args>(args)...);
  }

  /**
   * helper function for VarNode creation.
   * @param token
   * @return
   */
  std::unique_ptr<VarNode> newVarNode(Token token) const {
    return std::make_unique<VarNode>(token, this->lexer->toName(token));
  }

  template <typename Func>
  NameInfo expectName(TokenKind kind, Func func) {
    auto actual = this->curKind;
    auto token = this->expect(kind);
    std::string name;
    if (actual == kind) {
      name = (this->lexer->*func)(token);
    }
    return {token, std::move(name)};
  }

  template <unsigned int N>
  bool reportNoViableAlterError(const TokenKind (&alters)[N], bool allowComp) {
    return this->reportNoViableAlterError(N, alters, allowComp);
  }

  /**
   *
   * @param size
   * @param alters
   * @param allowComp
   * @return if successfully clear error, return true
   */
  bool reportNoViableAlterError(unsigned int size, const TokenKind *alters, bool allowComp);

  template <unsigned int N>
  bool reportDetailedError(ParseErrorKind kind, const TokenKind (&alters)[N]) {
    return this->reportDetailedError(kind, N, alters, nullptr);
  }

  /**
   *
   * @param kind
   * @param size
   * @param alters
   * @param messageSuffix
   * @return if successfully clear error, return true
   */
  bool reportDetailedError(ParseErrorKind kind, unsigned int size, const TokenKind *alters,
                           const char *messageSuffix);

  bool reportHereDocStartError(TokenKind kind, Token token) {
    this->createError(kind, token, INVALID_HERE_START,
                      "heredoc start word must follow `[a-zA-Z0-9_-]+' or "
                      "`['][a-zA-Z0-9_-]+[']' format");
    return this->saveAndClearError();
  }

  std::unique_ptr<Node> recoverAndSkipUntilSyncPoint(StmtParseOpt opt = StmtParseOpt::IGNORE_RBC);

  std::unique_ptr<Node> toAccessNode(Token token) const;

  const auto &getActiveHereDocState() const {
    unsigned int depth = this->lexer->hereDocStateDepth();
    if (this->curKind == TokenKind::HERE_START_SUB_CMD ||
        this->curKind == TokenKind::HERE_START_INTERP) {
      depth--;
    }
    assert(depth);
    return this->lexer->getHereDocStateAt(depth - 1);
  }

  /**
   * look up here doc node specified by pos
   * @param pos
   * @return
   * if not found, return hereDocNodes.size()
   */
  size_t findHereDocNodeIndex(unsigned int pos) const;

  // parser rule definition.
  std::unique_ptr<FunctionNode> parse_function(bool needBody = true);

  /**
   * not call it directory
   */
  std::unique_ptr<TypeNode> parse_basicOrReifiedType(Token token);

  std::unique_ptr<TypeNode> parse_typeNameImpl();

  /**
   * parse type
   * @param enterTYPEMode
   * if true, push lexer mode 'TYPE' and fetch next. after parsing, pop lexer mode and refetch
   * @return
   */
  std::unique_ptr<TypeNode> parse_typeName(bool enterTYPEMode = true);

  /**
   * not call it directory
   * @return
   */
  std::unique_ptr<Node> parse_statementImpl();

  std::unique_ptr<Node> parse_statement(StmtParseOpt opt = StmtParseOpt::IGNORE_RBC);

  /**
   *
   * @param opt
   * @return
   * always nullptr
   */
  std::unique_ptr<Node> parse_statementEnd(StmtParseOpt opt = StmtParseOpt::IGNORE_RBC);

  std::unique_ptr<Node> parse_typedef();

  std::unique_ptr<BlockNode> parse_block();

  std::unique_ptr<Node> parse_variableDeclaration();

  std::unique_ptr<Node> parse_ifExpression(bool asElif = false);

  std::unique_ptr<Node> parse_caseExpression();

  std::unique_ptr<ArmNode> parse_armExpression();

  std::unique_ptr<Node> parse_forExpression();

  std::unique_ptr<Node> parse_forCond();

  std::unique_ptr<Node> parse_forIter();

  std::unique_ptr<CatchNode> parse_catchBlock();

  std::unique_ptr<Node> parse_command(bool allowEmptyCmd);

  std::unique_ptr<RedirNode> parse_redirOption();

  /**
   *
   * @return
   * return always null
   */
  std::unique_ptr<Node> parse_hereDocBody();

  std::unique_ptr<CmdArgNode> parse_cmdArg(CmdArgParseOpt opt = CmdArgParseOpt::ARG);

  /**
   * parse and add command argument segment
   * @param argNode
   * @param opt
   * @return
   * return always null
   */
  std::unique_ptr<Node> parse_cmdArgSeg(CmdArgNode &argNode, CmdArgParseOpt opt);

  /**
   *
   * @param argNode for file name completion
   * @param opt
   * @return
   */
  std::unique_ptr<Node> parse_cmdArgSegImpl(const CmdArgNode &argNode, CmdArgParseOpt opt);

  std::unique_ptr<Node> parse_expressionImpl(OperatorPrecedence basePrecedence, bool allowEmptyCmd);

  std::unique_ptr<Node> parse_expression(OperatorPrecedence basePrecedence,
                                         bool allowEmptyCmd = false);

  std::unique_ptr<Node> parse_expression(bool allowEmptyCmd = false) {
    return this->parse_expression(getPrecedence(TokenKind::ASSIGN), allowEmptyCmd);
  }

  std::unique_ptr<Node> parse_unaryExpression(bool allowEmptyCmd = false);

  std::unique_ptr<Node> parse_suffixExpression(bool allowEmptyCmd = false);

  std::unique_ptr<Node> parse_primaryExpression(bool allowEmptyCmd = false);

  std::unique_ptr<Node> parse_arrayBody(Token token, std::unique_ptr<Node> &&firstNode);

  std::unique_ptr<Node> parse_mapBody(Token token, std::unique_ptr<Node> &&keyNode);

  std::unique_ptr<Node> parse_appliedName(bool asSpecialName = false);

  std::unique_ptr<Node> parse_stringLiteral(bool asHereStart = false);

  std::unique_ptr<Node> parse_regexLiteral();

  std::unique_ptr<Node> parse_backquoteLiteral();

  /**
   * parse argument list (1, 2, 4,)
   * @param first
   * if first.size is 0, expect '('
   * @return
   */
  std::unique_ptr<ArgsNode> parse_arguments(Token first = {0, 0});

  std::unique_ptr<Node> parse_stringExpression();

  std::unique_ptr<Node> parse_interpolation(ExpansionLoc loc);

  std::unique_ptr<Node> parse_paramExpansion();

  std::unique_ptr<Node> parse_cmdSubstitution(ExpansionLoc loc);

  std::unique_ptr<Node> parse_procSubstitution();

  std::unique_ptr<PrefixAssignNode> parse_prefixAssign();

  std::unique_ptr<Node> parse_cmdArgArray();

  std::unique_ptr<Node> parse_attributes();
};

} // namespace arsh

#endif // ARSH_PARSER_H
