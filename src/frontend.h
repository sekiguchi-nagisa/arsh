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
    C(Yellow,  33) \
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
        Parser parser;
        std::unique_ptr<ModuleScope> scope;
        std::unique_ptr<SourceListNode> srcListNode;

        Context(Lexer &&lexer, std::unique_ptr<ModuleScope> &&scope,
                ObserverPtr<CodeCompletionHandler> ccHandler = nullptr) :
                lexer(std::move(lexer)), parser(this->lexer, ccHandler), scope(std::move(scope)){}
    };

    std::vector<std::unique_ptr<Context>> contexts;

    const DSExecMode mode;
    TypeChecker checker;
    DSType *prevType{nullptr};
    ObserverPtr<ErrorReporter> reporter;
    ObserverPtr<NodeDumper> uastDumper;
    ObserverPtr<NodeDumper> astDumper;

public:
    FrontEnd(Lexer &&lexer, TypePool &typePool, SymbolTable &symbolTable, DSExecMode mode, bool toplevel,
             ObserverPtr<CodeCompletionHandler> ccHandler = nullptr);

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

    TypePool &getTypePool() {
        return this->checker.getTypePool();
    }

    void discard(TypePool::DiscardPoint discardPoint) {
        this->checker.getSymbolTable().abort();
        this->checker.getTypePool().discard(discardPoint);
    }

    const Lexer &getCurrentLexer() const {
        return this->contexts.back()->lexer;
    }

    bool frontEndOnly() const {
        return this->mode == DS_EXEC_MODE_PARSE_ONLY || this->mode == DS_EXEC_MODE_CHECK_ONLY;
    }

    unsigned int getRootLineNum() const {
        return this->contexts[0]->lexer.getMaxLineNum();
    }

    const std::unique_ptr<SourceListNode> &getCurSrcListNode() const {
        return this->contexts.back()->srcListNode;
    }

    bool hasUnconsumedPath() const {
        auto &e = this->getCurSrcListNode();
        return e && e->hasUnconsumedPath();
    }

    explicit operator bool() const {
        return static_cast<bool>(this->parser()) || this->contexts.size() > 1 || this->hasUnconsumedPath();
    }

    Ret operator()(DSError *dsError);

    void setupASTDump();

    void teardownASTDump();

    void handleError(DSErrorKind type, const char *errorKind,
                     Token errorToken, const char *message, DSError *dsError) const;

private:
    Parser &parser() {
        return this->contexts.back()->parser;
    }

    const Parser &parser() const {
        return this->contexts.back()->parser;
    }

    /**
     *
     * @return
     */
    const char *getCurScriptDir() const {
        return this->contexts.back()->lexer.getScriptDir();
    }

    std::unique_ptr<SourceListNode> &getCurSrcListNode() {
        return this->contexts.back()->srcListNode;
    }

    std::unique_ptr<Node> tryToParse(DSError *dsError);

    bool tryToCheckType(std::unique_ptr<Node> &node, DSError *dsError);

    Ret loadModule(DSError *dsError);

    void enterModule(const char *fullPath, ByteBuffer &&buf);

    std::unique_ptr<SourceNode> exitModule();

    // for error reporting
    void handleParseError(DSError *dsError) const {
        auto &e = this->parser().getError();
        Token errorToken = this->parser().getLexer()->shiftEOS(e.getErrorToken());
        return this->handleError(DS_ERROR_KIND_PARSE_ERROR,
                e.getErrorKind(), errorToken, e.getMessage().c_str(), dsError);
    }

    void handleTypeError(const TypeCheckError &e, DSError *dsError) const {
        this->handleError(DS_ERROR_KIND_TYPE_ERROR, e.getKind(), e.getToken(), e.getMessage(), dsError);
    }
};

} // namespace ydsh

#endif //YDSH_FRONTEND_H
