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

#include <stdarg.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <handle_info.h>
#include <misc/parser_base.hpp>
#include <misc/fatal.h>
#include <DescLexer.h>

namespace {

using namespace ydsh;

static HandleInfo toNum(unsigned int num) {
    // check range
    if(num < 9) {
        HandleInfo info = (HandleInfo) (num + P_N0);
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

static std::string toTypeInfoName(HandleInfo info) {
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
static void error(const char *fmt, ...) {
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

    const std::string &getName(HandleInfo info);

    HandleInfo getInfo(const std::string &name);

private:
    void registerName(HandleInfo info, const char *name);
};

HandleInfoMap::HandleInfoMap() :
        name2InfoMap(), info2NameMap() {
#define REGISTER(ENUM) this->registerName(ENUM, #ENUM);
    EACH_HANDLE_INFO_TYPE(REGISTER)
    EACH_HANDLE_INFO_PTYPE(REGISTER)
    EACH_HANDLE_INFO_TYPE_TEMP(REGISTER)
#undef REGISTER
}

HandleInfoMap &HandleInfoMap::getInstance() {
    static HandleInfoMap map;
    return map;
}

const std::string &HandleInfoMap::getName(HandleInfo info) {
    for(auto &pair : this->info2NameMap) {
        if(pair.first == info) {
            return pair.second;
        }
    }
    fatal("not found handle info: %s\n", toTypeInfoName(info).c_str());

    static std::string str;
    return str;
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
    this->info2NameMap.push_back(std::make_pair(info, actualName));
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

    static std::unique_ptr<TypeToken> newTypeToken(const std::string &name);
};

std::unique_ptr<TypeToken> CommonTypeToken::newTypeToken(const std::string &name) {
    return std::unique_ptr<TypeToken>(
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
            typeTemp(type.release()), requiredSize(elementSize), elements() { }

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

static std::unordered_map<std::string, std::pair<unsigned int, HandleInfo >> initTypeMap() {
    std::unordered_map<std::string, std::pair<unsigned int, HandleInfo >> map;
    map.insert({"Array",  {1, Array}});
    map.insert({"Map",    {2, Map}});
    map.insert({"Tuple",  {0, Tuple}});
    map.insert({"Option",  {1, Option}});
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
            func(func), funcName(std::move(funcName)), op(op), ownerType(), returnType(),
            paramTypes(), paramNames(), actualFuncName() {
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
        this->paramNames.push_back(std::make_pair(std::move(name), hasDefault));
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
        } else {
            std::string str("\"");
            str += this->funcName;
            str += "\"";
            return str;
        }
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

class Parser : public ydsh::parser_base::ParserBase<DescTokenKind, DescLexer> {
private:
    Parser() = default;

public:
    ~Parser() = default;

    /**
     * open file and parse.
     * after parsing, write results to elements.
     */
    static void parse(const char *fileName, std::vector<std::unique_ptr<Element>> &elements);

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

    void parse_params(const std::unique_ptr<Element> &element);

    std::unique_ptr<TypeToken> parse_type();

    void parse_funcDecl(const std::string &line, std::unique_ptr<Element> &element);
};

void Parser::parse(const char *fileName, std::vector<std::unique_ptr<Element>> &elements) {
    std::ifstream input(fileName);
    if(!input) {
        fatal("cannot open file: %s\n", fileName);
    }

    Parser parser;
    unsigned int lineNum = 0;
    std::string line;
    bool foundDesc = false;

    std::unique_ptr<Element> element;
    while(std::getline(input, line)) {
        lineNum++;

        try {
            if(foundDesc) {
                parser.parse_funcDecl(line, element);
                elements.push_back(std::move(element));
                foundDesc = false;
                continue;
            }
            if(isDescriptor(line)) {
                foundDesc = true;
                element = parser.parse_descriptor(line);
            }
        } catch(const ProcessingError &e) {
            std::cerr << fileName << ":" << lineNum
            << ": [error] " << e.getMessage() << std::endl;
            std::cerr << line << std::endl;
            exit(EXIT_FAILURE);
        } catch(const ParseError &e) {
            std::cerr << fileName << ":" << lineNum << ": [error] " << e.getMessage() << std::endl;
            std::cerr << line << std::endl;
            Token lineToken;
            lineToken.pos = 0;
            lineToken.size = line.size();
            std::cerr << parser.lexer->formatLineMarker(lineToken, e.getErrorToken()) << std::endl;
            exit(EXIT_FAILURE);
        }
    }
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

    this->expect(DESC_PREFIX);

    switch(CUR_KIND()) {
    case FUNC:
        return this->parse_funcDesc();
    case INIT:
        return this->parse_initDesc();
    default: {
        const DescTokenKind alters[] = {
                FUNC, INIT,
        };
        this->alternativeError(alters);
    }
    }
    return std::unique_ptr<Element>(nullptr);
}

std::unique_ptr<Element> Parser::parse_funcDesc() {
    this->expect(FUNC);

    std::unique_ptr<Element> element;

    // parser function name
    switch(CUR_KIND()) {
    case IDENTIFIER: {
        Token token = this->expect(IDENTIFIER);
        element = Element::newFuncElement(this->lexer->toTokenText(token), false);
        break;
    }
    case VAR_NAME: {
        Token token = this->expect(VAR_NAME);
        element = Element::newFuncElement(this->toName(token), true);
        break;
    }
    default: {
        const DescTokenKind alters[] = {
                IDENTIFIER, VAR_NAME,
        };
        this->alternativeError(alters);
    }
    }

    // parser parameter decl
    this->expect(LP);
    this->parse_params(element);
    this->expect(RP);

    this->expect(COLON);
    element->setReturnType(this->parse_type());

    return element;
}

std::unique_ptr<Element> Parser::parse_initDesc() {
    this->expect(INIT);

    std::unique_ptr<Element> element(Element::newInitElement());
    this->expect(LP);
    this->parse_params(element);
    this->expect(RP);

    return element;
}

void Parser::parse_params(const std::unique_ptr<Element> &element) {
    int count = 0;
    do {
        if(count++ > 0) {
            this->expect(COMMA);
        }

        Token token = this->expect(VAR_NAME);
        bool hasDefault = false;
        if(CUR_KIND() == OPT) {
            this->expect(OPT);
            hasDefault = true;
        }
        this->expect(COLON);
        std::unique_ptr<TypeToken> type(this->parse_type());

        element->addParam(this->toName(token), hasDefault, std::move(type));
    } while(CUR_KIND() == COMMA);
}

std::unique_ptr<TypeToken> Parser::parse_type() {
    Token token = this->expect(IDENTIFIER);
    if(CUR_KIND() != TYPE_OPEN) {
        return CommonTypeToken::newTypeToken(this->lexer->toTokenText(token));
    }

    auto type(ReifiedTypeToken::newReifiedTypeToken(this->lexer->toTokenText(token)));
    this->expect(TYPE_OPEN);

    if(CUR_KIND() != TYPE_CLOSE) {
        unsigned int count = 0;
        do {
            if(count++ > 0) {
                this->expect(COMMA);
            }
            type->addElement(this->parse_type());
        } while(CUR_KIND() == COMMA);
    }

    this->expect(TYPE_CLOSE);

    return std::unique_ptr<TypeToken>(type.release());
}

void Parser::parse_funcDecl(const std::string &line, std::unique_ptr<Element> &element) {
    DescLexer lexer(line.c_str());
    this->init(lexer);

    const bool isDecl = CUR_KIND() == YDSH_METHOD_DECL;
    if(isDecl) {
        this->expect(YDSH_METHOD_DECL);
    } else {
        this->expect(YDSH_METHOD);
    }

    Token token = this->expect(IDENTIFIER);
    std::string str(this->lexer->toTokenText(token));
    element->setActualFuncName(std::move(str));

    this->expect(LP);
    this->expect(RCTX);
    this->expect(AND);
    this->expect(IDENTIFIER);
    this->expect(RP);
    this->expect(isDecl ? SEMI_COLON : LBC);
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
            info(info), name(HandleInfoMap::getInstance().getName(info)),
            initElement(), funcElements() { }

    ~TypeBind() = default;
};

static std::vector<TypeBind *> genTypeBinds(std::vector<std::unique_ptr<Element>> &elements) {
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

static void gencode(const char *outFileName, const std::vector<TypeBind *> &binds) {
    FILE *fp = fopen(outFileName, "w");
    if(fp == NULL) {
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

    std::vector<std::unique_ptr<Element>> elements;
    Parser::parse(inputFileName, elements);

    std::vector<TypeBind *> binds(genTypeBinds(elements));
    gencode(outputFileName, binds);

    exit(EXIT_SUCCESS);
}

