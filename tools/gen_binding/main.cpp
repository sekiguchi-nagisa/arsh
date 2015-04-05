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

#include <core/handle_info.h>
#include <DescLexer.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

using namespace ydsh::core;

typedef struct {
    const char *handleInfo;
    unsigned int pos;
} context_t;

static TypeInfo toNum(unsigned int num) {
    // check range
    if(num < 9) {
        TypeInfo info = (TypeInfo) (num + P_N0);
        switch(info) {
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return info;
        default:
            break;
        }
    }
    fatal("out of range, must be 0~8\n");
    return P_N0;
}


static bool isType(context_t *ctx) {
    if(ctx->handleInfo[ctx->pos] != '\0') {
        switch(ctx->handleInfo[ctx->pos++]) {
        case VOID_T:
        case ANY_T:
        case INT_T:
        case FLOAT_T:
        case BOOL_T:
        case STRING_T:
        case ARRAY_T:
        case MAP_T:
            return true;
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return false;
        case T0:
        case T1:
            return true;
        }
    }
    return false;
}

static int getNum(context_t *ctx) {
    if(ctx->handleInfo[ctx->pos] != '\0') {
        char ch = ctx->handleInfo[ctx->pos++];
        switch(ch) {
        case VOID_T:
        case ANY_T:
        case INT_T:
        case FLOAT_T:
        case BOOL_T:
        case STRING_T:
        case ARRAY_T:
        case MAP_T:
            return -1;
        case P_N0:
        case P_N1:
        case P_N2:
        case P_N3:
        case P_N4:
        case P_N5:
        case P_N6:
        case P_N7:
        case P_N8:
            return (int) (ch - P_N0);
        case T0:
        case T1:
            return -1;
        }
    }
    return -1;
}

static bool verifyHandleInfo(char *handleInfo) {
    context_t ctx = {handleInfo, 0};

    /**
     * check return type
     */
    if(!isType(&ctx)) {
        return false;
    }

    /**
     * check param size
     */
    int paramSize = getNum(&ctx);
    if(paramSize < 0 || paramSize > 8) {
        return false;
    }

    /**
     * check param types
     */
    for(int i = 0; i < paramSize; i++) {
        if(!isType(&ctx)) {
            return false;
        }
    }

    /**
     * check null terminate
     */
    return ctx.handleInfo[ctx.pos] == '\0';
}

static std::string toTypeInfoName(TypeInfo info) {
#define EACH_TYPE_INFO(OP) \
    OP(VOID_T) \
    OP(ANY_T) \
    OP(INT_T) \
    OP(FLOAT_T) \
    OP(BOOL_T) \
    OP(STRING_T) \
    OP(ARRAY_T) \
    OP(MAP_T) \
    OP(P_N0) \
    OP(P_N1) \
    OP(P_N2) \
    OP(P_N3) \
    OP(P_N4) \
    OP(P_N5) \
    OP(P_N6) \
    OP(P_N7) \
    OP(P_N8) \
    OP(T0) \
    OP(T1)

    switch(info) {
    #define GEN_NAME(INFO) case INFO: return std::string(#INFO);
        EACH_TYPE_INFO(GEN_NAME)
    #undef GEN_NAME
    default:
        fatal("illegal type info: %d", info);
        return std::string();
    }

#undef EACH_TYPE_INFO
}

class ProcessingError {
private:
    std::string message;

public:
    ProcessingError(const char *message) :
            message(message) {
    }

    ~ProcessingError() {
    }

    const std::string &getMessage() const {
        return this->message;
    }
};

/**
 * for processing error reporting
 */
static void error(const char *fmt, ...) {
    const static unsigned int size = 128;
    static char buf[size];  // error message must be under size.

    // formate message
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);

    throw ProcessingError(buf);
}



class HandleInfoSerializer {
private:
    std::vector<TypeInfo> infos;

public:
    HandleInfoSerializer() : infos() {
    }

    ~HandleInfoSerializer() {
    }

    void add(TypeInfo info) {
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
        return str;
    }
};

class TypeToken {
public:
    TypeToken() {
    }

    virtual ~TypeToken() {
    }

    virtual void serialize(HandleInfoSerializer &s) = 0;
    virtual bool isType(TypeInfo info) = 0;
};


class CommonTypeToken : public  TypeToken {
private:
    TypeInfo info;

public:
    /**
     * not call it directory.
     */
    CommonTypeToken(TypeInfo info) :
        info(info) {
    }

    ~CommonTypeToken() {
    }

    void serialize(HandleInfoSerializer &s) {    // override
        s.add(this->info);
    }

    bool isType(TypeInfo info) {    // override
        return this->info == info;
    }

    static std::unique_ptr<TypeToken> newTypeToken(const std::string &name);
};

std::unique_ptr<TypeToken> CommonTypeToken::newTypeToken(const std::string &name) {
    TypeInfo info = VOID_T;

    if(name == "Void") {
        info = VOID_T;
    } else if(name == "Any") {
        info = ANY_T;
    } else if(name == "Int") {
        info = INT_T;
    } else if(name == "Float") {
        info = FLOAT_T;
    } else if(name == "Boolean") {
        info = BOOL_T;
    } else if(name == "String") {
        info = STRING_T;
    } else if(name == "T0") {
        info = T0;
    } else if(name == "T1") {
        info = T1;
    } else {
        error("unsupported type: %s\n", name.c_str());
    }

    return std::unique_ptr<TypeToken>(new CommonTypeToken(info));
}


class ReifiedTypeToken : public TypeToken {
private:
    std::unique_ptr<CommonTypeToken> typeTemp;

    /**
     * element size. must be equivalent to this->elements.size()
     */
    unsigned int requiredSize;

    std::vector<std::unique_ptr<TypeToken>> elements;

    ReifiedTypeToken(std::unique_ptr<CommonTypeToken> &&type, unsigned int elementSize) :
            typeTemp(type.release()), requiredSize(elementSize), elements() {
    }

public:
    ~ReifiedTypeToken() {
    }

    void addElement(std::unique_ptr<TypeToken> &&type) {
        this->elements.push_back(std::move(type));
    }
    void serialize(HandleInfoSerializer &s);    // override

    bool isType(TypeInfo info) {    // override
        return this->typeTemp->isType(info);
    }

    static std::unique_ptr<ReifiedTypeToken> newReifiedTypeToken(const std::string &name);
};

void ReifiedTypeToken::serialize(HandleInfoSerializer &s) {
    // check element size
    if(this->requiredSize != this->elements.size()) {
        error("require %d, but is %d\n", this->requiredSize, this->elements.size());
    }

    typeTemp->serialize(s);
    s.add(toNum(this->requiredSize));
    for(auto &tok : this->elements) {
        tok->serialize(s);
    }
}

std::unique_ptr<ReifiedTypeToken> ReifiedTypeToken::newReifiedTypeToken(const std::string &name) {
    std::unique_ptr<CommonTypeToken> tok;
    unsigned int size = 0;
    if(name == "Array") {
        tok.reset(new CommonTypeToken(ARRAY_T));
        size = 1;
    } else if(name == "Map") {
        tok.reset(new CommonTypeToken(MAP_T));
        size = 2;
    } else {
        error("unsupported type template: %s\n", name.c_str());
    }
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
            func(func), funcName(funcName), op(op), ownerType(), returnType(),
            paramTypes(), paramNames(), actualFuncName() {
    }

public:
    static std::unique_ptr<Element> newFuncElement(std::string &&funcName, bool op) {
        return std::unique_ptr<Element>(new Element(std::move(funcName), true, op));
    }

    virtual ~Element() {
    }

    bool isFunc() {
        return this->func;
    }

    void addParam(std::string &&name, bool hasDefault, std::unique_ptr<TypeToken> && type) {
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
        this->actualFuncName = name;
    }

    bool isOwnerType(TypeInfo info) {
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
        str += ", ";
        str += this->toParamNames();
        str += ", ";
        str += this->getActualFuncName();
        str += ", ";
        str += std::to_string((int) this->toDefaultFlag());
        str += "}";
        return str;
    }
};

class Parser {
private:
    /**
     * not call destructor
     */
    ydsh::parser::Lexer<DescLexer, DescTokenKind> *lexer;

    DescTokenKind  kind;
    ydsh::parser::Token token;

    Parser() :
            lexer() {
    }

public:
    ~Parser() {
    }

    /**
     * open file and parse.
     * after parsing, write results to elements.
     */
    static void parse(char *fileName, std::vector<std::unique_ptr<Element>> &elements);

private:
    static bool isDescriptor(const std::string &line);

    void nextToken() {
        this->kind = this->lexer->nextToken(this->token);
    }

    void matchToken(DescTokenKind expected) {
        if(this->kind != expected) {
            error("expected: %s, but is: %s, %s",
                  getTokenKindName(expected),
                  getTokenKindName(this->kind),
                  this->lexer->toTokenText(this->token).c_str());
        }
        this->nextToken();
    }

    ydsh::parser::Token matchAndGetToken(DescTokenKind expected) {
        if(this->kind != expected) {
            error("expected: %s, but is: %s, %s",
                  getTokenKindName(expected),
                  getTokenKindName(this->kind),
                  this->lexer->toTokenText(this->token).c_str());
        }
        auto token(this->token);
        this->nextToken();
        return token;
    }

    DescTokenKind consumeAndGetKind() {
        DescTokenKind kind = this->kind;
        this->nextToken();
        return kind;
    }

    void init(ydsh::parser::Lexer<DescLexer, DescTokenKind> &lexer) {
        this->lexer = &lexer;
        this->nextToken();
    }

    std::unique_ptr<Element> parse_descriptor(const std::string &line);
    std::unique_ptr<Element> parse_funcDes();
    void parse_params(const std::unique_ptr<Element> &element);
    std::unique_ptr<TypeToken> parse_type();

    void parse_funcDecl(const std::string &line, std::unique_ptr<Element> &element);
};

void Parser::parse(char *fileName, std::vector<std::unique_ptr<Element>> &elements) {
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

    static const char prefix[] = "//!bind:";
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
    ydsh::parser::Lexer<DescLexer, DescTokenKind> lexer(line.c_str());
    this->init(lexer);

    this->matchToken(DESC_PREFIX);

    switch(this->kind) {
    case FUNC:
        return this->parse_funcDes();
    case INIT:
        break;
    default:
        error("illegal token: %s", this->lexer->toTokenText(this->token).c_str());
        break;
    }
    return std::unique_ptr<Element>(nullptr);
}

std::unique_ptr<Element> Parser::parse_funcDes() {
    this->matchToken(FUNC);

    std::unique_ptr<Element> element;

    // parser function name
    switch(this->kind) {
    case IDENTIFIER: {
        auto token = this->matchAndGetToken(IDENTIFIER);
        element = Element::newFuncElement(this->lexer->toName(token), false);
        break;
    }
    case VAR_NAME: {
        auto token = this->matchAndGetToken(VAR_NAME);
        element = Element::newFuncElement(this->lexer->toName(token), true);
        break;
    }
    default:
        error("illegal token: %s", this->lexer->toTokenText(this->token).c_str());
        return std::unique_ptr<Element>(nullptr);
    }

    // parser parameter decl
    this->matchToken(LP);
    this->parse_params(element);
    this->matchToken(RP);

    this->matchToken(COLON);
    element->setReturnType(this->parse_type());

    return element;
}

void Parser::parse_params(const std::unique_ptr<Element> &element) {
    int count = 0;
    do {
        if(count++ > 0) {
            this->matchToken(COMMA);
        }

        auto token = this->matchAndGetToken(VAR_NAME);
        bool hasDefault = false;
        if(this->kind == OPT) {
            this->matchToken(OPT);
            hasDefault = true;
        }
        this->matchToken(COLON);
        std::unique_ptr<TypeToken> type(this->parse_type());

        element->addParam(this->lexer->toName(token), hasDefault, std::move(type));
    } while(this->kind == COMMA);
}

std::unique_ptr<TypeToken> Parser::parse_type() {
    switch(this->kind) {
    case IDENTIFIER: {
        auto token = this->matchAndGetToken(IDENTIFIER);
        return CommonTypeToken::newTypeToken(this->lexer->toTokenText(token));
    };
    case ARRAY:
    case MAP: {
        auto token = this->token;
        this->nextToken();

        auto type(ReifiedTypeToken::newReifiedTypeToken(this->lexer->toTokenText(token)));
        this->matchToken(TYPE_OPEN);

        unsigned int count = 0;
        do {
            if(count++ > 0) {
                this->matchToken(COMMA);
            }
            type->addElement(this->parse_type());
        } while(this->kind == COMMA);

        this->matchToken(TYPE_CLOSE);

        return std::unique_ptr<TypeToken>(type.release());
    };
    default:
        error("invalid token: %s", this->lexer->toTokenText(this->token).c_str());
        return std::unique_ptr<TypeToken>();
    }
}

void Parser::parse_funcDecl(const std::string &line, std::unique_ptr<Element> &element) {
    ydsh::parser::Lexer<DescLexer, DescTokenKind> lexer(line.c_str());
    this->init(lexer);

    this->matchToken(STATIC);
    this->matchToken(INLINE);
    this->matchToken(BOOL);

    auto token = this->matchAndGetToken(IDENTIFIER);
    std::string str(this->lexer->toTokenText(token));
    element->setActualFuncName(std::move(str));

    this->matchToken(LP);
    this->matchToken(RCTX);
    this->matchToken(AND);
    this->matchToken(IDENTIFIER);
    this->matchToken(RP);
    this->matchToken(LBC);
}

#define OUT(fmt, ...) \
    do {\
        fprintf(fp, fmt, ## __VA_ARGS__);\
    } while(0)


struct TypeBind {
    TypeInfo info;
    std::string name;

    /**
     * may be nullptr,if has no constructor.
     */
    Element *initElement;

    std::vector<Element*> funcElements;

    TypeBind(TypeInfo info, const char *name) :
            info(info), name(name), initElement(), funcElements() {
    }

    ~TypeBind() {
    }
};

static std::vector<TypeBind *> genTypeBinds(std::vector<std::unique_ptr<Element>> &elements) {
    std::vector<TypeBind *> binds;
    binds.push_back(new TypeBind(VOID_T, "Void"));
    binds.push_back(new TypeBind(ANY_T, "Any"));
    binds.push_back(new TypeBind(INT_T, "Int"));
    binds.push_back(new TypeBind(FLOAT_T, "Float"));
    binds.push_back(new TypeBind(BOOL_T, "Boolean"));
    binds.push_back(new TypeBind(STRING_T, "String"));
    binds.push_back(new TypeBind(ARRAY_T, "Array"));
    binds.push_back(new TypeBind(MAP_T, "Map"));

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
        fatal("cannot open output file: %s", outFileName);
    }

    // write header
    OUT("#include <core/bind.h>\n");
    OUT("#include <core/builtin.cpp>\n");
    OUT("#include <core/symbol.h>\n");
    OUT("#include <core/DSType.h>\n");
    OUT("\n");
    OUT("namespace ydsh {\n");
    OUT("namespace core {\n");
    OUT("\n");

    // generate dummy
    OUT("native_type_info_t *info_Dummy() {\n");
    OUT("    static native_type_info_t info = {0, 0, {}};\n");
    OUT("    return &info;\n");
    OUT("}\n");
    OUT("\n");

    // generate type binding
    for(TypeBind *bind : binds) {
        OUT("native_type_info_t *info_%sType() {\n", bind->name.c_str());
        if(bind->initElement != 0) {
            OUT("     static NativeFuncInfo initInfo = %s;\n",
                bind->initElement->toString().c_str());
        }
        OUT("    static native_type_info_t info = {\n");
        OUT("        .initInfo = %s,\n", (bind->initElement == 0 ? "0" : "initInfo"));
        OUT("        .methodSize = %ld,\n", bind->funcElements.size());
        OUT("        .funcInfos = {\n");
        for(Element *e : bind->funcElements) {
            OUT("            %s,\n", e->toString().c_str());
        }
        OUT("        }\n");
        OUT("    };\n");
        OUT("    return &info;\n");
        OUT("}\n");
        OUT("\n");
    }

    OUT("} // namespace core\n");
    OUT("} // namespace ydsh\n");
    fclose(fp);
}

int main(int argc, char **argv) {
    if(argc != 3) {
        fprintf(stderr, "usage: %s [target source] [generated code]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *inputFileName = argv[1];
    char *outputFileName = argv[2];

    std::vector<std::unique_ptr<Element>> elements;
    Parser::parse(inputFileName, elements);

    std::vector<TypeBind *> binds(genTypeBinds(elements));
    gencode(outputFileName, binds);

    exit(EXIT_SUCCESS);
}

