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

#include "directive_parser.h"
#include <misc/debug.h>

#include <fstream>
#include <sstream>

namespace ydsh {
namespace directive {


// #############################
// ##     DirectiveParser     ##
// #############################

static bool isDirective(const std::string &line) {
    std::string prefix("#$test");
    if(line.size() < prefix.size()) {
        return false;
    }
    unsigned int size = prefix.size();
    for(unsigned int i = 0; i < size; i++) {
        if(line[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool DirectiveParser::operator()(const char *sourceName, std::istream &input, Directive &d) {
    std::string line;
    unsigned int lineNum = 0;

    while(std::getline(input, line)) {
        lineNum++;

        if(!isDirective(line)) {
            continue;
        }

        // prepare
        const char *src = line.c_str() + 1;
        Lexer lexer(src);
        lexer.setLineNum(lineNum);
        this->lexer = &lexer;
        this->fetchNext();

        try {
            std::unique_ptr<DirectiveNode> node;
            this->parse_toplevel(node);
            DirectiveInitializer()(node, d);
            return true;
        } catch(const ParseError &e) {
            std::cerr << sourceName << ":" << e.getLineNum() << ": [syntax error] ";
            if(dynamic_cast<const TokenMismatchedError *>(&e)) {
                std::cerr << *static_cast<const TokenMismatchedError *>(&e) << std::endl;
            } else if(dynamic_cast<const NoViableAlterError *>(&e)) {
                std::cerr << *static_cast<const NoViableAlterError *>(&e) << std::endl;
            } else if(dynamic_cast<const InvalidTokenError *>(&e)) {
                std::cerr << *static_cast<const InvalidTokenError *>(&e) << std::endl;
            }

            std::cerr << src << std::endl;
            Token lineToken;
            lineToken.startPos = 0;
            lineToken.size = line.size();
            std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
            return false;
        } catch(const SemanticError &e) {
            std::cerr << sourceName << ":" << e.getErrorToken().lineNum << ": [semantic error] ";
            std::cerr << e.getMessage() << std::endl;

            std::cerr << src << std::endl;
            Token lineToken;
            lineToken.startPos = 0;
            lineToken.size = line.size();
            std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
            return false;
        }
    }
    return true;
}

#define CUR_KIND() this->curToken.kind

void DirectiveParser::parse_toplevel(std::unique_ptr<DirectiveNode> &node) {
    Token token;
    this->expect(APPLIED_NAME, token);
    node.reset(new DirectiveNode(token, this->lexer->toName(token)));

    this->expect(LP);

    bool first = true;
    do {
        if(!first) {
            this->expect(COMMA);
        } else {
            first = false;
        }
        std::unique_ptr<AttributeNode> attr;
        this->parse_attribute(attr);
        node->append(std::move(attr));
    } while(CUR_KIND() == COMMA);
}

void DirectiveParser::parse_attribute(std::unique_ptr<AttributeNode> &node) {
    Token token;
    this->expect(APPLIED_NAME, token);

    this->expect(ASSIGN);

    std::unique_ptr<Node> value;
    this->parse_value(value);

    node.reset(new AttributeNode(token, this->lexer->toName(token), std::move(value)));
}

void DirectiveParser::parse_value(std::unique_ptr<Node> &value) {
#define EACH_LA_value(OP) \
    OP(INT_LITERAL) \
    OP(STRING_LITERAL) \
    OP(LB)

#define GEN_LA_ALTER(K) alters.push_back(K);

    switch(CUR_KIND()) {
    case INT_LITERAL:
        this->parse_number(value);
        return;
    case STRING_LITERAL:
        this->parse_string(value);
        return;
    case LB:
        this->parse_array(value);
        return;
    default:
        std::vector<TokenKind> alters;
        EACH_LA_value(GEN_LA_ALTER)
        this->alternativeError(std::move(alters));
        return;
    }


#undef GEN_LA_ALTER
#undef EACH_LA_value
}

void DirectiveParser::parse_number(std::unique_ptr<Node> &node) {
    Token token;
    this->expect(INT_LITERAL, token);
    int status;
    int value = this->lexer->toInt(token, status);
    if(value < 0 || status != 0) {
        std::string str("out of range number: ");
        str += this->lexer->toTokenText(token);
        throw SemanticError(token, std::move(str));
    }
    node.reset(new NumberNode(token, value));
}

void DirectiveParser::parse_string(std::unique_ptr<Node> &node) {
    Token token;
    this->expect(STRING_LITERAL, token);
    node.reset(new StringNode(token, this->lexer->toString(token, true)));
}

void DirectiveParser::parse_array(std::unique_ptr<Node> &node) {
    Token token;
    this->expect(LB, token);
    std::unique_ptr<ArrayNode> arrayNode(new ArrayNode(token));
    std::unique_ptr<Node> value;
    this->parse_value(value);
    arrayNode->appendNode(std::move(value));

    while(CUR_KIND() == COMMA) {
        this->expect(COMMA);

        this->parse_value(value);
        arrayNode->appendNode(std::move(value));
    }
    this->expect(RB);
    node = std::move(arrayNode);
}

// ##################################
// ##     DirectiveInitializer     ##
// ##################################

bool DirectiveInitializer::operator()(const std::unique_ptr<DirectiveNode> &node, Directive &d) {
    this->directive = &d;

    node->accept(*this);
    return true;
}

void DirectiveInitializer::visitDirectiveNode(DirectiveNode &node) {
    // check name
    if(node.getName() != "test") {
        std::string str("unsupported directive: ");
        str += node.getName();
        throw SemanticError(node.getToken(), std::move(str));
    }

    for(auto &e : node.getNodes()) {
        e->accept(*this);
    }
}

void DirectiveInitializer::visitAttributeNode(AttributeNode &node) {
    this->token = &node.getToken();
    this->name = &node.getName();

    node.getAttrNode()->accept(*this);
}

void DirectiveInitializer::visitNumberNode(NumberNode &node) {
    if(*this->name == "status") {
        this->directive->setStatus(node.getValue());
        return;
    }

    this->raiseAttributeError();
}

void DirectiveInitializer::visitStringNode(StringNode &node) {
    if(*this->name == "result") {
        ExecStatus status = this->resolveStatus(node);
        this->directive->setResult(status);
        return;
    }
    if(*this->name == "params" && this->inArray) {
        this->directive->appendParam(std::string(node.getValue()));
        return;
    }

    this->raiseAttributeError();
}

void DirectiveInitializer::visitArrayNode(ArrayNode &node) {
    if(*this->name == "params") {
        for(auto &e : node.getValues()) {
            if(!isType<StringNode>(e)) {
                throw SemanticError(e->getToken(), "must be string");
            }
            this->inArray = true;
            e->accept(*this);
            this->inArray = false;
        }
        return;
    }

    this->raiseAttributeError();
}

ExecStatus DirectiveInitializer::resolveStatus(const StringNode &node) {
#define EACH_STATUS(OP) \
    OP("SUCCESS", SUCCESS) \
    OP("success", SUCCESS) \
    OP("PARSE_ERROR", PARSE_ERROR) \
    OP("parse", PARSE_ERROR) \
    OP("TYPE_ERROR", TYPE_ERROR) \
    OP("type", TYPE_ERROR) \
    OP("RUNTIME_ERROR", RUNTIME_ERROR) \
    OP("runtime", RUNTIME_ERROR) \
    OP("THROW", RUNTIME_ERROR) \
    OP("throw", RUNTIME_ERROR) \
    OP("ASSERTION_ERROR", ASSERTION_ERROR) \
    OP("ASSERT", ASSERTION_ERROR) \
    OP("assert", ASSERTION_ERROR) \
    OP("exit", EXIT) \
    OP("EXIT", EXIT)

#define MATCH(STR, STATUS) if(node.getValue() == STR) { return ExecStatus::STATUS; }

    EACH_STATUS(MATCH)

#define ALTER(STR, STATUS) alters.push_back(std::string(STR));

    std::vector<std::string> alters;
    EACH_STATUS(ALTER)

    std::string message("illegal status, expect for ");
    unsigned int count = 0;
    for(auto &e : alters) {
        if(count++ > 0) {
            message += ", ";
        }
        message += e;
    }
    throw SemanticError(node.getToken(), std::move(message));

#undef ALTER
#undef MATCH
#undef EACH_STATUS
}

void DirectiveInitializer::raiseAttributeError() {
    std::string str("unsupported attribute: ");
    str += *this->name;
    throw SemanticError(*this->token, std::move(str));
}


// #######################
// ##     Directive     ##
// #######################

bool Directive::init(const char *fileName, Directive &d) {
    std::ifstream input(fileName);
    if(!input) {
        fatal("cannot open file: %s\n", fileName);
    }
    return DirectiveParser()(fileName, input, d);
}

bool Directive::init(const char *sourceName, const char *src, Directive &d) {
    std::istringstream input(src);
    return DirectiveParser()(sourceName, input, d);
}

} // namespace directive
} // namespace ydsh
