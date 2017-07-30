/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include <functional>

#include "directive.h"
#include "directive_lexer.h"
#include <misc/parser_base.hpp>

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

    std::string getRealName() const;

    bool operator==(const TypeImpl &t) const;

    bool operator!=(const TypeImpl &t) const {
        return !(*this == t);
    }

    static std::shared_ptr<TypeImpl> create(const char *name);
    static std::shared_ptr<TypeImpl> create(const char *name, const std::shared_ptr<TypeImpl> &child);
};

using Type = std::shared_ptr<TypeImpl>;

class TypeEnv {
private:
    std::unordered_map<std::string, Type> typeMap;

public:
    TypeEnv();
    ~TypeEnv() = default;

    const Type &getType(const std::string &name) const;

    const Type &getIntType() const {
        return this->getType(std::string("Int"));
    }

    const Type &getStringType() const {
        return this->getType(std::string("String"));
    }

    const Type &getBooleanType() const {
        return this->getType(std::string("Boolean"));
    }

    const Type &getArrayType(const Type &elementType);

private:
    const Type &addType(Type &&type);
    const Type &addType(std::string &&name, Type &&type);
    bool hasType(const std::string &name) const;
};


class Node {
public:
    const enum NodeKind {
        Directive,
        Attribute,
        Number,
        String,
        Boolean,
        Array,
    } kind;

protected:
    Token token;
    Type type;

public:
    Node(NodeKind kind, const Token &token) : kind(kind), token(token) {}
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
    DirectiveNode(const Token &token, std::string &&name) : Node(Directive, token), name(std::move(name)), nodes() {}
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

    static bool classof(Node *node) {
        return node != nullptr && node->kind == Directive;
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
            Node(Attribute, token), name(std::move(name)), attrNode(std::move(attrNode)) {}
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

    static bool classof(Node *node) {
        return node != nullptr && node->kind == Attribute;
    }
};

class NumberNode : public Node {
private:
    unsigned int value;

public:
    NumberNode(const Token &token, unsigned int value) : Node(Number, token), value(value) {}
    ~NumberNode() = default;

    unsigned int getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitNumberNode(*this);
    }

    static bool classof(Node *node) {
        return node != nullptr && node->kind == Number;
    }
};

class StringNode : public Node {
private:
    std::string value;

public:
    StringNode(const Token &token, std::string &&value) : Node(String, token), value(std::move(value)) {}
    ~StringNode() = default;

    const std::string &getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitStringNode(*this);
    }

    static bool classof(Node *node) {
        return node != nullptr && node->kind == String;
    }
};

class BooleanNode : public Node {
private:
    bool value;

public:
    BooleanNode(const Token &token, bool value) : Node(Boolean, token), value(value) {}
    ~BooleanNode() = default;

    bool getValue() const {
        return this->value;
    }

    void accept(NodeVisitor &visitor) override {
        visitor.visitBooleanNode(*this);
    }

    static bool classof(Node *node) {
        return node != nullptr && node->kind == Boolean;
    }
};

class ArrayNode : public Node {
private:
    std::vector<std::unique_ptr<Node>> values;

public:
    explicit ArrayNode(const Token &token) : Node(Array, token), values() {}
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

    static bool classof(Node *node) {
        return node != nullptr && node->kind == Array;
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
class DirectiveParser : public ydsh::parser_base::AbstractParser<TokenKind, Lexer> {
public:
    DirectiveParser() = default;
    ~DirectiveParser() = default;

    bool operator()(const char *sourceName, std::istream &input, Directive &d);

private:
    std::unique_ptr<DirectiveNode> parse_toplevel();
    std::unique_ptr<AttributeNode> parse_attribute();
    std::unique_ptr<Node> parse_value();
    std::unique_ptr<Node> parse_number();
    std::unique_ptr<Node> parse_string();
    std::unique_ptr<Node> parse_boolean();
    std::unique_ptr<Node> parse_array();
};


using AttributeHandler = std::function<void(Node &, Directive &)>;

class DirectiveInitializer : protected NodeVisitor {
private:
    TypeEnv env;
    using Handler = std::pair<Type, AttributeHandler>;
    std::unordered_map<std::string, Handler> handlerMap;

    std::unique_ptr<SemanticError> error;

public:
    DirectiveInitializer() = default;
    ~DirectiveInitializer() = default;

    /**
     * entry point.
     */
    void operator()(const std::unique_ptr<DirectiveNode> &node, Directive &d);

    bool hasError() const {
        return static_cast<bool>(this->error);
    }

    const SemanticError &getError() const {
        return *this->error;
    }

private:
    void visitDirectiveNode(DirectiveNode &node) override;
    void visitAttributeNode(AttributeNode &node) override;
    void visitNumberNode(NumberNode &node) override;
    void visitStringNode(StringNode &node) override;
    void visitBooleanNode(BooleanNode &node) override;
    void visitArrayNode(ArrayNode &node) override;

    Type checkType(Node &node);
    Type checkType(const Type &requiredType, Node &node);
    void addHandler(const char *attributeName, const Type &type, AttributeHandler &&handler);
    unsigned int resolveStatus(const StringNode &node);

    /**
     * if not found corresponding handler, return null.
     */
    const std::pair<Type, AttributeHandler> *lookupHandler(const std::string &name) const;

    template <typename ...Arg>
    void createError(Arg && ...arg) {
        this->error.reset(new SemanticError(std::forward<Arg>(arg)...));
    }
};


} // namespace directive
} // namespace ydsh

#endif //YDSH_DIRECTIVE_PARSER_H
