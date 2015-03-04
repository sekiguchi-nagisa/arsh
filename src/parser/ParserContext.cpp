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

#include <parser/ParserContext.h>
#include <stdlib.h>

// ###########################
// ##     ParserContext     ##
// ###########################

ParserContext::ParserContext() :
        rootNode(0) {
}

ParserContext::~ParserContext() {
    delete this->rootNode;
    this->rootNode = 0;
}

void ParserContext::setRootNode(RootNode *rootNode) {
    this->rootNode = rootNode;
}

RootNode *ParserContext::getRootNode() {
    return this->rootNode;
}

RootNode *ParserContext::removeRootNode() {
    RootNode *root = this->rootNode;
    this->rootNode = 0;
    return root;
}

// ####################
// ##     Parser     ##
// ####################

#include "../Parser.c"

Parser::Parser() :
        parserImpl(ParseAlloc(malloc)) {
}

Parser::~Parser() {
    ParseFree(this->parserImpl, free);
    this->parserImpl = 0;
}

RootNode *Parser::parse(Lexer &lexer) {
    ParserContext ctx;
    while(true) {
        YYSTYPE val;
        TokenKind kind = lexer.nextToken(val.token);
        Parse(this->parserImpl, kind, val, &ctx);
        if(kind == EOS) {
            Parse(this->parserImpl, 0, val, &ctx);
            break;
        }
    }
    return ctx.removeRootNode();
}


