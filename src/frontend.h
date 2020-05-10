/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef YDSH_FRONTEND_H
#define YDSH_FRONTEND_H

#include <ydsh/ydsh.h>
#include "parser.h"
#include "type_checker.h"

namespace ydsh {

#define EACH_TERM_COLOR(C) \
    C(Reset,    0) \
    C(Bold,     1) \
    /*C(Black,   30)*/ \
    /*C(Red,     31)*/ \
    C(Green,   32) \
    /*C(Yellow,  33)*/ \
    C(Blue,    34) \
    C(Magenta, 35) \
    C(Cyan,    36) /*\
    C(white,   37)*/

enum class TermColor : unsigned int {   // ansi color code
#define GEN_ENUM(E, N) E,
    EACH_TERM_COLOR(GEN_ENUM)
#undef GEN_ENUM
};

class ErrorReporter {
private:
    FILE *fp;
    bool close;
    bool tty;

public:
    ErrorReporter(FILE *fp, bool close);

    ~ErrorReporter();

    void operator()(const Lexer &lex, const char *kind, Token token, TermColor c, const char *message) const;

private:
    const char *color(TermColor c) const;

    void printErrorLine(const Lexer &lexer, Token token) const;
};

class FrontEnd {
public:
    enum Status : unsigned char {
        IN_MODULE,
        ENTER_MODULE,
        EXIT_MODULE,
        FAILED,
    };

    struct Ret {
        std::unique_ptr<Node> node;
        Status status;

        explicit operator bool() const {
            return this->status != FAILED;
        }
    };

private:
    struct Context {
        Lexer lexer;
        ModuleScope scope;

        // for saving old state
        TokenKind kind;
        Token token;
        TokenKind consumedKind;
        std::unique_ptr<SourceListNode> srcListNode;

        Context(Lexer &&lexer, ModuleScope &&scope, std::tuple<TokenKind, Token, TokenKind > &&state) :
                lexer(std::move(lexer)), scope(std::move(scope)),
                kind(std::get<0>(state)), token(std::get<1>(state)),
                consumedKind(std::get<2>(state)){}
    };

    // root lexer state
    Lexer lexer;

    std::vector<std::unique_ptr<Context>> contexts;

    const DSExecMode mode;
    Parser parser;
    TypeChecker checker;
    DSType *prevType{nullptr};

    std::unique_ptr<SourceListNode> srcListNode;

    ObserverPtr<ErrorReporter> reporter;
    ObserverPtr<NodeDumper> uastDumper;
    ObserverPtr<NodeDumper> astDumper;

public:
    FrontEnd(Lexer &&lexer, SymbolTable &symbolTable, DSExecMode mode, bool toplevel);

    ~FrontEnd() {
        this->getSymbolTable().clear();
    }

    void setErrorReporter(ErrorReporter &r) {
        this->reporter.reset(&r);
    }

    void setUASTDumper(NodeDumper &dumper) {
        assert(dumper);
        this->uastDumper.reset(&dumper);
    }

    void setASTDumper(NodeDumper &dumper) {
        assert(dumper);
        this->astDumper.reset(&dumper);
    }

    SymbolTable &getSymbolTable() {
        return this->checker.getSymbolTable();
    }

    const Lexer &getCurrentLexer() const {
        return *this->parser.getLexer();
    }

    bool frontEndOnly() const {
        return this->mode == DS_EXEC_MODE_PARSE_ONLY || this->mode == DS_EXEC_MODE_CHECK_ONLY;
    }

    unsigned int getRootLineNum() const {
        return this->lexer.getMaxLineNum();
    }

    explicit operator bool() const {
        return static_cast<bool>(this->parser) || !this->contexts.empty();
    }

    Ret operator()(DSError *dsError);

    void setupASTDump();

    void teardownASTDump();

private:
    /**
     *
     * @return
     */
    const char *getCurScriptDir() const {
        return (this->contexts.empty() ? this->lexer : this->contexts.back()->lexer).getScriptDir();
    }

    std::unique_ptr<SourceListNode> &getCurSrcListNode() {
        return this->contexts.empty() ? this->srcListNode : this->contexts.back()->srcListNode;
    }

    std::unique_ptr<Node> tryToParse(DSError *dsError);

    bool tryToCheckType(std::unique_ptr<Node> &node, DSError *dsError);

    Ret loadModule(DSError *dsError);

    void enterModule(const char *fullPath, ByteBuffer &&buf);

    std::unique_ptr<SourceNode> exitModule();

    // for error reporting
    void handleError(DSErrorKind type, const char *errorKind,
            Token errorToken, const std::string &message, DSError *dsError) const;

    void handleParseError(DSError *dsError) const {
        auto &e = this->parser.getError();
        Token errorToken = this->parser.getLexer()->shiftEOS(e.getErrorToken());
        return this->handleError(DS_ERROR_KIND_PARSE_ERROR, e.getErrorKind(), errorToken, e.getMessage(), dsError);
    }

    void handleTypeError(const TypeCheckError &e, DSError *dsError) const {
        this->handleError(DS_ERROR_KIND_TYPE_ERROR, e.getKind(), e.getToken(), e.getMessage(), dsError);
    }
};

} // namespace ydsh

#endif //YDSH_FRONTEND_H
