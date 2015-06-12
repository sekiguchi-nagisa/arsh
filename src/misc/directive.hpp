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

#ifndef YDSH_DIRECTIVE_HPP
#define YDSH_DIRECTIVE_HPP

#include "../parser/Lexer.h"
#include "../parser/TokenKind.h"
#include "debug.h"
#include <ydsh/ydsh.h>
#include <memory>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ydsh {
namespace directive {

class Directive {
private:
    ydsh::ExecStatus result;
    std::vector<std::string> params;

    /**
     * for command exit status
     */
    unsigned int status;

public:
    Directive() : result(ExecStatus::SUCCESS), params(), status(0) {}
    ~Directive() = default;

    ydsh::ExecStatus getResult() const {
        return this->result;
    }

    void setResult(ydsh::ExecStatus status) {
        this->result = status;
    }

    void appendParam(std::string &&param) {
        this->params.push_back(std::move(param));
    }

    const std::vector<std::string> &getParams() const {
        return this->params;
    }

    void setStatus(unsigned int status) {
        this->status = status;
    }

    unsigned int getStatus() const {
        return this->status;
    }
};

namespace __detail_directive {

using namespace ydsh::parser;

class NumberNode;
class StringNode;
class ArrayNode;

struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    virtual void visitNumberNode(NumberNode &node) = 0;
    virtual void visitStringNode(StringNode &node) = 0;
    virtual void visitArrayNode(ArrayNode &node) = 0;
};

struct Node {
    virtual ~Node() = default;

    virtual void accept(NodeVisitor &visitor) = 0;
};

class NumberNode : public Node {
private:
    unsigned int value;

public:
    NumberNode(unsigned int value) : value(value) {}
    ~NumberNode() = default;

    unsigned int getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) { // override
        visitor.visitNumberNode(*this);
    }
};

class StringNode : public Node {
private:
    std::string value;

public:
    StringNode(std::string &&value) : value(std::move(value)) {}
    ~StringNode() = default;

    const std::string &getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) { // override
        return visitor.visitStringNode(*this);
    }
};

class ArrayNode : public Node {
private:
    std::vector<std::unique_ptr<Node>> values;

public:
    ArrayNode() = default;
    ~ArrayNode() = default;

    void appendNode(std::unique_ptr<Node> &&node) {
        this->values.push_back(std::move(node));
    }

    const std::vector<std::unique_ptr<Node>> &getValues() const {
        return this->values;
    }

    void accept(NodeVisitor &visitor) { // override
        return visitor.visitArrayNode(*this);
    }
};

class KeyValue {
private:
    std::string key;
    std::unique_ptr<Node> value;

public:
    KeyValue() = default;
    ~KeyValue() = default;

    void setKey(std::string &&key) {
        this->key = std::move(key);
    }

    const std::string &getKey() {
        return this->key;
    }

    void setValue(std::unique_ptr<Node> &&value) {
        this->value = std::move(value);
    }

    const std::unique_ptr<Node> &getValue() {
        return this->value;
    }
};

class SemanticError {
private:
    std::string message;

public:
    SemanticError(const char *message) : message(message) {}
    SemanticError(std::string &&message) : message(std::move(message)) {}
    ~SemanticError() = default;

    const std::string &getMessage() const {
        return this->message;
    }
};

/**
 * ex. $test($result = "SUCCESS", $args = ['sd', '32'])
 *
 * test directive use subset of ydsh lexical rule.
 */
template <size_t N>
class DirectiveParser : public ydsh::parser_base::ParserBase<TokenKind, Lexer> {
public:
    static_assert(N == 1, "N is always 1");

    DirectiveParser() = default;
    ~DirectiveParser() = default;

    bool operator()(unsigned int lineNum, const char *sourceName,
                    const std::string &line, Directive &directive);

    /**
     * read from file
     */
    static bool parse(const std::string &fileName, Directive &d);

private:
    ExecStatus resolveStatus(const std::string &str);

    void parse_toplevel(Directive &directive);
    void parse_keyvalue(KeyValue &keyValue);
    void parse_key(std::string &key);
    void parse_value(std::unique_ptr<Node> &value);
    void parse_number(std::unique_ptr<Node> &node);
    void parse_string(std::unique_ptr<Node> &node);
    void parse_array(std::unique_ptr<Node> &node);
};

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

template <size_t N>
bool DirectiveParser<N>::operator()(unsigned int lineNum, const char *sourceName,
                                    const std::string &line, Directive &directive) {
    if(!isDirective(line)) {
        return true;    //do nothing
    }

    // prepare
    Lexer lexer(line.c_str() + 1);
    this->lexer = &lexer;
    this->fetchNext();

    // parse
    try {
        this->parse_toplevel(directive);
    } catch(const ParseError &e) {
        std::cerr << sourceName << ":" << lineNum << ": [syntax error] ";
        if(dynamic_cast<const TokenMismatchedError *>(&e)) {
            std::cerr << *static_cast<const TokenMismatchedError *>(&e) << std::endl;
        } else if(dynamic_cast<const NoViableAlterError *>(&e)) {
            std::cerr << *static_cast<const NoViableAlterError *>(&e) << std::endl;
        } else if(dynamic_cast<const InvalidTokenError *>(&e)) {
            std::cerr << *static_cast<const InvalidTokenError *>(&e) << std::endl;
        }

        std::cerr << line << std::endl;
        Token lineToken;
        lineToken.startPos = 0;
        lineToken.size = line.size();
        std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
        return false;
    } catch(const SemanticError &e) {
        std::cerr << sourceName << ":" << lineNum << ": [semantic error] " <<
        e.getMessage() << std::endl;
        std::cerr << line << std::endl;
        return false;
    }
    return true;
}

template <size_t N>
bool DirectiveParser<N>::parse(const std::string &sourceName, Directive &d) {
    std::ifstream input(sourceName);
    if(!input) {
        fatal("cannot open file: %s\n", sourceName.c_str());
    }

    DirectiveParser parser;
    unsigned int lineNum = 0;
    std::string line;

    while(std::getline(input, line)) {
        lineNum++;

        if(!isDirective(line)) {
            continue;
        }

        return parser(lineNum, sourceName.c_str(), line, d);
    }
    return true;
}

#define CUR_KIND() this->curToken.kind

template <size_t N>
ExecStatus DirectiveParser<N>::resolveStatus(const std::string &str) {
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

#define MATCH(STR, STATUS) if(str == STR) { return ExecStatus::STATUS; }

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
    throw SemanticError(std::move(message));

#undef ALTER
#undef MATCH
#undef EACH_STATUS
}

template <size_t N>
void DirectiveParser<N>::parse_toplevel(Directive &directive) {
    Token token;
    this->expect(APPLIED_NAME, token);

    std::string name(this->lexer->toName(token));
    if(name != "test") {
        std::string str("unsupported directive: ");
        str += name;
        throw SemanticError(std::move(str));
    }

    this->expect(LP);

    bool first = true;
    do {
        if(!first) {
            this->expect(COMMA);
        } else {
            first = false;
        }
        KeyValue keyValue;
        this->parse_keyvalue(keyValue);

        if(keyValue.getKey() == "result" &&
                dynamic_cast<StringNode *>(keyValue.getValue().get()) != nullptr) {
            StringNode *node = static_cast<StringNode *>(keyValue.getValue().get());
            ExecStatus status = this->resolveStatus(node->getValue());
            directive.setResult(status);
            continue;
        }

        if(keyValue.getKey() == "params" &&
                dynamic_cast<ArrayNode *>(keyValue.getValue().get()) != nullptr) {
            ArrayNode *node = static_cast<ArrayNode *>(keyValue.getValue().get());
            for(auto &e : node->getValues()) {
                StringNode *value = dynamic_cast<StringNode *>(e.get());
                if(value == nullptr) {
                    throw SemanticError("require string literal");
                }
                directive.appendParam(std::string(value->getValue()));
            }
            continue;
        }

        if(keyValue.getKey() == "status" &&
                dynamic_cast<NumberNode *>(keyValue.getValue().get()) != nullptr) {
            NumberNode *node = static_cast<NumberNode *>(keyValue.getValue().get());
            directive.setStatus(node->getValue());
            continue;
        }

        std::string msg("unsupported parameter: ");
        msg += keyValue.getKey();
        throw SemanticError(std::move(msg));
    } while(CUR_KIND() == COMMA);
}

template <size_t N>
void DirectiveParser<N>::parse_keyvalue(KeyValue &keyValue) {
    std::string key;
    this->parse_key(key);

    this->expect(ASSIGN);

    std::unique_ptr<Node> value;
    this->parse_value(value);

    keyValue.setKey(std::move(key));
    keyValue.setValue(std::move(value));
}

template <size_t N>
void DirectiveParser<N>::parse_key(std::string &key) {
    Token token;
    this->expect(APPLIED_NAME, token);
    key = this->lexer->toName(token);
}

template <size_t N>
void DirectiveParser<N>::parse_value(std::unique_ptr<Node> &value) {
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

template <size_t N>
void DirectiveParser<N>::parse_number(std::unique_ptr<Node> &node) {
    Token token;
    this->expect(INT_LITERAL, token);
    int status;
    int value = this->lexer->toInt(token, status);
    if(value < 0 || status != 0) {
        std::string str("out of range number: ");
        str += this->lexer->toTokenText(token);
        throw SemanticError(std::move(str));
    }
    node.reset(new NumberNode(value));
}

template <size_t N>
void DirectiveParser<N>::parse_string(std::unique_ptr<Node> &node) {
    Token token;
    this->expect(STRING_LITERAL, token);
    node.reset(new StringNode(this->lexer->toString(token, true)));
}

template <size_t N>
void DirectiveParser<N>::parse_array(std::unique_ptr<Node> &node) {
    this->expect(LB);
    std::unique_ptr<ArrayNode> arrayNode(new ArrayNode());
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


} // namespace __detail_directive

typedef __detail_directive::DirectiveParser<1> DirectiveParser;


} // namespace directive
} // namespace ydsh


#endif //YDSH_DIRECTIVE_HPP
