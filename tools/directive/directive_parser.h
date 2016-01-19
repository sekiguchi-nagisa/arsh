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

#ifndef YDSH_DIRECTIVE_PARSER_H
#define YDSH_DIRECTIVE_PARSER_H

#include <type_traits>
#include <memory>
#include <unordered_map>

#include "directive.h"
#include "directive_lexer.h"
#include <parser/parser_base.hpp>

namespace ydsh {
namespace directive {

class DirectiveNode;
class AttributeNode;
class NumberNode;
class StringNode;
class BooleanNode;
class ArrayNode;

struct NodeVisitor {
    virtual ~NodeVisitor() = default;

    virtual void visitDirectiveNode(DirectiveNode &node) = 0;
    virtual void visitAttributeNode(AttributeNode &node) = 0;
    virtual void visitNumberNode(NumberNode &node) = 0;
    virtual void visitStringNode(StringNode &node) = 0;
    virtual void visitBooleanNode(BooleanNode &node) = 0;
    virtual void visitArrayNode(ArrayNode &node) = 0;
};

class TypeImpl {
private:
    std::string name;
    std::vector<std::shared_ptr<TypeImpl>> childs;

public:
    TypeImpl(const char *name, unsigned int childSize) : name(name), childs(childSize) { }
    ~TypeImpl() = default;

    const std::string &getName() const {
        return this->name;
    }

    const std::vector<std::shared_ptr<TypeImpl>> &getChilds() const {
        return this->childs;
    }

    std::string getRealName();

    bool operator==(const TypeImpl &t) const;

    bool operator!=(const TypeImpl &t) const {
        return !(*this == t);
    }

    static std::shared_ptr<TypeImpl> create(const char *name);
    static std::shared_ptr<TypeImpl> create(const char *name, const std::shared_ptr<TypeImpl> &child);
};

typedef std::shared_ptr<TypeImpl> Type;

class TypeEnv {
private:
    std::unordered_map<std::string, Type> typeMap;

public:
    TypeEnv();
    ~TypeEnv() = default;

    const Type &getType(const std::string &name);

    const Type &getIntType() {
        return this->getType(std::string("Int"));
    }

    const Type &getStringType() {
        return this->getType(std::string("String"));
    }

    const Type &getBooleanType() {
        return this->getType(std::string("Boolean"));
    }

    const Type &getArrayType(const Type &elementType);

private:
    const Type &addType(Type &&type);
    const Type &addType(std::string &&name, Type &&type);
    bool hasType(const std::string &name);
};


class Node {
protected:
    Token token;
    Type type;

public:
    Node(const Token &token) : token(token) {}
    virtual ~Node() = default;

    const Token &getToken() const {
        return this->token;
    }

    const Type &getType() const {
        return this->type;
    }

    void setType(const Type &type) {
        this->type = type;
    }

    virtual void accept(NodeVisitor &visitor) = 0;
};

/**
 * root node
 */
class DirectiveNode : public Node {
private:
    std::string name;
    std::vector<std::unique_ptr<AttributeNode>> nodes;

public:
    DirectiveNode(const Token &token, std::string &&name) : Node(token), name(std::move(name)), nodes() {}
    ~DirectiveNode() = default;

    const std::string &getName() const {
        return this->name;
    }

    void append(std::unique_ptr<AttributeNode> &&node) {
        this->nodes.push_back(std::move(node));
    }

    const std::vector<std::unique_ptr<AttributeNode>> &getNodes() const {
        return this->nodes;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitDirectiveNode(*this);
    }
};

/**
 * represent key-value pair of attribute
 * ex. $hoge = 34
 */
class AttributeNode : public Node {
private:
    std::string name;
    std::unique_ptr<Node> attrNode;

public:
    AttributeNode(const Token &token, std::string &&name, std::unique_ptr<Node> &&attrNode) :
            Node(token), name(std::move(name)), attrNode(std::move(attrNode)) {}
    ~AttributeNode() = default;

    const std::string &getName() const {
        return this->name;
    }

    const std::unique_ptr<Node> &getAttrNode() const {
        return this->attrNode;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitAttributeNode(*this);
    }
};

class NumberNode : public Node {
private:
    unsigned int value;

public:
    NumberNode(const Token &token, unsigned int value) : Node(token), value(value) {}
    ~NumberNode() = default;

    unsigned int getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitNumberNode(*this);
    }
};

class StringNode : public Node {
private:
    std::string value;

public:
    StringNode(const Token &token, std::string &&value) : Node(token), value(std::move(value)) {}
    ~StringNode() = default;

    const std::string &getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitStringNode(*this);
    }
};

class BooleanNode : public Node {
private:
    bool value;

public:
    BooleanNode(const Token &token, bool value) : Node(token), value(value) {}
    ~BooleanNode() = default;

    bool getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitBooleanNode(*this);
    }
};

class ArrayNode : public Node {
private:
    std::vector<std::unique_ptr<Node>> values;

public:
    ArrayNode(const Token &token) : Node(token), values() {}
    ~ArrayNode() = default;

    void appendNode(std::unique_ptr<Node> &&node) {
        this->values.push_back(std::move(node));
    }

    const std::vector<std::unique_ptr<Node>> &getValues() const {
        return this->values;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitArrayNode(*this);
    }
};

using ParseError = ydsh::parser_base::ParseError<TokenKind>;

class SemanticError {
private:
    Token errorToken;
    std::string message;

public:
    SemanticError(const Token &token, const char *message) :
            errorToken(token), message(message) {}
    SemanticError(const Token &token, std::string &&message) :
            errorToken(token), message(std::move(message)) {}
    ~SemanticError() = default;

    const Token &getErrorToken() const {
        return this->errorToken;
    }

    const std::string &getMessage() const {
        return this->message;
    }
};

/**
 * ex. $test($result = "SUCCESS", $args = ['sd', '32'])
 *
 * test directive use subset of ydsh lexical rule.
 */
class DirectiveParser : public ydsh::parser_base::ParserBase<TokenKind, Lexer> {
public:
    DirectiveParser() = default;
    ~DirectiveParser() = default;

    bool operator()(const char *sourceName, std::istream &input, Directive &d);

private:
    void parse_toplevel(std::unique_ptr<DirectiveNode> &node);
    void parse_attribute(std::unique_ptr<AttributeNode> &node);
    void parse_value(std::unique_ptr<Node> &value);
    void parse_number(std::unique_ptr<Node> &node);
    void parse_string(std::unique_ptr<Node> &node);
    void parse_boolean(std::unique_ptr<Node> &node);
    void parse_array(std::unique_ptr<Node> &node);
};

struct AttributeHandler {
    virtual void operator()(Node &node, Directive &d) = 0;
};

class DirectiveInitializer : public NodeVisitor {
private:
    TypeEnv env;
    typedef std::pair<Type, AttributeHandler *> Handler;
    std::unordered_map<std::string, Handler> handlerMap;

public:
    DirectiveInitializer() = default;
    ~DirectiveInitializer() = default;

    /**
     * entry point.
     */
    bool operator()(const std::unique_ptr<DirectiveNode> &node, Directive &d);

    void visitDirectiveNode(DirectiveNode &node) override;
    void visitAttributeNode(AttributeNode &node) override;
    void visitNumberNode(NumberNode &node) override;
    void visitStringNode(StringNode &node) override;
    void visitBooleanNode(BooleanNode &node) override;
    void visitArrayNode(ArrayNode &node) override;

private:
    Type checkType(Node &node);
    Type checkType(const Type &requiredType, Node &node);
    void addHandler(const char *attributeName, const Type &type, AttributeHandler &handler);

    /**
     * if not found corresponding handler, return null.
     */
    const std::pair<Type, AttributeHandler *> *lookupHandler(const std::string &name);
};


} // namespace directive
} // namespace ydsh

#endif //YDSH_DIRECTIVE_PARSER_H
