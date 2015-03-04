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

#ifndef PARSER_PARSERCONTEXT_H_
#define PARSER_PARSERCONTEXT_H_

#include <parser/Lexer.h>
#include <ast/Node.h>
#include <ast/TypeToken.h>

typedef enum {
    TOKEN_T,
    TYPE_TOKEN_T,
    NODE_T,
    ROOT_NODE_T
} STYPE_KIND;

typedef union {
    Token token;
    TypeToken *tyoeToken;
    Node *node;
    RootNode *rootNode;
} YYSTYPE;

class ParserContext {
private:
    RootNode *rootNode;

public:
    ParserContext();
    ~ParserContext();

    void setRootNode(RootNode *rootNode);
    RootNode *getRootNode();

    /**
     * get rootNode and assign null to this->rootNode
     */
    RootNode *removeRootNode();
};

class Parser {
private:
    void *parserImpl;

public:
    Parser();
    ~Parser();

    RootNode *parse(Lexer &lexer);
};




#endif /* PARSER_PARSERCONTEXT_H_ */
