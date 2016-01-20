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

#ifndef YDSH_PARSER_PARSER_H
#define YDSH_PARSER_PARSER_H

#include <memory>
#include <utility>

#include "parser_base.hpp"
#include "lexer.h"
#include "../ast/node.h"

namespace ydsh {
namespace parser {

using namespace ydsh::ast;

class ArgsWrapper {
private:
    std::vector<Node *> nodes;

public:
    ArgsWrapper() = default;
    ArgsWrapper(const ArgsWrapper &a) = delete;
    ArgsWrapper(ArgsWrapper &&a) = default;
    ~ArgsWrapper();

    ArgsWrapper &operator=(const ArgsWrapper &) = delete;

    void addArgNode(std::unique_ptr<Node> &&node);
    std::vector<Node *> remove();
};

using ParseError = ydsh::parser_base::ParseError<TokenKind>;

class Parser : public ydsh::parser_base::ParserBase<TokenKind, Lexer> {
public:
    Parser() = default;

    ~Parser() = default;

    /**
     * parse entry point.
     * write parsed nodes to rootNode.
     */
    void parse(Lexer &lexer, RootNode &rootNode);

private:
    // parser rule definition.
    void parse_toplevel(RootNode &rootNode);

    /**
     * change lexer mode and refetch.
     */
    void refetch(LexerMode mode);

    /**
     * after matching token, change lexer mode and fetchNext.
     */
    void expectAfter(TokenKind kind, LexerMode mode);

    std::unique_ptr<Node> parse_function();
    std::unique_ptr<FunctionNode> parse_funcDecl();
    std::unique_ptr<Node> parse_interface();
    std::unique_ptr<Node> parse_typeAlias();

    void restoreLexerState(Token prevToken);

    /**
     * not call it directory
     */
    std::unique_ptr<TypeNode> parse_basicOrReifiedType(Token token);

    /**
     * not call NETX_TOKEN, before call it.
     */
    std::unique_ptr<TypeNode> parse_typeName();

    std::unique_ptr<Node> parse_statement();

    void parse_statementEnd();

    std::unique_ptr<BlockNode> parse_block();

    std::unique_ptr<Node> parse_variableDeclaration();

    std::unique_ptr<Node> parse_forStatement();

    std::unique_ptr<Node> parse_forInit();

    std::unique_ptr<Node> parse_forCond();

    std::unique_ptr<Node> parse_forIter();

    std::unique_ptr<CatchNode> parse_catchStatement();

    std::unique_ptr<Node> parse_commandListExpression();

    std::unique_ptr<Node> parse_orListCommand();

    std::unique_ptr<Node> parse_andListCommand();

    std::unique_ptr<Node> parse_pipedCommand();

    std::unique_ptr<Node> parse_command();

    void parse_redirOption(std::unique_ptr<CmdNode> &node);

    std::unique_ptr<CmdArgNode> parse_cmdArg();

    std::unique_ptr<Node> parse_cmdArgSeg(bool expandTilde = false);

    std::unique_ptr<Node> parse_commandOrExpression();

    std::unique_ptr<Node> parse_expression();

    std::unique_ptr<Node> parse_expression(std::unique_ptr<Node> &&leftNode,
                                           unsigned int basePrecedence);

    std::unique_ptr<Node> parse_unaryExpression();

    std::unique_ptr<Node> parse_suffixExpression();

    std::unique_ptr<Node> parse_memberExpression();

    std::unique_ptr<Node> parse_primaryExpression();

    std::unique_ptr<Node> parse_appliedName(bool asSpecialName = false);

    std::unique_ptr<Node> parse_stringLiteral();

    ArgsWrapper parse_arguments();

    std::unique_ptr<Node> parse_stringExpression();

    std::unique_ptr<Node> parse_interpolation();

    std::unique_ptr<Node> parse_paramExpansion();

    std::unique_ptr<Node> parse_commandSubstitution();
};

// for DBus interface loading

/**
 * if parse error happened, return false.
 */
bool parse(const char *sourceName, RootNode &rootNode);


} // namespace parser
} // namespace ydsh

#endif //YDSH_PARSER_PARSER_H
