/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef PARSER_PARSER_H_
#define PARSER_PARSER_H_

#include <memory>
#include <utility>

#include <parser/Lexer.h>
#include <ast/Node.h>
#include <ast/TypeToken.h>

namespace ydsh {
namespace parser {

using namespace ydsh::ast;

class Parser {
private:
    /**
     * not call destructor.
     */
    Lexer<LexerDef, TokenKind> *lexer;

    /**
     * current token kind. updated by Lexer.
     */
    TokenKind curTokenKind;

    /**
     * current token. updated by Lexer.
     */
    Token curToken;

public:
    Parser();

    ~Parser();

    /**
     * parse entry point.
     * write parsed nodes to rootNode.
     */
    void parse(Lexer<LexerDef, TokenKind> &lexer, RootNode &rootNode);

private:
    /**
     * match curToken and expected token.
     * if failed, throw exception.
     * if fetchNext is true, call NEXT_TOKEN()
     */
    void matchToken(TokenKind expected, bool fetchNext = true);

    /**
     * match curToken and expected token.
     * if success, return matched token, otherwise throw exception.
     * if fetchNext is true, call NEXT_TOKEN()
     */
    Token matchAndGetToken(TokenKind expected, bool fetchNext = true);

    /**
     * return curTokenKind and consume.
     */
    TokenKind consumeAndGetKind();

    /**
     * if has new line, throw exception.
     */
    void hasNoNewLine();

    // parser rule definition.
    void parse_toplevel(RootNode &rootNode);

    std::unique_ptr<Node> parse_toplevelStatement();

    std::unique_ptr<Node> parse_function();
    std::unique_ptr<FunctionNode> parse_funcDecl();
    std::unique_ptr<Node> parse_interface();
    std::unique_ptr<Node> parse_typeAlias();

    void restoreLexerState(unsigned int prevLineNum, const Token &prevToken);

    /**
     * not call NETX_TOKEN, before call it.
     */
    std::unique_ptr<TypeToken> parse_typeName();

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

    std::unique_ptr<CmdNode> parse_command();

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

    std::unique_ptr<Node> parse_appliedName();

    std::unique_ptr<Node> parse_specialName();

    std::unique_ptr<Node> parse_stringLiteral();

    std::unique_ptr<ArgsNode> parse_arguments();

    std::unique_ptr<Node> parse_stringExpression();

    std::unique_ptr<Node> parse_interpolation();

    std::unique_ptr<Node> parse_commandSubstitution();
};

} // namespace parser
} // namespace ydsh

#endif /* PARSER_PARSER_H_ */
