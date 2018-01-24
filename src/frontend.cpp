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

#include <cstdlib>
#include <unistd.h>

#include "frontend.h"
#include "symbol.h"
#include "core.h"

namespace ydsh {

FrontEnd::FrontEnd(Lexer &lexer, TypePool &pool, SymbolTable &symbolTable,
                   DSExecMode mode, bool toplevel, const DumpTarget &target) :
        mode(mode), parser(lexer), checker(pool, symbolTable, toplevel),
        uastDumper(target.fps[DS_DUMP_KIND_UAST], pool),
        astDumper(target.fps[DS_DUMP_KIND_AST], pool) {
    this->checker.reset();
}

/**
 * not allow dumb terminal
 */
static bool isSupportedTerminal(int fd) {
    const char *term = getenv(ENV_TERM);
    return term != nullptr && strcasecmp(term, "dumb") != 0 && isatty(fd) != 0;
}

#define EACH_TERM_COLOR(C) \
    C(Reset,    0) \
    C(Bold,     1) \
    /*C(Black,   30)*/ \
    /*C(Red,     31)*/ \
    C(Green,   32) \
    /*C(Yellow,  33)*/ \
    /*C(Blue,    34)*/ \
    C(Magenta, 35) \
    C(Cyan,    36) /*\
    C(white,   37)*/

enum class TermColor : unsigned int {   // ansi color code
#define GEN_ENUM(E, N) E,
    EACH_TERM_COLOR(GEN_ENUM)
#undef GEN_ENUM
};

static const char *color(TermColor color, bool isatty) {
    if(isatty) {
#define GEN_STR(E, C) "\033[" #C "m",
        const char *ansi[] = {
                EACH_TERM_COLOR(GEN_STR)
        };
#undef GEN_STR
        return ansi[static_cast<unsigned int>(color)];
    }
    return "";
}

static std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> bufs;
    bufs.emplace_back();
    for(auto ch : str) {
        if(ch == '\n') {
            bufs.emplace_back();
        } else {
            bufs.back() += ch;
        }
    }
    return bufs;
}

static void formatErrorLine(bool isatty, const Lexer &lexer, Token errorToken) {
    errorToken = lexer.shiftEOS(errorToken);
    Token lineToken = lexer.getLineToken(errorToken);
    auto line = lexer.toTokenText(lineToken);
    auto marker = lexer.formatLineMarker(lineToken, errorToken);

    auto lines = split(line);
    auto markers = split(marker);
    unsigned int size = lines.size();
    assert(size == markers.size());
    for(unsigned int i = 0; i < size; i++) {
        // print error line
        fprintf(stderr, "%s%s%s\n", color(TermColor::Cyan, isatty),
                lines[i].c_str(), color(TermColor::Reset, isatty));

        // print line marker
        fprintf(stderr, "%s%s%s%s\n", color(TermColor::Green, isatty), color(TermColor::Bold, isatty),
                markers[i].c_str(), color(TermColor::Reset, isatty));
    }

    fflush(stderr);
}

DSError FrontEnd::handleError(DSErrorKind type, const char *errorKind,
                           Token errorToken, const std::string &message) const {
    auto &lexer = *this->parser.getLexer();
    unsigned int errorLineNum = lexer.getSourceInfoPtr()->getLineNum(errorToken.pos);
    const bool isatty = isSupportedTerminal(STDERR_FILENO);

    /**
     * show error message
     */
    fprintf(stderr, "%s:%d:%s%s ",
            lexer.getSourceInfoPtr()->getSourceName().c_str(), errorLineNum,
            color(TermColor::Magenta, isatty), color(TermColor::Bold, isatty));
    fprintf(stderr, "[%s error] %s%s\n",
            type == DS_ERROR_KIND_PARSE_ERROR ? "syntax" : "semantic",
            color(TermColor::Reset, isatty), message.c_str());
    formatErrorLine(isatty, lexer, errorToken);

    return {
            .kind = type,
            .lineNum = errorLineNum,
            .name = errorKind
    };
}

struct NodeWrapper {
    Node *ptr;

    explicit NodeWrapper(std::unique_ptr<Node> &&ptr) : ptr(ptr.release()) {}

    ~NodeWrapper() {
        delete this->ptr;
    }

    std::unique_ptr<Node> release() {
        Node *old = nullptr;
        std::swap(this->ptr, old);
        return std::unique_ptr<Node>(old);
    }
};

std::unique_ptr<Node> FrontEnd::operator()(DSError *dsError) {
    // parse
    auto node = this->parser();
    if(this->parser.hasError()) {
        auto e = this->handleParseError();
        if(dsError != nullptr) {
            *dsError = e;
        }
        return node;
    }
    if(this->uastDumper) {
        this->uastDumper(*node);
    }

    if(this->mode == DS_EXEC_MODE_PARSE_ONLY) {
        return node;
    }

    // typecheck
    try {
        NodeWrapper wrap(std::move(node));
        this->prevType = this->checker(this->prevType, wrap.ptr);
        node = wrap.release();

        if(this->astDumper) {
            this->astDumper(*node);
        }
    } catch(const TypeCheckError &e) {
        auto ret = this->handleTypeError(e);
        if(dsError != nullptr) {
            *dsError = ret;
        }
        return nullptr;
    }
    return node;
}

void FrontEnd::setupASTDump() {
    if(this->uastDumper) {
        this->uastDumper.initialize("### dump untyped AST ###");
    }
    if(this->mode == DS_EXEC_MODE_PARSE_ONLY) {
        return;
    }
    if(this->astDumper) {
        this->astDumper.initialize("### dump typed AST ###");
    }
}

void FrontEnd::teardownASTDump() {
    const auto &srcInfo = this->parser.getLexer()->getSourceInfoPtr();
    unsigned int varNum = this->checker.getSymbolTable().getMaxVarIndex();
    unsigned int gvarNum = this->checker.getSymbolTable().getMaxGVarIndex();

    if(this->uastDumper) {
        this->uastDumper.finalize(srcInfo, varNum, gvarNum);
    }
    if(this->mode == DS_EXEC_MODE_PARSE_ONLY) {
        return;
    }
    if(this->astDumper) {
        this->astDumper.finalize(srcInfo, varNum, gvarNum);
    }
}

} // namespace ydsh