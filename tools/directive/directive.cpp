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

#include <fstream>
#include <sstream>
#include <unordered_set>

#include "directive_parser.h"
#include <misc/fatal.h>

namespace ydsh {
namespace directive {

// ######################
// ##     TypeImpl     ##
// ######################

std::string TypeImpl::getRealName() {
    if(this->childs.size() == 0) {
        return this->name;
    }

    std::string str(this->name);
    str += "<";
    unsigned int size = this->childs.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            str += ",";
        }
        str += this->childs[i]->getRealName();
    }
    str += ">";
    return str;
}

bool TypeImpl::operator==(const TypeImpl &t) const {
    // check name
    if(this->getName() != t.getName()) {
        return false;
    }

    // check child size
    unsigned int size = this->getChilds().size();
    if(size != t.getChilds().size()) {
        return false;
    }

    // check child
    for(unsigned int i = 0; i < size; i++) {
        if(*this->getChilds()[i] != *t.getChilds()[i]) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<TypeImpl> TypeImpl::create(const char *name) {
    return std::make_shared<TypeImpl>(name, 0);
}

std::shared_ptr<TypeImpl> TypeImpl::create(const char *name, const std::shared_ptr<TypeImpl> &child) {
    auto value(std::make_shared<TypeImpl>(name, 1));
    value->childs[0] = child;
    return value;
}

// #####################
// ##     TypeEnv     ##
// #####################

TypeEnv::TypeEnv() : typeMap() {
    this->addType(TypeImpl::create("Int"));
    this->addType(TypeImpl::create("String"));
    this->addType(TypeImpl::create("Boolean"));
}

const Type &TypeEnv::getType(const std::string &name) {
    auto iter = this->typeMap.find(name);
    if(iter == this->typeMap.end()) {
        fatal("undefined type: %s\n", name.c_str());
    }
    return iter->second;
}

const Type &TypeEnv::getArrayType(const Type &elementType) {
    std::string name("Array<");
    name += elementType->getRealName();
    name += ">";

    if(this->hasType(name)) {
        return this->getType(name);
    }
    return addType(std::move(name), TypeImpl::create("Array", elementType));
}

const Type &TypeEnv::addType(Type &&type) {
    std::string name(type->getRealName());
    return this->addType(std::move(name), std::move(type));
}

const Type &TypeEnv::addType(std::string &&name, Type &&type) {
    auto pair = this->typeMap.insert(std::make_pair(std::move(name), std::move(type)));
    if(!pair.second) {
        fatal("found duplicated type: %s\n", pair.first->first.c_str());
    }
    return pair.first->second;
}

bool TypeEnv::hasType(const std::string &name) {
    auto iter = this->typeMap.find(name);
    return iter != this->typeMap.end();
}


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
            std::cerr << sourceName << ":" << lexer.getLineNum() << ": [syntax error] " << e.getMessage() << std::endl;
            std::cerr << src << std::endl;
            Token lineToken;
            lineToken.pos = 0;
            lineToken.size = line.size();
            std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
            return false;
        } catch(const SemanticError &e) {
            std::cerr << sourceName << ":" << lexer.getLineNum() << ": [semantic error] ";
            std::cerr << e.getMessage() << std::endl;

            std::cerr << src << std::endl;
            Token lineToken;
            lineToken.pos = 0;
            lineToken.size = line.size();
            std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
            return false;
        }
    }
    return true;
}

#define CUR_KIND() this->curKind

void DirectiveParser::parse_toplevel(std::unique_ptr<DirectiveNode> &node) {
    Token token = this->expect(APPLIED_NAME);
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

    this->expect(RP);
}

void DirectiveParser::parse_attribute(std::unique_ptr<AttributeNode> &node) {
    Token token = this->expect(APPLIED_NAME);

    this->expect(ASSIGN);

    std::unique_ptr<Node> value;
    this->parse_value(value);

    node.reset(new AttributeNode(token, this->lexer->toName(token), std::move(value)));
}

void DirectiveParser::parse_value(std::unique_ptr<Node> &value) {
    switch(CUR_KIND()) {
    case INT_LITERAL:
        this->parse_number(value);
        return;
    case STRING_LITERAL:
        this->parse_string(value);
        return;
    case TRUE_LITERAL:
    case FALSE_LITERAL:
        this->parse_boolean(value);
        return;
    case ARRAY_OPEN:
        this->parse_array(value);
        return;
    default:
        const TokenKind alters[] = {
#define EACH_LA_value(OP) \
    OP(INT_LITERAL) \
    OP(STRING_LITERAL) \
    OP(TRUE_LITERAL) \
    OP(FALSE_LITERAL) \
    OP(ARRAY_OPEN)
#define GEN_LA_value(OP) OP,
                EACH_LA_value(GEN_LA_value)
#undef GEN_LA_value
#undef EACH_LA_value
        };
        this->alternativeError(sizeof(alters) / sizeof(alters[0]), alters);
    }
}

void DirectiveParser::parse_number(std::unique_ptr<Node> &node) {
    Token token = this->expect(INT_LITERAL);
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
    Token token = this->expect(STRING_LITERAL);
    node.reset(new StringNode(token, this->lexer->toString(token)));
}

void DirectiveParser::parse_boolean(std::unique_ptr<Node> &node) {
    Token token;
    bool value;
    if(CUR_KIND() == TRUE_LITERAL) {
        token = this->expect(TRUE_LITERAL);
        value = true;
    } else {
        token = this->expect(FALSE_LITERAL);
        value = false;
    }
    node.reset(new BooleanNode(token, value));
}

void DirectiveParser::parse_array(std::unique_ptr<Node> &node) {
    Token token = this->expect(ARRAY_OPEN);
    std::unique_ptr<ArrayNode> arrayNode(new ArrayNode(token));
    std::unique_ptr<Node> value;
    this->parse_value(value);
    arrayNode->appendNode(std::move(value));

    while(CUR_KIND() == COMMA) {
        this->expect(COMMA);

        this->parse_value(value);
        arrayNode->appendNode(std::move(value));
    }
    this->expect(ARRAY_CLOSE);
    node = std::move(arrayNode);
}

template <typename T>
T *cast(Node &node) {
    static_assert(std::is_base_of<Node, T>::value, "not derived type");
    auto t = dynamic_cast<T *>(&node);
    if(t == nullptr) {
        fatal("illegal cast\n");
    }
    return t;
}


// for attribute initialization
struct StatusHandler : public AttributeHandler {
    void operator()(Node &node, Directive &d)  override {
        d.setStatus(cast<NumberNode>(node)->getValue());
    }
};

struct ResultHandler : public AttributeHandler {
    unsigned int resolveStatus(const StringNode &node) {
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

#define MATCH(STR, KIND) if(node.getValue() == STR) { return DS_STATUS_##KIND; }

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

    void operator()(Node &node, Directive &d) override {
        d.setResult(this->resolveStatus(*cast<StringNode>(node)));
    }
};

struct ParamsHandler : public AttributeHandler {
    void operator()(Node &node, Directive &d) override {
        auto value = cast<ArrayNode>(node);
        for(auto &e : value->getValues()) {
            d.appendParam(cast<StringNode>(*e)->getValue());
        }
    }
};

struct LineNumHandler : public AttributeHandler {
    void operator()(Node &node, Directive &d) override {
        d.setLineNum(cast<NumberNode>(node)->getValue());
    }
};

struct IfHaveDBusHandler : public AttributeHandler {
    void operator()(Node &node, Directive &d) override {
        d.setIfHaveDBus(cast<BooleanNode>(node)->getValue());
    }
};

struct ErrorKindHandler : public AttributeHandler {
    void operator()(Node &node, Directive &d) override {
        d.setErrorKind(cast<StringNode>(node)->getValue());
    }
};

// ##################################
// ##     DirectiveInitializer     ##
// ##################################

bool DirectiveInitializer::operator()(const std::unique_ptr<DirectiveNode> &node, Directive &d) {
    // check name
    if(node->getName() != "test") {
        std::string str("unsupported directive: ");
        str += node->getName();
        throw SemanticError(node->getToken(), std::move(str));
    }

    // set handler
    auto statusHandler = StatusHandler();
    auto resultHandler = ResultHandler();
    auto paramHandler = ParamsHandler();
    auto lineNumHandler = LineNumHandler();
    auto ifHaveDBusHandler = IfHaveDBusHandler();
    auto errorKindHandler = ErrorKindHandler();

    this->addHandler("status", this->env.getIntType(), statusHandler);
    this->addHandler("result", this->env.getStringType(), resultHandler);
    this->addHandler("params", this->env.getArrayType(this->env.getStringType()), paramHandler);
    this->addHandler("lineNum", this->env.getIntType(), lineNumHandler);
    this->addHandler("ifHaveDBus", this->env.getBooleanType(), ifHaveDBusHandler);
    this->addHandler("errorKind", this->env.getStringType(), errorKindHandler);

    std::unordered_set<std::string> foundAttrSet;

    for(auto &e : node->getNodes()) {
        const std::string &attrName = e->getName();
        auto *pair = this->lookupHandler(attrName);
        if(pair == nullptr) {
            std::string str("unsupported attribute: ");
            str += attrName;
            throw SemanticError(e->getToken(), std::move(str));
        }

        // check duplication
        auto iter = foundAttrSet.find(attrName);
        if(iter != foundAttrSet.end()) {
            std::string str("duplicated attribute: ");
            str += attrName;
            throw SemanticError(e->getToken(), std::move(str));
        }

        // check type attribute
        this->checkType(pair->first, *e->getAttrNode());

        // invoke handler
        (*pair->second)(*e->getAttrNode(), d);

        foundAttrSet.insert(attrName);
    }
    return true;
}

void DirectiveInitializer::visitDirectiveNode(DirectiveNode &) {
    fatal("unsupported: visitDirectibeNode\n");
}

void DirectiveInitializer::visitAttributeNode(AttributeNode &) {
    fatal("unsupported: visitAttributeNode\n");
}

void DirectiveInitializer::visitNumberNode(NumberNode &node) {
    node.setType(this->env.getIntType());
}

void DirectiveInitializer::visitStringNode(StringNode &node) {
    node.setType(this->env.getStringType());
}

void DirectiveInitializer::visitBooleanNode(BooleanNode &node) {
    node.setType(this->env.getBooleanType());
}

void DirectiveInitializer::visitArrayNode(ArrayNode &node) {
    unsigned int size = node.getValues().size();
    auto type = this->checkType(*node.getValues()[0]);
    for(unsigned int i = 1; i < size; i++) {
        this->checkType(type, *node.getValues()[i]);
    }
    node.setType(this->env.getArrayType(type));
}


Type DirectiveInitializer::checkType(Node &node) {
    auto type = node.getType();
    if(type) {
        return type;
    }

    node.accept(*this);
    type = node.getType();

    if(!type) {
        throw SemanticError(node.getToken(), std::string("require type, but is null"));
    }
    return type;
}

Type DirectiveInitializer::checkType(const Type &requiredType, Node &node) {
    Type type = this->checkType(node);
    if(*type != *requiredType) {
        std::string str("require: ");
        str += requiredType->getRealName();
        str += ", but is: ";
        str += type->getRealName();
        throw SemanticError(node.getToken(), std::move(str));
    }
    return type;
}

void DirectiveInitializer::addHandler(const char *attributeName, const Type &type, AttributeHandler &handler) {
    auto pair = this->handlerMap.insert(std::make_pair(attributeName, std::make_pair(type, &handler)));
    if(!pair.second) {
        fatal("found duplicated handler: %s\n", attributeName);
    }
}

const std::pair<Type, AttributeHandler *> *DirectiveInitializer::lookupHandler(const std::string &name) {
    auto iter = this->handlerMap.find(name);
    if(iter == this->handlerMap.end()) {
        return nullptr;
    }
    return &iter->second;
}


// #######################
// ##     Directive     ##
// #######################

std::unique_ptr<char *[]> Directive::getAsArgv(const char *sourceName) const {
    unsigned int size = this->params.size();
    std::unique_ptr<char *[]> ptr(new char*[size + 2]);

    ptr[0] = const_cast<char *>(sourceName);
    for(unsigned int i = 0; i < size; i++) {
        ptr[i + 1] = const_cast<char *>(this->params[i].c_str());
    }
    ptr[size + 1] = nullptr;
    return ptr;
}

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
