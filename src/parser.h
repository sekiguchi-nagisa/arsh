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
#include "lexer.h"
#include "node.h"

namespace ydsh {

struct TypeWrapper {
    std::unique_ptr<TypeNode> typeNode;
    Token token;

    TypeWrapper(std::unique_ptr<TypeNode> &&typeNode, Token token) :
            typeNode(std::move(typeNode)), token(token) {}

    TypeWrapper(std::nullptr_t) : typeNode(), token() {}
};

class ArgsWrapper {
private:
    Token token;
    std::vector<Node *> nodes;

public:
    NON_COPYABLE(ArgsWrapper);

    explicit ArgsWrapper(unsigned int pos) : token({pos, 1}) {}
    ArgsWrapper(ArgsWrapper &&) = default;
    ArgsWrapper(std::nullptr_t) : ArgsWrapper(-1) {}

    ~ArgsWrapper();

    Token getToken() const {
        return this->token;
    }

    void updateToken(Token token) {
        this->token.size = token.pos + token.size - this->token.pos;
    }

    void addArgNode(std::unique_ptr<Node> &&node);

    static std::vector<Node *> extract(ArgsWrapper &&argsWrapper) {
        return std::move(argsWrapper.nodes);
    }
};

using ParseError = ydsh::parser_base::ParseError<TokenKind>;

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

class Parser : public ydsh::parser_base::AbstractParser<TokenKind, Lexer, TokenTracker> {
private:
    unsigned int callCount{0};

    static constexpr unsigned int MAX_NESTING_DEPTH = 5000;

public:
    explicit Parser(Lexer &lexer) {
        this->lexer = &lexer;
        this->fetchNext();
    }

    ~Parser() = default;

    std::unique_ptr<Node> operator()() {
        return this->parse_statement();
    }

    explicit operator bool() const {
        return this->curKind != EOS;
    }

    void restoreLexicalState(Lexer &lexer, TokenKind kind, Token token, TokenKind ckind) {
        this->lexer = &lexer;
        this->curKind = kind;
        this->curToken = token;
        this->consumedKind = ckind;
    }

    std::tuple<TokenKind, Token, TokenKind> saveLexicalState() const {
        return {this->curKind, this->curToken, this->consumedKind};
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
     * after matching token, change lexer mode and fetchNext.
     */
    Token expectAndChangeMode(TokenKind kind, LexerMode mode, bool fetchNext = true);

    void raiseTokenFormatError(TokenKind kind, Token token, const char *msg);

    void raiseDeepNestingError();

    // parser rule definition.
    std::unique_ptr<FunctionNode> parse_funcDecl();
    std::unique_ptr<Node> parse_interface();

    /**
     * not call it directory
     */
    TypeWrapper parse_basicOrReifiedType(Token token);

    TypeWrapper parse_typeNameImpl();

    /**
     * not call NEXT_TOKEN, before call it.
     */
    std::unique_ptr<TypeNode> parse_typeName();

    /**
     * not call it directory
     * @return
     */
    std::unique_ptr<Node> parse_statementImp();

    std::unique_ptr<Node> parse_statement();

    /**
     *
     * @return
     * always nullptr
     */
    std::unique_ptr<Node> parse_statementEnd();

    std::unique_ptr<BlockNode> parse_block();

    std::unique_ptr<Node> parse_variableDeclaration();

    std::unique_ptr<Node> parse_ifExpression(bool asElif = false);

    std::unique_ptr<Node> parse_forExpression();

    std::unique_ptr<Node> parse_forInit();

    std::unique_ptr<Node> parse_forCond();

    std::unique_ptr<Node> parse_forIter();

    std::unique_ptr<CatchNode> parse_catchStatement();

    std::unique_ptr<Node> parse_command();

    std::unique_ptr<RedirNode> parse_redirOption();

    std::unique_ptr<CmdArgNode> parse_cmdArg();

    std::unique_ptr<Node> parse_cmdArgSeg(bool first);

    std::unique_ptr<StringNode> parse_cmdArgPart(bool first, LexerMode mode = yycCMD);

    std::unique_ptr<Node> parse_expression();

    std::unique_ptr<Node> parse_binaryExpression(std::unique_ptr<Node> &&leftNode,
                                                 unsigned int basePrecedence);

    std::unique_ptr<Node> parse_unaryExpression();

    std::unique_ptr<Node> parse_suffixExpression();

    std::unique_ptr<Node> parse_primaryExpression();

    template <typename Func>
    auto expectNum(TokenKind kind, Func func) ->
    std::pair<Token, decltype((this->lexer->*func)(std::declval<Token>(), std::declval<int &>()))> {
        auto token = this->expect(kind);
        int status = 0;
        auto out = (this->lexer->*func)(token, status);
        if(status != 0) {
            this->raiseTokenFormatError(kind, token, "out of range");
        }
        return std::make_pair(token, out);
    }

    std::unique_ptr<Node> parse_appliedName(bool asSpecialName = false);

    std::unique_ptr<Node> parse_stringLiteral();

    ArgsWrapper parse_arguments();

    std::unique_ptr<Node> parse_stringExpression();

    std::unique_ptr<Node> parse_interpolation();

    std::unique_ptr<Node> parse_paramExpansion();

    std::unique_ptr<Node> parse_substitution(bool strExpr = false);
};

// for DBus interface loading
std::unique_ptr<Node> parse(const char *sourceName);

} // namespace ydsh

#endif //YDSH_PARSER_H
