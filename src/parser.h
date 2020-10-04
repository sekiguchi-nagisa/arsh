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
#include <utility>
#include <tuple>

#include "misc/parser_base.hpp"
#include "misc/enum_util.hpp"
#include "lexer.h"
#include "node.h"

namespace ydsh {

class ArgsWrapper {
private:
    Token token;
    std::vector<std::unique_ptr<Node>> nodes;

public:
    NON_COPYABLE(ArgsWrapper);

    explicit ArgsWrapper(unsigned int pos) : token({pos, 1}) {}
    ArgsWrapper(ArgsWrapper &&) = default;
    ArgsWrapper(std::nullptr_t) : ArgsWrapper(-1) {}    //NOLINT

    Token getToken() const {
        return this->token;
    }

    void updateToken(Token other) {
        this->token.size = other.pos + other.size - this->token.pos;
    }

    void addArgNode(std::unique_ptr<Node> &&node) {
        this->nodes.push_back(std::move(node));
    }

    std::vector<std::unique_ptr<Node>> take() {
        return std::move(this->nodes);
    }
};

enum class CmdArgParseOpt : unsigned int {
    FIRST  = 1u << 0u,
    MODULE = 1u << 1u,
};

template <> struct allow_enum_bitop<CmdArgParseOpt> : std::true_type {};


using ParseError = ParseErrorBase<TokenKind>;

class TokenTracker {
private:
    std::vector<std::pair<TokenKind, Token>> tokenPairs;

public:
    TokenTracker() = default;
    ~TokenTracker() = default;

    void operator()(TokenKind kind, Token token) {
        this->tokenPairs.emplace_back(kind, token);
    }

    const std::vector<std::pair<TokenKind, Token>> &getTokenPairs() const {
        return this->tokenPairs;
    };
};

class CodeCompletionHandler;

class Parser : public ydsh::ParserBase<TokenKind, Lexer, TokenTracker> {
private:
    using parse_base_type = ydsh::ParserBase<TokenKind, Lexer, TokenTracker>;

#ifdef __SANITIZE_ADDRESS__
    static constexpr unsigned int MAX_NESTING_DEPTH = 2500;
#else
    static constexpr unsigned int MAX_NESTING_DEPTH = 5000;
#endif

    ObserverPtr<CodeCompletionHandler> ccHandler;

public:
    explicit Parser(Lexer &lexer, ObserverPtr<CodeCompletionHandler> handler = nullptr) {
        this->lexer = &lexer;
        this->ccHandler = handler;
        if(this->ccHandler) {
            this->lexer->setComplete(true);
        }
        this->fetchNext();
    }

    ~Parser() = default;

    std::unique_ptr<Node> operator()() {
        return this->parse_statement(false);
    }

    explicit operator bool() const {
        return this->curKind != EOS;
    }

    void restoreLexicalState(Lexer &lexer, TokenKind kind, Token token, TokenKind ckind = {}) {
        this->lexer = &lexer;
        this->curKind = kind;
        this->curToken = token;
        this->consumedKind = ckind;
    }

    std::tuple<TokenKind, Token, TokenKind> saveLexicalState() const {
        return std::make_tuple(this->curKind, this->curToken, this->consumedKind);
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

    void restoreLexerState(Token prevToken);

    /**
     * try to change lexer mode to STMT mode and refetech token.
     */
    void changeLexerModeToSTMT();

    Token expect(TokenKind kind, bool fetchNext = true);

    /**
     * after matching token, change lexer mode and fetchNext.
     */
    Token expectAndChangeMode(TokenKind kind, LexerMode mode, bool fetchNext = true);

    bool inCompletionPoint() const {
        return this->curKind == COMPLETION;
    }

    bool inCompletionPointAt(TokenKind kind) const {
        if(this->inCompletionPoint()) {
            auto compTokenKind  = this->lexer->getCompTokenKind();
            return compTokenKind == kind || compTokenKind == EOS;
        }
        return false;
    }

    bool inVarNameCompletionPoint() const;

    /**
     * helper function for VarNode creation.
     * @param token
     * @return
     */
    std::unique_ptr<VarNode> newVarNode(Token token) const {
        return std::make_unique<VarNode>(token, this->lexer->toName(token));
    }

    std::unique_ptr<Node> toAccessNode(Token token) const;

    // parser rule definition.
    std::unique_ptr<FunctionNode> parse_funcDecl();
    std::unique_ptr<Node> parse_interface();

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
    std::unique_ptr<Node> parse_statementImp();

    std::unique_ptr<Node> parse_statement(bool disallowEOS = true);

    /**
     *
     * @param allowEOS
     * @return
     * always nullptr
     */
    std::unique_ptr<Node> parse_statementEnd(bool disallowEOS = true);

    std::unique_ptr<BlockNode> parse_block();

    std::unique_ptr<Node> parse_variableDeclaration();

    std::unique_ptr<Node> parse_ifExpression(bool asElif = false);

    std::unique_ptr<Node> parse_caseExpression();

    std::unique_ptr<ArmNode> parse_armExpression();

    std::unique_ptr<Node> parse_forExpression();

    std::unique_ptr<Node> parse_forCond();

    std::unique_ptr<Node> parse_forIter();

    std::unique_ptr<CatchNode> parse_catchStatement();

    std::unique_ptr<Node> parse_command();

    std::unique_ptr<RedirNode> parse_redirOption();

    std::unique_ptr<CmdArgNode> parse_cmdArg(CmdArgParseOpt opt = {});

    std::unique_ptr<Node> parse_cmdArgSeg(CmdArgParseOpt opt);

    std::unique_ptr<StringNode> parse_cmdArgPart(CmdArgParseOpt opt);

    std::unique_ptr<Node> parse_expression(unsigned basePrecedence);

    std::unique_ptr<Node> parse_expression() {
        return this->parse_expression(getPrecedence(ASSIGN));
    }

    std::unique_ptr<Node> parse_unaryExpression();

    std::unique_ptr<Node> parse_suffixExpression();

    std::unique_ptr<Node> parse_primaryExpression();

    template <typename Func>
    auto expectNum(TokenKind kind, Func func) {
        auto token = this->expect(kind);
        int status = 0;
        auto out = (this->lexer->*func)(token, status);
        if(status != 0) {
            this->reportTokenFormatError(kind, token, "out of range");
        }
        return std::make_pair(token, out);
    }

    std::unique_ptr<Node> parse_signalLiteral();

    std::unique_ptr<Node> parse_appliedName(bool asSpecialName = false);

    std::unique_ptr<Node> parse_stringLiteral();

    std::unique_ptr<Node> parse_regexLiteral();

    /**
     * parse argument list (1, 2, 4,)
     * @param first
     * if first.size is 0, expect '('
     * @return
     */
    ArgsWrapper parse_arguments(Token first = {0,0});

    std::unique_ptr<Node> parse_stringExpression();

    std::unique_ptr<Node> parse_interpolation(EmbedNode::Kind kind);

    std::unique_ptr<Node> parse_paramExpansion();

    std::unique_ptr<Node> parse_cmdSubstitution(bool strExpr = false);

    std::unique_ptr<Node> parse_procSubstitution();
};

} // namespace ydsh

#endif //YDSH_PARSER_H
