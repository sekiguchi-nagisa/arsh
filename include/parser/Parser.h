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

class Parser {
private:
    /**
     * not call destructor.
     */
    Lexer *lexer;

    /**
     * current token kind. updated by Lexer.
     */
    TokenKind curTokenKind;

    /**
     * current token. updated by Lexer.
     */
    Token  curToken;

public:
    Parser(Lexer *lexer);
    ~Parser();

    void setLexer(Lexer *lexer);

    // entry point.
    RootNode *parse();

private:
    /**
     * match curToken and expected token.
     * if failed, throw exception.
     */
    void matchToken(TokenKind expected);

    /**
     * match curToken and expected token.
     * if success, return matched token, otherwise throw exception.
     */
    Token matchAndGetToken(TokenKind expected);

    /**
     * if has new line, throw exception.
     */
    void hasNoNewLine();

    // parser rule definition.
    std::unique_ptr<RootNode> parse_toplevel();
    std::unique_ptr<Node> parse_toplevelStatement();
    std::unique_ptr<Node> parse_statement();
    std::unique_ptr<Node> parse_expression();
    std::string parse_name();
    std::unique_ptr<Node> parse_variableDeclaration();
    std::unique_ptr<Node> parse_rightHandleSide();
};

#endif /* PARSER_PARSER_H_ */
