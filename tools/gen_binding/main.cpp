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

#include <cstdarg>

#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <handle_info.h>
#include <symbol.h>
#include <misc/parser_base.hpp>
#include <misc/fatal.h>
#include <DescLexer.h>

namespace {

using namespace ydsh;

HandleInfo toNum(unsigned int num) {
    // check range
    if(num < 9) {
        auto info = (HandleInfo) (num + P_N0);
        switch(info) {
#define GEN_CASE(ENUM) case ENUM:
        EACH_HANDLE_INFO_NUM(GEN_CASE)
#undef GEN_CASE
            return info;
        default:
            break;
        }
    }
    fatal("out of range, must be 0~8\n");
    return P_N0;
}

std::string toTypeInfoName(HandleInfo info) {
    switch(info) {
#define GEN_NAME(INFO) case INFO: return std::string(#INFO);
    EACH_HANDLE_INFO(GEN_NAME)
#undef GEN_NAME
    default:
        fatal("illegal type info: %d\n", info);
        return std::string();
    }
}

class ProcessingError {
private:
    std::string message;

public:
    explicit ProcessingError(const char *message) : message(message) { }

    ~ProcessingError() = default;

    const std::string &getMessage() const {
        return this->message;
    }
};

/**
 * for processing error reporting
 */
void error(const char *fmt, ...) {
    const unsigned int size = 128;
    char buf[size];  // error message must be under size.

    // format message
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);

    throw ProcessingError(buf);
}

class HandleInfoMap {
private:
    std::unordered_map<std::string, HandleInfo> name2InfoMap;
    std::vector<std::pair<HandleInfo, std::string>> info2NameMap;

    HandleInfoMap();

public:
    ~HandleInfoMap() = default;

    static HandleInfoMap &getInstance();

    const char *getName(HandleInfo info) const;

    HandleInfo getInfo(const std::string &name);

private:
    void registerName(HandleInfo info, const char *name);
};

HandleInfoMap::HandleInfoMap() {
#define REGISTER(ENUM) this->registerName(ENUM, #ENUM);
    EACH_HANDLE_INFO_TYPE(REGISTER)
    EACH_HANDLE_INFO_PTYPE(REGISTER)
    EACH_HANDLE_INFO_TYPE_TEMP(REGISTER)
    EACH_HANDLE_INFO_FUNC_TYPE(REGISTER)
#undef REGISTER
}

HandleInfoMap &HandleInfoMap::getInstance() {
    static HandleInfoMap map;
    return map;
}

const char *HandleInfoMap::getName(HandleInfo info) const {
    for(auto &pair : this->info2NameMap) {
        if(pair.first == info) {
            return pair.second.c_str();
        }
    }
    fatal("not found handle info: %s\n", toTypeInfoName(info).c_str());
    return nullptr;
}

HandleInfo HandleInfoMap::getInfo(const std::string &name) {
    auto iter = this->name2InfoMap.find(name);
    if(iter == this->name2InfoMap.end()) {
        error("not found type name: %s", name.c_str());
    }
    return iter->second;
}

void HandleInfoMap::registerName(HandleInfo info, const char *name) {
    std::string actualName(name);
    this->info2NameMap.emplace_back(info, actualName);
    this->name2InfoMap.insert(std::make_pair(actualName, info));
}


class HandleInfoSerializer {
private:
    std::vector<HandleInfo> infos;

public:
    HandleInfoSerializer() = default;

    ~HandleInfoSerializer() = default;

    void add(HandleInfo info) {
        this->infos.push_back(info);
    }

    std::string toString() {
        std::string str("{");
        unsigned int size = this->infos.size();
        for(unsigned int i = 0; i < size; i++) {
            if(i > 0) {
                str += ", ";
            }
            str += toTypeInfoName(this->infos[i]);
        }
        str += "}";

        if(!verifyHandleInfo(this->infos)) {
            fatal("broken handle info: %s\n", str.c_str());
        }
        return str;
    }

private:
    static int getNum(const std::vector<HandleInfo> &infos, unsigned int &index);
    static bool isType(const std::vector<HandleInfo> &infos, unsigned int &index);
    static bool verifyHandleInfo(const std::vector<HandleInfo> &infos);
};

bool HandleInfoSerializer::isType(const std::vector<HandleInfo> &infos, unsigned int &index) {
#define GEN_CASE(ENUM) case ENUM:
    if(index < infos.size()) {
        switch(infos[index++]) {
        EACH_HANDLE_INFO_TYPE(GEN_CASE)
            return true;
        case Array: {
            return getNum(infos, index) == 1 && isType(infos, index);
        }
        case Map: {
            return getNum(infos, index) == 2 && isType(infos, index) && isType(infos, index);
        }
        case Tuple: {
            int num = getNum(infos, index);
            if(num < 0 || num > 8) {
                return false;
            }
            for(int i = 0; i < num; i++) {
                if(!isType(infos, index)) {
                    return false;
                }
            }
            return true;
        }
        case Option: {
            return getNum(infos, index) == 1 && isType(infos, index);
        }
        case Func: {
            if(!isType(infos, index)) {
                return false;
            }
            int num = getNum(infos, index);
            for(int i = 0; i < num; i++) {
                if(!isType(infos, index)) {
                    return false;
                }
            }
            return true;
        }
        EACH_HANDLE_INFO_NUM(GEN_CASE)
            return false;
        EACH_HANDLE_INFO_PTYPE(GEN_CASE)
            return true;
        }
    }
    return false;
#undef GEN_CASE
}

int HandleInfoSerializer::getNum(const std::vector<HandleInfo> &infos, unsigned int &index) {
#define GEN_CASE(ENUM) case ENUM:
    if(index < infos.size()) {
        auto ch = infos[index++];
        switch(ch) {
        EACH_HANDLE_INFO_TYPE(GEN_CASE)
            return -1;
        EACH_HANDLE_INFO_TYPE_TEMP(GEN_CASE)
            return -1;
        EACH_HANDLE_INFO_FUNC_TYPE(GEN_CASE)
            return -1;
        EACH_HANDLE_INFO_NUM(GEN_CASE)
            return (int) (ch - P_N0);
        EACH_HANDLE_INFO_PTYPE(GEN_CASE)
            return -1;
        }
    }
    return -1;
#undef GEN_ENUM
}

bool HandleInfoSerializer::verifyHandleInfo(const std::vector<HandleInfo> &infos) {
    unsigned int index = 0;

    /**
     * check return type
     */
    if(!isType(infos, index)) {
        return false;
    }

    /**
     * check param size
     */
    int paramSize = getNum(infos, index);
    if(paramSize < 0 || paramSize > 8) {
        return false;
    }

    /**
     * check each param type
     */
    for(int i = 0; i < paramSize; i++) {
        if(!isType(infos, index)) {
            return false;
        }
    }
    return index == infos.size();
}


struct TypeToken {
    virtual ~TypeToken() = default;

    virtual void serialize(HandleInfoSerializer &s) = 0;

    virtual bool isType(HandleInfo info) = 0;
};


class CommonTypeToken : public TypeToken {
private:
    HandleInfo info;

public:
    /**
     * not call it directory.
     */
    explicit CommonTypeToken(HandleInfo info) : info(info) { }

    ~CommonTypeToken() = default;

    void serialize(HandleInfoSerializer &s) override {
        s.add(this->info);
    }

    bool isType(HandleInfo info) override {
        return this->info == info;
    }

    static std::unique_ptr<CommonTypeToken> newTypeToken(const std::string &name);
};

std::unique_ptr<CommonTypeToken> CommonTypeToken::newTypeToken(const std::string &name) {
    return std::unique_ptr<CommonTypeToken>(
            new CommonTypeToken(HandleInfoMap::getInstance().getInfo(name)));
}


class ReifiedTypeToken : public TypeToken {
private:
    std::unique_ptr<CommonTypeToken> typeTemp;

    /**
     * element size. must be equivalent to this->elements.size().
     * if requiredSize is 0, allow any elements
     */
    unsigned int requiredSize;

    std::vector<std::unique_ptr<TypeToken>> elements;

    ReifiedTypeToken(std::unique_ptr<CommonTypeToken> &&type, unsigned int elementSize) :
            typeTemp(type.release()), requiredSize(elementSize) { }

public:
    ~ReifiedTypeToken() = default;

    void addElement(std::unique_ptr<TypeToken> &&type) {
        this->elements.push_back(std::move(type));
    }

    void serialize(HandleInfoSerializer &s) override;

    bool isType(HandleInfo info) override {
        return this->typeTemp->isType(info);
    }

    static std::unique_ptr<ReifiedTypeToken> newReifiedTypeToken(const std::string &name);
};

void ReifiedTypeToken::serialize(HandleInfoSerializer &s) {
    // check element size
    unsigned int elementSize = this->elements.size();
    if(this->requiredSize > 0) {
        if(this->requiredSize != elementSize) {
            error("require %d, but is %d", this->requiredSize, this->elements.size());
        }
    }

    typeTemp->serialize(s);
    s.add(toNum(elementSize));
    for(auto &tok : this->elements) {
        tok->serialize(s);
    }
}

std::unordered_map<std::string, std::pair<unsigned int, HandleInfo >> initTypeMap() {
    std::unordered_map<std::string, std::pair<unsigned int, HandleInfo >> map;
    map.insert({TYPE_ARRAY,  {1, Array}});
    map.insert({TYPE_MAP,    {2, Map}});
    map.insert({TYPE_TUPLE,  {0, Tuple}});
    map.insert({TYPE_OPTION,  {1, Option}});
    return map;
}

std::unique_ptr<ReifiedTypeToken> ReifiedTypeToken::newReifiedTypeToken(const std::string &name) {
    static auto typeMap = initTypeMap();
    auto iter = typeMap.find(name);
    if(iter == typeMap.end()) {
        error("unsupported type template: %s", name.c_str());
    }
    auto tok = std::unique_ptr<CommonTypeToken>(new CommonTypeToken(iter->second.second));
    unsigned int size = iter->second.first;
    return std::unique_ptr<ReifiedTypeToken>(new ReifiedTypeToken(std::move(tok), size));
}

class FuncTypeToken : public TypeToken {
private:
    std::unique_ptr<CommonTypeToken> typeTemp;

    std::unique_ptr<TypeToken> returnType;

    std::vector<std::unique_ptr<TypeToken>> paramTypes;

public:
    explicit FuncTypeToken(std::unique_ptr<TypeToken> &&returnType) :
            typeTemp(CommonTypeToken::newTypeToken(TYPE_FUNC)), returnType(std::move(returnType)) {}

    ~FuncTypeToken() = default;

    void addParamType(std::unique_ptr<TypeToken> &&type) {
        this->paramTypes.push_back(std::move(type));
    }

    void serialize(HandleInfoSerializer &s) override;

    bool isType(HandleInfo info) override {
        return this->typeTemp->isType(info);
    }
};

void FuncTypeToken::serialize(HandleInfoSerializer &s) {
    this->typeTemp->serialize(s);
    this->returnType->serialize(s);
    s.add(toNum(this->paramTypes.size()));
    for(auto &tok : this->paramTypes) {
        tok->serialize(s);
    }
}

class Element {
protected:
    /**
     * if true, treat as function,
     * if false, treat as constructor
     */
    bool func;

    /**
     * if this element is constructor, it is empty string
     */
    std::string funcName;

    /**
     * if true, this function is operator
     */
    bool op;

    /**
     * owner type which this element belongs to
     */
    std::unique_ptr<TypeToken> ownerType;

    /**
     * if this element represents for constructor, returnType is always void.
     */
    std::unique_ptr<TypeToken> returnType;

    /**
     * not contains receiver type if this element represents for function
     */
    std::vector<std::unique_ptr<TypeToken>> paramTypes;

    /**
     * contains parameter name and default value flag.
     */
    std::vector<std::pair<std::string, bool>> paramNames;

    /**
     * name of binding function
     */
    std::string actualFuncName;

    Element(std::string &&funcName, bool func, bool op) :
            func(func), funcName(std::move(funcName)), op(op), ownerType(), returnType(){
    }

public:
    static std::unique_ptr<Element> newFuncElement(std::string &&funcName, bool op) {
        return std::unique_ptr<Element>(new Element(std::move(funcName), true, op));
    }

    static std::unique_ptr<Element> newInitElement() {
        std::unique_ptr<Element> element(new Element(std::string(""), false, false));
        element->setReturnType(std::unique_ptr<TypeToken>(new CommonTypeToken(Void)));
        return element;
    }

    virtual ~Element() = default;

    bool isFunc() {
        return this->func;
    }

    void addParam(std::string &&name, bool hasDefault, std::unique_ptr<TypeToken> &&type) {
        if(!this->ownerType) {  // treat first param type as receiver
            this->ownerType = std::move(type);
            return;
        }
        this->paramNames.emplace_back(std::move(name), hasDefault);
        this->paramTypes.push_back(std::move(type));
    }

    void setReturnType(std::unique_ptr<TypeToken> &&type) {
        this->returnType = std::move(type);
    }

    void setActualFuncName(std::string &&name) {
        this->actualFuncName = std::move(name);
    }

    bool isOwnerType(HandleInfo info) {
        return this->ownerType->isType(info);
    }

    std::string toSerializedHandle() {
        HandleInfoSerializer s;
        this->returnType->serialize(s);
        s.add(toNum(this->paramTypes.size() + 1));
        this->ownerType->serialize(s);
        for(const std::unique_ptr<TypeToken> &t : this->paramTypes) {
            t->serialize(s);
        }
        return s.toString();
    }

    std::string toParamNames() {
        std::string str("{");
        str += "\"\"";
        for(auto &pair : this->paramNames) {
            str += ", ";
            str += "\"";
            str += pair.first;
            str += "\"";
        }
        str += "}";
        return str;
    }

    std::string toFuncName() {
        if(this->op) {
            return this->funcName;
        }

        std::string str("\"");
        str += this->funcName;
        str += "\"";
        return str;
    }

    const char *getActualFuncName() {
        return this->actualFuncName.c_str();
    }

    unsigned char toDefaultFlag() {
        unsigned char flag = 0;
        unsigned int size = this->paramNames.size();
        for(unsigned int i = 0; i < size; i++) {
            if(this->paramNames[i].second) {
                flag += (1 << (i + 1));
            }
        }
        return flag;
    }

    std::string toString() {
        std::string str("{");
        str += this->toFuncName();
        str += ", ";
        str += this->toSerializedHandle();
//        str += ", ";
//        str += this->toParamNames();
        str += ", ";
        str += this->getActualFuncName();
//        str += ", ";
//        str += std::to_string((int) this->toDefaultFlag());
        str += "}";
        return str;
    }
};

using ParseError = ydsh::parser_base::ParseError<DescTokenKind>;

#define CUR_KIND() (this->curKind)

#define TRY(expr) \
({ auto v = expr; if(this->hasError()) { return nullptr; } std::forward<decltype(v)>(v); })


class Parser : public ydsh::parser_base::AbstractParser<DescTokenKind, DescLexer> {
public:
    Parser() = default;

    ~Parser() = default;

    std::vector<std::unique_ptr<Element>> operator()(const char *fileName);

private:
    static bool isDescriptor(const std::string &line);

    std::string toName(const Token &token) {
        Token t = token;
        t.pos++;
        t.size--;
        return this->lexer->toTokenText(t);
    }

    void init(DescLexer &lexer) {
        this->lexer = &lexer;
        this->fetchNext();
    }

    std::unique_ptr<Element> parse_descriptor(const std::string &line);

    std::unique_ptr<Element> parse_funcDesc();

    std::unique_ptr<Element> parse_initDesc();

    /**
     *
     * @param element
     * @return
     * always null
     */
    std::unique_ptr<Element> parse_params(std::unique_ptr<Element> &element);

    std::unique_ptr<TypeToken> parse_type();

    /**
     *
     * @param line
     * @param element
     * @return
     * always null
     */
    std::unique_ptr<Element> parse_funcDecl(const std::string &line, std::unique_ptr<Element> &element);
};

std::vector<std::unique_ptr<Element>> Parser::operator()(const char *fileName) {
    std::ifstream input(fileName);
    if(!input) {
        fatal("cannot open file: %s\n", fileName);
    }

    unsigned int lineNum = 0;
    std::string line;
    bool foundDesc = false;

    std::vector<std::unique_ptr<Element>> elements;
    std::unique_ptr<Element> element;
    while(std::getline(input, line)) {
        lineNum++;

        try {
            if(foundDesc) {
                this->parse_funcDecl(line, element);
                if(this->hasError()) {
                    goto ERR;
                }
                elements.push_back(std::move(element));
                foundDesc = false;
                continue;
            }
            if(isDescriptor(line)) {
                foundDesc = true;
                element = this->parse_descriptor(line);
                if(this->hasError()) {
                    goto ERR;
                }
            }

            ERR:
            if(this->hasError()) {
                auto &e = *this->getError();
                std::cerr << fileName << ":" << lineNum << ": [error] " << e.getMessage() << std::endl;
                std::cerr << line << std::endl;
                Token lineToken{};
                lineToken.pos = 0;
                lineToken.size = line.size();
                std::cerr << this->lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
                exit(EXIT_FAILURE);
            }
        } catch(const ProcessingError &e) {
            std::cerr << fileName << ":" << lineNum
                      << ": [error] " << e.getMessage() << std::endl;
            std::cerr << line << std::endl;
            exit(EXIT_FAILURE);
        }
    }

    return elements;
}

bool Parser::isDescriptor(const std::string &line) {
    // skip space
    unsigned int pos = 0;
    unsigned int size = line.size();
    for(; pos < size; pos++) {
        char ch = line[pos];
        if(ch != ' ' && ch != '\t') {
            break;
        }
    }

    const char *prefix = "//!bind:";
    for(unsigned int i = 0; prefix[i] != '\0'; i++) {
        if(pos >= size) {
            return false;
        }
        if(line[pos++] != prefix[i]) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<Element> Parser::parse_descriptor(const std::string &line) {
    DescLexer lexer(line.c_str());
    this->init(lexer);

    TRY(this->expect(DESC_PREFIX));

    switch(CUR_KIND()) {
    case FUNC:
        return this->parse_funcDesc();
    case INIT:
        return this->parse_initDesc();
    default:
        const DescTokenKind alters[] = {
                FUNC, INIT,
        };
        this->raiseNoViableAlterError(alters);
        return nullptr;
    }
}

std::unique_ptr<Element> Parser::parse_funcDesc() {
    TRY(this->expect(FUNC));

    std::unique_ptr<Element> element;

    // parser function name
    switch(CUR_KIND()) {
    case IDENTIFIER: {
        Token token = TRY(this->expect(IDENTIFIER));
        element = Element::newFuncElement(this->lexer->toTokenText(token), false);
        break;
    }
    case VAR_NAME: {
        Token token = TRY(this->expect(VAR_NAME));
        element = Element::newFuncElement(this->toName(token), true);
        break;
    }
    default: {
        const DescTokenKind alters[] = {
                IDENTIFIER, VAR_NAME,
        };
        this->raiseNoViableAlterError(alters);
        return nullptr;
    }
    }

    // parser parameter decl
    TRY(this->expect(LP));
    TRY(this->parse_params(element));
    TRY(this->expect(RP));

    TRY(this->expect(COLON));
    element->setReturnType(TRY(this->parse_type()));

    return element;
}

std::unique_ptr<Element> Parser::parse_initDesc() {
    TRY(this->expect(INIT));

    std::unique_ptr<Element> element(Element::newInitElement());
    TRY(this->expect(LP));
    TRY(this->parse_params(element));
    TRY(this->expect(RP));

    return element;
}

std::unique_ptr<Element> Parser::parse_params(std::unique_ptr<Element> &element) {
    int count = 0;
    do {
        if(count++ > 0) {
            TRY(this->expect(COMMA));
        }

        Token token = TRY(this->expect(VAR_NAME));
        bool hasDefault = false;
        if(CUR_KIND() == OPT) {
            TRY(this->expect(OPT));
            hasDefault = true;
        }
        TRY(this->expect(COLON));
        auto type = TRY(this->parse_type());

        element->addParam(this->toName(token), hasDefault, std::move(type));
    } while(CUR_KIND() == COMMA);

    return nullptr;
}

bool isFunc(const std::string &str) {
    return str == TYPE_FUNC;
}

std::unique_ptr<TypeToken> Parser::parse_type() {
    Token token = TRY(this->expect(IDENTIFIER));
    if(CUR_KIND() != TYPE_OPEN) {
        return CommonTypeToken::newTypeToken(this->lexer->toTokenText(token));
    }

    auto str = this->lexer->toTokenText(token);
    if(isFunc(str)) {
        TRY(this->expect(TYPE_OPEN));
        auto retType = TRY(this->parse_type());
        std::unique_ptr<FuncTypeToken> funcType(new FuncTypeToken(std::move(retType)));

        if(CUR_KIND() != TYPE_CLOSE) {
            TRY(this->expect(COMMA));
            TRY(this->expect(PTYPE_OPEN));
            unsigned int count = 0;
            do {
                if(count++ > 0) {
                    TRY(this->expect(COMMA));
                }
                funcType->addParamType(TRY(this->parse_type()));
            } while(CUR_KIND() == COMMA);
            TRY(this->expect(PTYPE_CLOSE));
        }

        TRY(this->expect(TYPE_CLOSE));

        return std::move(funcType);
    } else {
        auto type(ReifiedTypeToken::newReifiedTypeToken(str));
        TRY(this->expect(TYPE_OPEN));

        if(CUR_KIND() != TYPE_CLOSE) {
            unsigned int count = 0;
            do {
                if(count++ > 0) {
                    TRY(this->expect(COMMA));
                }
                type->addElement(TRY(this->parse_type()));
            } while(CUR_KIND() == COMMA);
        }

        TRY(this->expect(TYPE_CLOSE));

        return std::unique_ptr<TypeToken>(type.release());
    }
}

std::unique_ptr<Element> Parser::parse_funcDecl(const std::string &line, std::unique_ptr<Element> &element) {
    DescLexer lexer(line.c_str());
    this->init(lexer);

    const bool isDecl = CUR_KIND() == YDSH_METHOD_DECL;
    if(isDecl) {
        TRY(this->expect(YDSH_METHOD_DECL));
    } else {
        TRY(this->expect(YDSH_METHOD));
    }

    Token token = TRY(this->expect(IDENTIFIER));
    std::string str(this->lexer->toTokenText(token));
    element->setActualFuncName(std::move(str));

    TRY(this->expect(LP));
    TRY(this->expect(RCTX));
    TRY(this->expect(AND));
    TRY(this->expect(IDENTIFIER));
    TRY(this->expect(RP));
    TRY(this->expect(isDecl ? SEMI_COLON : LBC));

    return nullptr;
}

#define OUT(fmt, ...) \
    do {\
        fprintf(fp, fmt, ## __VA_ARGS__);\
    } while(false)


struct TypeBind {
    HandleInfo info;
    std::string name;

    /**
     * may be nullptr,if has no constructor.
     */
    Element *initElement;

    std::vector<Element *> funcElements;

    explicit TypeBind(HandleInfo info) :
            info(info), name(HandleInfoMap::getInstance().getName(info)), initElement() { }

    ~TypeBind() = default;
};

std::vector<TypeBind *> genTypeBinds(std::vector<std::unique_ptr<Element>> &elements) {
    std::vector<TypeBind *> binds;
#define GEN_BIND(ENUM) binds.push_back(new TypeBind(ENUM));
    EACH_HANDLE_INFO_TYPE(GEN_BIND)
    EACH_HANDLE_INFO_TYPE_TEMP(GEN_BIND)
#undef GEN_BIND

    for(const std::unique_ptr<Element> &element : elements) {
        bool matched = false;
        for(TypeBind *bind : binds) {
            if(element->isOwnerType(bind->info)) {
                matched = true;
                if(element->isFunc()) {
                    bind->funcElements.push_back(element.get());
                } else {
                    bind->initElement = element.get();
                }
                break;
            }
        }
        if(!matched) {
            fatal("invalid owner type\n");
        }
    }
    return binds;
}

void gencode(const char *outFileName, const std::vector<TypeBind *> &binds) {
    FILE *fp = fopen(outFileName, "w");
    if(fp == nullptr) {
        fatal("cannot open output file: %s\n", outFileName);
    }

    // write header
    OUT("#ifndef YDSH_BIND_H\n");
    OUT("#define YDSH_BIND_H\n");
    OUT("\n");
    OUT("#include <builtin.h>\n");
    OUT("#include <symbol.h>\n");
    OUT("\n");
    OUT("namespace ydsh {\n");
    OUT("\n");

    // generate NativeFuncInfo table
    OUT("static NativeFuncInfo infoTable[] = {\n");
    OUT("    {nullptr, {0}, nullptr},\n");
    for(TypeBind *bind : binds) {
        if(bind->initElement != nullptr) {
            OUT("    %s,\n", bind->initElement->toString().c_str());
        }
        for(Element *e : bind->funcElements) {
            OUT("    %s,\n", e->toString().c_str());
        }
    }
    OUT("};\n");
    OUT("NativeFuncInfo *const nativeFuncInfoTable = infoTable;\n");
    OUT("\n");

    OUT("const NativeCode *getNativeCode(unsigned int index) {\n");
    OUT("    static auto codes(initNative(infoTable));\n");
    OUT("    return &codes[index];\n");
    OUT("}\n");
    OUT("\n");

    // generate dummy
    OUT("static native_type_info_t info_Dummy() {\n");
    OUT("    return { .offset = 0, .constructorSize = 0, .methodSize = 0 };\n");
    OUT("}\n");
    OUT("\n");

    unsigned int offsetCount = 1;

    // generate each native_type_info_t
    for(TypeBind *bind : binds) {
        if(bind->name == "Void") {
            continue;   // skip Void due to having no elements.
        }


        unsigned int constructorSize = bind->initElement != nullptr ? 1 : 0;
        unsigned int methodSize = bind->funcElements.size();

        OUT("static native_type_info_t info_%sType() {\n", bind->name.c_str());
        OUT("    return { .offset = %u, .constructorSize = %u, .methodSize = %u };\n",
            bind->initElement == nullptr && bind->funcElements.empty() ? 0 : offsetCount,
            constructorSize, methodSize);
        OUT("}\n");
        OUT("\n");

        offsetCount += constructorSize;
        offsetCount += methodSize;
    }

    OUT("} // namespace ydsh\n");
    OUT("\n");
    OUT("#endif //YDSH_BIND_H\n");
    fclose(fp);
}

} // namespace

int main(int argc, char **argv) {
    if(argc != 3) {
        fprintf(stderr, "usage: %s [target source] [generated code]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *inputFileName = argv[1];
    const char *outputFileName = argv[2];

    auto elements = Parser()(inputFileName);
    std::vector<TypeBind *> binds(genTypeBinds(elements));
    gencode(outputFileName, binds);

    exit(EXIT_SUCCESS);
}

