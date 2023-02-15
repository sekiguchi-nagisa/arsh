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

#ifndef YDSH_PARSER_H
#define YDSH_PARSER_H

#include <memory>
#include <tuple>
#include <utility>

#include "lexer.h"
#include "misc/enum_util.hpp"
#include "misc/parser_base.hpp"
#include "node.h"

namespace ydsh {

enum class CmdArgParseOpt : unsigned int {
  FIRST = 1u << 0u,
  MODULE = 1u << 1u,
  ASSIGN = 1u << 2u,
  REDIR = 1u << 3u,
};

template <>
struct allow_enum_bitop<CmdArgParseOpt> : std::true_type {};

using ParseError = ParseErrorBase<TokenKind>;

struct TokenTracker {
  virtual ~TokenTracker() = default;

  virtual void operator()(TokenKind kind, Token token) = 0;
};

class CodeCompletionHandler;

#define EACH_PARSE_ERROR_KIND(OP)                                                                  \
  OP(EXPR, "expression")                                                                           \
  OP(EXPR_RP, "expression or `)'")                                                                 \
  OP(EXPR_END, "expression or `;'")                                                                \
  OP(TYPE, "typename")                                                                             \
  OP(STMT, "statement")                                                                            \
  OP(CMD_ARG, "command argument")                                                                  \
  OP(REDIR, "io redirection target")                                                               \
  OP(MOD_PATH, "module path")

class Parser : public ydsh::ParserBase<TokenKind, Lexer, TokenTracker> {
private:
  using parse_base_type = ydsh::ParserBase<TokenKind, Lexer, TokenTracker>;

  static constexpr unsigned int MAX_NESTING_DEPTH = 1024;

  enum class ParseErrorKind {
#define GEN_ENUM(E, S) E,
    EACH_PARSE_ERROR_KIND(GEN_ENUM)
#undef GEN_ENUM
  };

  static constexpr const char *REDIR_NEED_SPACE = "RedirNeedSpace";

  ObserverPtr<CodeCompletionHandler> ccHandler;

  std::unique_ptr<Node> incompleteNode; // for code completion

  bool inStmtCompCtx{false};

  const bool singleExpr;

  std::vector<bool> skippableNewlines; // if true, newline is skippable

public:
  explicit Parser(Lexer &lexer, bool singleExpr = false,
                  ObserverPtr<CodeCompletionHandler> handler = nullptr);

  ~Parser() = default;

  std::vector<std::unique_ptr<Node>> operator()();

  explicit operator bool() const { return this->curKind != TokenKind::EOS; }

  bool hasError() const { return ParserBase::hasError(); }

  void forceTerminate() { // FIXME: temporal api, must be implement proper error recovery
    this->clear();
    this->curKind = TokenKind::EOS;
  }

protected:
  /**
   * change lexer mode and refetch.
   */
  void refetch(LexerMode mode);

  void pushLexerMode(LexerMode mode) {
    this->lexer->pushLexerMode(mode);
    this->fetchNext();
  }

  void popLexerMode();

  /**
   * try to change lexer mode to STMT mode and refetech token.
   */
  void changeLexerModeToSTMT();

  Token expect(TokenKind kind, bool fetchNext = true);

  /**
   * after matching token, change lexer mode and fetchNext.
   */
  Token expectAndChangeMode(TokenKind kind, LexerMode mode, bool fetchNext = true);

  auto inSkippableNLCtx(bool skip = true) {
    this->skippableNewlines.push_back(skip);
    return finally([&] { this->skippableNewlines.pop_back(); });
  }

  bool hasSpace() const { return this->lexer->isPrevSpace(); }

  bool hasNewline() const { return this->lexer->isPrevNewLine(); }

  bool hasLineTerminator() const { return this->hasNewline() && !this->skippableNewlines.back(); }

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

  void tryCompleteFileNames(CmdArgParseOpt opt);

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

  void addCmdArgSeg(CmdArgNode &cmdArgNode, Token token, StringNode::StringKind k) {
    auto node = std::make_unique<StringNode>(token, this->lexer->toCmdArg(token), k);
    cmdArgNode.addSegmentNode(std::move(node));
  }

  template <typename Func>
  NameInfo expectName(TokenKind kind, Func func) {
    auto actual = this->curKind;
    auto token = this->expect(kind);
    std::string name;
    if (actual == kind) {
      name = (this->lexer->*func)(token);
    }
    return NameInfo(token, std::move(name));
  }

  template <unsigned int N>
  void reportNoViableAlterError(const TokenKind (&alters)[N], bool allowComp) {
    this->reportNoViableAlterError(N, alters, allowComp);
  }

  void reportNoViableAlterError(unsigned int size, const TokenKind *alters, bool allowComp);

  template <unsigned int N>
  void reportDetailedError(ParseErrorKind kind, const TokenKind (&alters)[N]) {
    this->reportDetailedError(kind, N, alters);
  }

  void reportDetailedError(ParseErrorKind kind, unsigned int size, const TokenKind *alters);

  std::unique_ptr<Node> toAccessNode(Token token) const;

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

  std::unique_ptr<Node> parse_statement();

  /**
   *
   * @param allowEOS
   * @return
   * always nullptr
   */
  std::unique_ptr<Node> parse_statementEnd();

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

  std::unique_ptr<Node> parse_command();

  std::unique_ptr<RedirNode> parse_redirOption();

  std::unique_ptr<CmdArgNode> parse_cmdArg(CmdArgParseOpt opt = {});

  /**
   * parse and add command argument segment
   * @param argNode
   * @param opt
   * @return
   * return always null
   */
  std::unique_ptr<Node> parse_cmdArgSeg(CmdArgNode &argNode, CmdArgParseOpt opt);

  std::unique_ptr<Node> parse_cmdArgSegImpl(CmdArgParseOpt opt);

  /**
   * parse sequence style brace expansion
   * @param argNode
   * @return
   * if not recognize brace sequence style brace expansion, return false
   */
  bool parse_braceSeq(CmdArgNode &argNode);

  std::unique_ptr<Node> parse_expression(unsigned basePrecedence);

  std::unique_ptr<Node> parse_expression() {
    return this->parse_expression(getPrecedence(TokenKind::ASSIGN));
  }

  std::unique_ptr<Node> parse_unaryExpression();

  std::unique_ptr<Node> parse_suffixExpression();

  std::unique_ptr<Node> parse_primaryExpression();

  std::unique_ptr<Node> parse_arrayBody(Token token, std::unique_ptr<Node> &&firstNode);

  std::unique_ptr<Node> parse_mapBody(Token token, std::unique_ptr<Node> &&keyNode);

  std::unique_ptr<Node> parse_appliedName(bool asSpecialName = false);

  std::unique_ptr<Node> parse_stringLiteral();

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

  std::unique_ptr<Node> parse_interpolation(EmbedNode::Kind kind);

  std::unique_ptr<Node> parse_paramExpansion();

  std::unique_ptr<Node> parse_cmdSubstitution(bool strExpr = false);

  std::unique_ptr<Node> parse_procSubstitution();

  std::unique_ptr<PrefixAssignNode> parse_prefixAssign();

  std::unique_ptr<Node> parse_cmdArgArray();
};

} // namespace ydsh

#endif // YDSH_PARSER_H
