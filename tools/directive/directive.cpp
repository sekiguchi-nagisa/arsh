/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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
#include <functional>

#include "directive.h"

#include <misc/fatal.h>
#include <parser.h>
#include <type_checker.h>

namespace ydsh {
namespace directive {

#define TRY(expr) \
({ auto v = expr; if(this->hasError()) { return nullptr; } std::forward<decltype(v)>(v); })

struct DirectiveParser : public Parser {
    explicit DirectiveParser(Lexer &lexer) : Parser(lexer) {}
    ~DirectiveParser() = default;

    std::unique_ptr<ApplyNode> operator()() {
        auto exprNode = TRY(this->parse_appliedName(false));
        auto args = TRY(this->parse_arguments());
        TRY(this->expect(EOS));
        return make_unique<ApplyNode>(exprNode.release(), ArgsWrapper::extract(std::move(args)));
    }
};

static bool isDirective(const std::string &line) {
    auto *ptr = line.c_str();
    return strstr(ptr, "#$test") == ptr;
}

static std::pair<std::string, unsigned int> extractDirective(std::istream &input) {
    unsigned int lineNum = 0;

    for(std::string line; std::getline(input, line); ) {
        lineNum++;
        if(isDirective(line)) {
            line.erase(line.begin());
            return {line, lineNum};
        }
    }
    return {std::string(), 0};
}

using AttributeHandler = std::function<void(Node &, Directive &)>;

class DirectiveInitializer : public TypeChecker {
private:
    using Handler = std::pair<DSType *, AttributeHandler>;
    std::unordered_map<std::string, Handler> handlerMap;

public:
    explicit DirectiveInitializer(SymbolTable &symbolTable);
    ~DirectiveInitializer() override = default;

    void operator()(ApplyNode &node, Directive &d);

private:
    void addHandler(const char *attributeName, DSType &type, AttributeHandler &&handler);
    unsigned int resolveStatus(const StringNode &node);

    /**
     * if not found corresponding handler, return null.
     */
    const std::pair<DSType *, AttributeHandler> *lookupHandler(const std::string &name) const;

    void checkNode(NodeKind kind, const Node &node);

    void setVarName(const char *name, DSType &type);

    template <typename T>
    T &checkedCast(Node &node) {
        this->checkNode(type2info<T>::value, node);
        return static_cast<T &>(node);
    }
};

// ##################################
// ##     DirectiveInitializer     ##
// ##################################

static bool checkDirectiveName(ApplyNode &node) {
    assert(node.getExprNode()->is(NodeKind::Var));
    auto *exprNode = static_cast<VarNode *>(node.getExprNode());
    return exprNode->getVarName() == "test";
}

static bool toBool(const std::string &str) {
    return strcasecmp(str.c_str(), "true") == 0;
}

DirectiveInitializer::DirectiveInitializer(SymbolTable &symbolTable) :
        TypeChecker(symbolTable, false) {
    auto &boolType = this->symbolTable.getBooleanType();
    const char *names[] = {
            "TRUE", "True", "true", "FALSE", "False", "false",
    };
    for(auto &name : names) {
        this->setVarName(name, boolType);
    }
}

void DirectiveInitializer::operator()(ApplyNode &node, Directive &d) {
    if(!checkDirectiveName(node)) {
        std::string str("unsupported directive: ");
        str += static_cast<VarNode *>(node.getExprNode())->getVarName();
        throw TypeCheckError(node.getToken(), "", str.c_str());
    }

    this->addHandler("status", this->symbolTable.getIntType(), [&](Node &node, Directive &d) {
        d.setStatus(this->checkedCast<NumberNode>(node).getIntValue());
    });

    this->addHandler("result", this->symbolTable.getStringType(), [&](Node &node, Directive &d) {
        d.setResult(this->resolveStatus(this->checkedCast<StringNode>(node)));
    });

    this->addHandler("params", this->symbolTable.getStringArrayType(), [&](Node &node, Directive &d) {
        auto &value = this->checkedCast<ArrayNode>(node);
        for(auto &e : value.getExprNodes()) {
            d.appendParam(this->checkedCast<StringNode>(*e).getValue());
        }
    });

    this->addHandler("lineNum", this->symbolTable.getInt32Type(), [&](Node &node, Directive &d) {
        d.setLineNum(this->checkedCast<NumberNode>(node).getIntValue());
    });

    this->addHandler("ifHaveDBus", this->symbolTable.getBooleanType(), [&](Node &node, Directive &d) {
        bool v = toBool(this->checkedCast<VarNode>(node).getVarName());
        d.setIfHaveDBus(v);
    });

    this->addHandler("errorKind", this->symbolTable.getStringType(), [&](Node &node, Directive &d) {
        d.setErrorKind(this->checkedCast<StringNode>(node).getValue());
    });

    this->addHandler("out", this->symbolTable.getStringType(), [&](Node &node, Directive &d) {
        d.setOut(this->checkedCast<StringNode>(node).getValue());
    });

    this->addHandler("err", this->symbolTable.getStringType(), [&](Node &node, Directive &d) {
        d.setErr(this->checkedCast<StringNode>(node).getValue());
    });

    std::unordered_set<std::string> foundAttrSet;
    for(auto &attrNode : node.getArgNodes()) {
        auto &assignNode = this->checkedCast<AssignNode>(*attrNode);
        auto &attrName = this->checkedCast<VarNode>(*assignNode.getLeftNode()).getVarName();
        auto *pair = this->lookupHandler(attrName);
        if(pair == nullptr) {
            std::string str("unsupported attribute: ");
            str += attrName;
            throw TypeCheckError(assignNode.getLeftNode()->getToken(), "", str.c_str());
        }

        // check duplication
        auto iter = foundAttrSet.find(attrName);
        if(iter != foundAttrSet.end()) {
            std::string str("duplicated attribute: ");
            str += attrName;
            throw TypeCheckError(assignNode.getLeftNode()->getToken(), "", str.c_str());
        }

        // check type attribute
        this->checkType(*pair->first, assignNode.getRightNode());

        // invoke handler
        (pair->second)(*assignNode.getRightNode(), d);

        foundAttrSet.insert(attrName);
    }
}

void DirectiveInitializer::addHandler(const char *attributeName, DSType &type, AttributeHandler &&handler) {
    auto pair = this->handlerMap.insert(std::make_pair(attributeName, std::make_pair(&type, std::move(handler))));
    if(!pair.second) {
        fatal("found duplicated handler: %s\n", attributeName);
    }
}

unsigned int DirectiveInitializer::resolveStatus(const StringNode &node) {
    const struct {
        const char *name;
        unsigned int status;
    } statusTable[] = {
#define _E(K) DS_ERROR_KIND_##K
            {"success",         _E(SUCCESS)},
            {"parse_error",     _E(PARSE_ERROR)},
            {"parse",           _E(PARSE_ERROR)},
            {"type_error",      _E(TYPE_ERROR)},
            {"type",            _E(TYPE_ERROR)},
            {"runtime_error",   _E(RUNTIME_ERROR)},
            {"runtime",         _E(RUNTIME_ERROR)},
            {"throw",           _E(RUNTIME_ERROR)},
            {"assertion_error", _E(ASSERTION_ERROR)},
            {"assert",          _E(ASSERTION_ERROR)},
            {"exit",            _E(EXIT)},
#undef _E
    };

    for(auto &e : statusTable) {
        if(strcasecmp(node.getValue().c_str(), e.name) == 0) {
            return e.status;
        }
    }

    std::vector<std::string> alters;
    for(auto &e : statusTable) {
        alters.emplace_back(e.name);
    }

    std::string message("illegal status, expect for ");
    unsigned int count = 0;
    for(auto &e : alters) {
        if(count++ > 0) {
            message += ", ";
        }
        message += e;
    }
    throw TypeCheckError(node.getToken(), "", message.c_str());
//    return 0;
}

const std::pair<DSType *, AttributeHandler> *DirectiveInitializer::lookupHandler(const std::string &name) const {
    auto iter = this->handlerMap.find(name);
    if(iter == this->handlerMap.end()) {
        return nullptr;
    }
    return &iter->second;
}

void DirectiveInitializer::checkNode(NodeKind kind, const Node &node) {
    const char *table[] = {
#define GEN_STR(K) #K,
            EACH_NODE_KIND(GEN_STR)
#undef GEN_STR
    };

    if(!node.is(kind)) {
        std::string str = "require: ";
        str += table[static_cast<unsigned int>(kind)];
        str += "Node, but is: ";
        str += table[static_cast<unsigned int>(node.getNodeKind())];
        str += "Node";
        throw TypeCheckError(node.getToken(), "", str.c_str());
    }
}

void DirectiveInitializer::setVarName(const char *name, DSType &type) {
    this->symbolTable.registerHandle(name, type, FieldAttributes());
}


// #######################
// ##     Directive     ##
// #######################

Directive::~Directive() {
    free(this->out);
    free(this->err);
}

static void showError(const char *sourceName, Lexer &lexer, const std::string &line,
                      Token errorToken, const std::string &message, const char *errorName) {
    Token lineToken = {0, static_cast<unsigned int>(line.size())};
    std::cerr << sourceName << ":" << lexer.getLineNum() << ": [" << errorName << " error] ";
    std::cerr << message << std::endl;
    std::cerr << line << std::endl;
    std::cerr << lexer.formatLineMarker(lineToken, errorToken) << std::endl;
}

static bool initDirective(const char *fileName, std::istream &input, Directive &directive) {
    auto ret = extractDirective(input);
    if(ret.first.empty()) {
        return true;
    }

    Lexer lexer(fileName, ret.first.c_str());
    lexer.setLineNum(ret.second);

    DirectiveParser parser(lexer);
    auto node = parser();
    if(parser.hasError()) {
        auto &e = parser.getError();
        showError(fileName, lexer, ret.first, e.getErrorToken(), e.getMessage(), "syntax");
        return false;
    }

    try {
        SymbolTable symbolTable;
        DirectiveInitializer initializer(symbolTable);
        initializer(*node, directive);
    } catch(const TypeCheckError &e) {
        showError(fileName, lexer, ret.first, e.getToken(), e.getMessage(), "semantic");
        return false;
    }
    return true;
}

bool Directive::init(const char *fileName, Directive &d) {
    std::ifstream input(fileName);
    if(!input) {
        fatal("cannot open file: %s\n", fileName);
    }
    return initDirective(fileName, input, d);
}

bool Directive::init(const char *sourceName, const char *src, Directive &d) {
    std::istringstream input(src);
    return initDirective(sourceName, input, d);
}

} // namespace directive
} // namespace ydsh