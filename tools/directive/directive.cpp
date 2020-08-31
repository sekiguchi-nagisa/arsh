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
#include "../platform/platform.h"

#include <misc/fatal.h>
#include <parser.h>
#include <type_checker.h>
#include <core.h>

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
        return std::make_unique<ApplyNode>(std::move(exprNode), ArgsWrapper::extract(std::move(args)));
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

class DirectiveInitializer : public TypeChecker {
private:
    std::string sourceName;
    using AttributeHandler = std::function<void(Node &, Directive &)>;
    using Handler = std::pair<DSType *, AttributeHandler>;
    std::unordered_map<std::string, Handler> handlerMap;

    std::unique_ptr<TypeCheckError> error;

public:
    DirectiveInitializer(const char *sourceName, SymbolTable &symbolTable);
    ~DirectiveInitializer() override = default;

    void operator()(ApplyNode &node, Directive &d);

    bool hasError() const {
        return static_cast<bool>(this->error);
    }

    const TypeCheckError &getError() const {
        return *this->error;
    }

private:
    void addHandler(const char *attributeName, DSType &type, AttributeHandler &&handler);

    void addHandler(const char *attributeName, TYPE type, AttributeHandler &&handler) {
        this->addHandler(attributeName, this->symbolTable.get(type), std::move(handler));
    }

    void addAlias(const char *alias, const char *attr);
    int resolveKind(const StringNode &node);

    /**
     * if not found corresponding handler, return null.
     */
    const std::pair<DSType *, AttributeHandler> *lookupHandler(const std::string &name) const;

    bool checkNode(NodeKind kind, const Node &node);

    void setVarName(const char *name, DSType &type);

    template <typename T>
    T *checkedCast(Node &node) {
        if(!this->checkNode(T::KIND, node)) {
            return nullptr;
        }
        return cast<T>(&node);
    }

    /**
     * return always [String : String] type
     * @return
     */
    DSType &getMapType() {
        return *this->symbolTable.createMapType(
                this->symbolTable.get(TYPE::String), this->symbolTable.get(TYPE::String)).take();
    }

    void createError(const Node &node, const std::string &str) {
        this->error = std::make_unique<TypeCheckError>(node.getToken(), "", CStrPtr(strdup(str.c_str())));
    }
};

#undef TRY
#define TRY(E) ({ auto v = E; if(this->hasError()) { return; } std::forward<decltype(v)>(v); })

// ##################################
// ##     DirectiveInitializer     ##
// ##################################

static bool checkDirectiveName(ApplyNode &node) {
    assert(node.getExprNode().is(NodeKind::Var));
    auto &exprNode = cast<VarNode>(node.getExprNode());    //NOLINT
    return exprNode.getVarName() == "test";
}

DirectiveInitializer::DirectiveInitializer(const char *sourceName, SymbolTable &symbolTable) :
        TypeChecker(symbolTable, false, nullptr), sourceName(sourceName) {
    this->setVarName("0", this->symbolTable.get(TYPE::String));
}

static bool isIgnoredUser(const std::string &text) {
    return text.find('#') != std::string::npos && getuid() == 0;
}

void DirectiveInitializer::operator()(ApplyNode &node, Directive &d) {
    if(!checkDirectiveName(node)) {
        std::string str("unsupported directive: ");
        str += cast<VarNode>(node.getExprNode()).getVarName();    //NOLINT
        return this->createError(node, str);
    }

    this->addHandler("status", TYPE::Int, [&](Node &node, Directive &d) {
        int64_t value = TRY(this->checkedCast<NumberNode>(node))->getIntValue();
        if(value < INT32_MIN || value > INT32_MAX) {
            std::string str = "must be int32 value: ";
            str += std::to_string(value);
            return this->createError(node, str);
        }
        d.setStatus(static_cast<int>(value));
    });

    this->addHandler("result", TYPE::String, [&](Node &node, Directive &d) {
        auto *strNode = TRY(this->checkedCast<StringNode>(node));
        d.setKind(TRY(this->resolveKind(*strNode)));
    });

    this->addHandler("params", TYPE::StringArray, [&](Node &node, Directive &d) {
        auto *value = TRY(this->checkedCast<ArrayNode>(node));
        for(auto &e : value->getExprNodes()) {
            d.appendParam(TRY(this->checkedCast<StringNode>(*e))->getValue());
        }
    });

    this->addHandler("lineNum", TYPE::Int, [&](Node &node, Directive &d) {
        d.setLineNum(TRY(this->checkedCast<NumberNode>(node))->getIntValue());
    });

    this->addHandler("errorKind", TYPE::String, [&](Node &node, Directive &d) {
        d.setErrorKind(TRY(this->checkedCast<StringNode>(node))->getValue());
    });

    this->addHandler("in", TYPE::String, [&](Node &node, Directive &d) {
        d.setIn(TRY(this->checkedCast<StringNode>(node))->getValue());
    });

    this->addHandler("out", TYPE::String, [&](Node &node, Directive &d) {
        d.setOut(TRY(this->checkedCast<StringNode>(node))->getValue());
    });

    this->addHandler("err", TYPE::String, [&](Node &node, Directive &d) {
        d.setErr(TRY(this->checkedCast<StringNode>(node))->getValue());
    });

    this->addHandler("fileName", TYPE::String, [&](Node &node, Directive &d) {
        if(node.is(NodeKind::Var) &&
           TRY(this->checkedCast<VarNode>(node))->getVarName() == "0") {
            d.setFileName(this->sourceName.c_str());
            return;
        }

        std::string str = TRY(this->checkedCast<StringNode>(node))->getValue();
        expandTilde(str);
        char *buf = realpath(str.c_str(), nullptr);
        if(buf == nullptr) {
            std::string message = "invalid file name: ";
            message += str;
            return this->createError(node, message);
        }
        d.setFileName(buf);
        free(buf);
    });

    this->addHandler("envs", this->getMapType(), [&](Node &node, Directive &d) {
        auto *mapNode = TRY(this->checkedCast<MapNode>(node));
        const unsigned int size = mapNode->getKeyNodes().size();
        for(unsigned int i = 0; i < size; i++) {
            auto *keyNode = TRY(this->checkedCast<StringNode>(*mapNode->getKeyNodes()[i]));
            auto *valueNode = TRY(this->checkedCast<StringNode>(*mapNode->getValueNodes()[i]));
            d.addEnv(keyNode->getValue(), valueNode->getValue());
        }
    });
    this->addAlias("env", "envs");

    this->addHandler("ignored", TYPE::String, [&](Node &node, Directive &d) {
        auto &str = TRY(this->checkedCast<StringNode>(node))->getValue();
        d.setIgnoredPlatform(isIgnoredUser(str) || platform::contain(str));
    });

    std::unordered_set<std::string> foundAttrSet;
    for(auto &attrNode : node.getArgNodes()) {
        auto *assignNode = TRY(this->checkedCast<AssignNode>(*attrNode));
        auto &attrName = TRY(this->checkedCast<VarNode>(assignNode->getLeftNode()))->getVarName();
        auto *pair = this->lookupHandler(attrName);
        if(pair == nullptr) {
            std::string str("unsupported attribute: ");
            str += attrName;
            return this->createError(assignNode->getLeftNode(), str);
        }

        // check duplication
        auto iter = foundAttrSet.find(attrName);
        if(iter != foundAttrSet.end()) {
            std::string str("duplicated attribute: ");
            str += attrName;
            return this->createError(assignNode->getLeftNode(), str);
        }

        // check type attribute
        try {
            this->checkType(*pair->first, assignNode->getRightNode());
        } catch(const TypeCheckError &e) {
            this->error = std::make_unique<TypeCheckError>(e);
            return;
        }

        // invoke handler
        (pair->second)(assignNode->getRightNode(), d);
        if(this->hasError()) {
            return;
        }
        foundAttrSet.insert(attrName);
    }
}

void DirectiveInitializer::addHandler(const char *attributeName, DSType &type, AttributeHandler &&handler) {
    auto pair = this->handlerMap.emplace(attributeName, std::make_pair(&type, std::move(handler)));
    if(!pair.second) {
        fatal("found duplicated handler: %s\n", attributeName);
    }
}

void DirectiveInitializer::addAlias(const char *alias, const char *attr) {
    auto *handler = this->lookupHandler(attr);
    if(handler != nullptr) {
        this->addHandler(alias, *handler->first, AttributeHandler(handler->second));
    }
}

int DirectiveInitializer::resolveKind(const StringNode &node) {
    const struct {
        const char *name;
        int kind;
    } statusTable[] = {
#define _E(K) DS_ERROR_KIND_##K
            {"success",         _E(SUCCESS)},
            {"parse_error",     _E(PARSE_ERROR)},
            {"parse",           _E(PARSE_ERROR)},
            {"type_error",      _E(TYPE_ERROR)},
            {"type",            _E(TYPE_ERROR)},
            {"codegen",         _E(CODEGEN_ERROR)},
            {"runtime_error",   _E(RUNTIME_ERROR)},
            {"runtime",         _E(RUNTIME_ERROR)},
            {"throw",           _E(RUNTIME_ERROR)},
            {"assertion_error", _E(ASSERTION_ERROR)},
            {"assert",          _E(ASSERTION_ERROR)},
            {"exit",            _E(EXIT)},
#undef _E
    };

    const auto &value = node.getValue();
    for(auto &e : statusTable) {
        if(strcasecmp(value.c_str(), e.name) == 0) {
            return e.kind;
        }
    }

    if(!value.empty()) {
        std::vector<std::string> alters;
        for(auto &e : statusTable) {
            alters.emplace_back(e.name);
        }

        std::string message = "illegal status: ";
        message += value;
        message += ", expect for ";
        unsigned int count = 0;
        for(auto &e : alters) {
            if(count++ > 0) {
                message += ", ";
            }
            message += e;
        }
        createError(node, message);
    }
    return -1;
}

const std::pair<DSType *, DirectiveInitializer::AttributeHandler>
        *DirectiveInitializer::lookupHandler(const std::string &name) const {
    auto iter = this->handlerMap.find(name);
    if(iter == this->handlerMap.end()) {
        return nullptr;
    }
    return &iter->second;
}

bool DirectiveInitializer::checkNode(NodeKind kind, const Node &node) {
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
        this->createError(node, str);
        return false;
    }
    return true;
}

void DirectiveInitializer::setVarName(const char *name, DSType &type) {
    this->symbolTable.newHandle(name, type, FieldAttribute());
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
    std::cerr << sourceName << ":" << lexer.getMaxLineNum() << ": [" << errorName << " error] ";
    std::cerr << message << std::endl;
    std::cerr << line << std::endl;
    std::cerr << lexer.formatLineMarker(lineToken, errorToken) << std::endl;
}

static bool initDirective(const char *fileName, std::istream &input, Directive &directive) {
    auto ret = extractDirective(input);
    if(ret.first.empty()) {
        return true;
    }

    ByteBuffer buf;
    buf.append(ret.first.c_str(), ret.first.size());
    Lexer lexer(fileName, std::move(buf), nullptr);
    lexer.setLineNumOffset(ret.second);

    DirectiveParser parser(lexer);
    auto node = parser();
    if(parser.hasError()) {
        auto &e = parser.getError();
        showError(fileName, lexer, ret.first, e.getErrorToken(), e.getMessage(), "syntax");
        return false;
    }

    SymbolTable symbolTable;
    DirectiveInitializer initializer(fileName, symbolTable);
    initializer(*node, directive);
    if(initializer.hasError()) {
        auto &e = initializer.getError();
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