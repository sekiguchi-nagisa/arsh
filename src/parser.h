/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include "misc/parser_base.hpp"
#include "lexer.h"
#include "node.h"

namespace ydsh {

class ArgsWrapper {
private:
    Token token;
    std::vector<Node *> nodes;

public:
    NON_COPYABLE(ArgsWrapper);

    explicit ArgsWrapper(unsigned int pos) : token({pos, 1}), nodes() {}
    ArgsWrapper(ArgsWrapper &&a) = default;
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
        this->tokenPairs.push_back(std::make_pair(kind, token));
    }

    const std::vector<std::pair<TokenKind, Token>> &getTokenPairs() const {
        return this->tokenPairs;
    };
};

class Parser : public ydsh::parser_base::ParserBase<TokenKind, Lexer, TokenTracker> {
public:
    Parser() = default;

    ~Parser() = default;

    /**
     * parse entry point.
     * write parsed nodes to rootNode.
     */
    void parse(Lexer &lexer, RootNode &rootNode);

private:
    /**
     * change lexer mode and refetch.
     */
    void refetch(LexerMode mode);

    void restoreLexerState(Token prevToken);

    /**
     * after matching token, change lexer mode and fetchNext.
     */
    void expectAndChangeMode(TokenKind kind, LexerMode mode);

    // parser rule definition.
    void parse_toplevel(RootNode &rootNode);

    std::unique_ptr<Node> parse_function();
    std::unique_ptr<FunctionNode> parse_funcDecl();
    std::unique_ptr<Node> parse_interface();
    std::unique_ptr<Node> parse_typeAlias();

    /**
     * not call it directory
     */
    std::unique_ptr<TypeNode> parse_basicOrReifiedType(Token token);

    /**
     * not call NEXT_TOKEN, before call it.
     */
    std::unique_ptr<TypeNode> parse_typeName();

    std::unique_ptr<Node> parse_statement();

    void parse_statementEnd();

    std::unique_ptr<BlockNode> parse_block();

    std::unique_ptr<Node> parse_variableDeclaration();

    std::unique_ptr<Node> parse_ifStatement(bool asElif = false);

    std::unique_ptr<Node> parse_forStatement();

    std::unique_ptr<Node> parse_forInit();

    std::unique_ptr<Node> parse_forCond();

    std::unique_ptr<Node> parse_forIter();

    std::unique_ptr<CatchNode> parse_catchStatement();

    std::unique_ptr<Node> parse_pipedCommand();

    std::unique_ptr<Node> parse_command();

    void parse_redirOption(std::unique_ptr<CmdNode> &node);

    std::unique_ptr<CmdArgNode> parse_cmdArg();

    std::unique_ptr<Node> parse_cmdArgSeg(bool expandTilde = false);

    std::unique_ptr<Node> parse_assignmentExpression();

    std::unique_ptr<Node> parse_expression();

    std::unique_ptr<Node> parse_binaryExpression(std::unique_ptr<Node> &&leftNode,
                                                 unsigned int basePrecedence);

    std::unique_ptr<Node> parse_unaryExpression();

    std::unique_ptr<Node> parse_memberExpression();

    std::unique_ptr<Node> parse_primaryExpression();

    std::unique_ptr<Node> parse_appliedName(bool asSpecialName = false);

    std::unique_ptr<Node> parse_stringLiteral();

    ArgsWrapper parse_arguments();

    std::unique_ptr<Node> parse_stringExpression();

    std::unique_ptr<Node> parse_interpolation();

    std::unique_ptr<Node> parse_paramExpansion();

    std::unique_ptr<SubstitutionNode> parse_substitution();
};

// for DBus interface loading

/**
 * if parse error happened, return false.
 */
bool parse(const char *sourceName, RootNode &rootNode);

} // namespace ydsh

#endif //YDSH_PARSER_H
