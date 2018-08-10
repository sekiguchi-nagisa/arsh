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
#include <cerrno>

#include <unistd.h>

#include "frontend.h"
#include "constant.h"
#include "core.h"

namespace ydsh {

FrontEnd::FrontEnd(const char *scriptDir, Lexer &&lexer, SymbolTable &symbolTable,
                   DSExecMode mode, bool toplevel, const DumpTarget &target) :
        scriptDir(scriptDir), lexer(std::move(lexer)), mode(mode),
        parser(this->lexer), checker(symbolTable, toplevel),
        uastDumper(target.fps[DS_DUMP_KIND_UAST], symbolTable),
        astDumper(target.fps[DS_DUMP_KIND_AST], symbolTable) {
}

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

/**
 * not allow dumb terminal
 */
static bool isSupportedTerminal(int fd) {
    const char *term = getenv(ENV_TERM);
    return term != nullptr && strcasecmp(term, "dumb") != 0 && isatty(fd) != 0;
}

struct ColorController {
    bool isatty;

    explicit ColorController(int fd) : isatty(isSupportedTerminal(fd)) {}

    const char *operator()(TermColor color) const {
        if(this->isatty) {
#define GEN_STR(E, C) "\033[" #C "m",
            const char *ansi[] = {
                    EACH_TERM_COLOR(GEN_STR)
            };
#undef GEN_STR
            return ansi[static_cast<unsigned int>(color)];
        }
        return "";
    }
};

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

static void formatErrorLine(ColorController cc, const Lexer &lexer, Token errorToken) {
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
        fprintf(stderr, "%s%s%s\n", cc(TermColor::Cyan), lines[i].c_str(), cc(TermColor::Reset));

        // print line marker
        fprintf(stderr, "%s%s%s%s\n", cc(TermColor::Green), cc(TermColor::Bold),
                markers[i].c_str(), cc(TermColor::Reset));
    }

    fflush(stderr);
}

void printError(const Lexer &lex, const char *kind, Token token,
               ColorController cc, TermColor color, const char *message) {
    unsigned int lineNum = lex.getSourceInfoPtr()->getLineNum(token.pos);
    fprintf(stderr, "%s:%d: ", lex.getSourceInfoPtr()->getSourceName().c_str(), lineNum);
    fprintf(stderr, "%s%s[%s]%s %s\n",
            cc(color), cc(TermColor::Bold), kind, cc(TermColor::Reset), message);
    formatErrorLine(cc, lex, token);
}

DSError FrontEnd::handleError(DSErrorKind type, const char *errorKind,
                              Token errorToken, const std::string &message) const {
    ColorController cc(STDERR_FILENO);

    /**
     * show error message
     */
    printError(*this->parser.getLexer(),
               type == DS_ERROR_KIND_PARSE_ERROR ? "syntax error" : "semantic error",
               errorToken, cc, TermColor::Magenta, message.c_str());

    for(int i = static_cast<int>(this->contexts.size()) - 1; i > -1; i--) {
        Token token = this->contexts[i].sourceNode->getPathNode()->getToken();
        auto &lex = i > 0 ? this->contexts[i - 1].lexer : this->lexer;
        printError(lex, "note", token, cc, TermColor::Blue, "at module import");
    }

    unsigned int errorLineNum = this->getSourceInfo()->getLineNum(errorToken.pos);
    const char *sourceName = this->getSourceInfo()->getSourceName().c_str();
    return {
            .kind = type,
            .fileName = strdup(sourceName),
            .lineNum = errorLineNum,
            .name = errorKind
    };
}

std::unique_ptr<Node> FrontEnd::tryToParse(DSError *dsError) {
    std::unique_ptr<Node> node;
    if(this->parser) {
        node = this->parser();
        if(this->parser.hasError()) {
            auto e = this->handleParseError();
            if(dsError != nullptr) {
                *dsError = e;
            } else {
                DSError_release(&e);
            }
        }
        if(this->uastDumper) {
            this->uastDumper(*node);
        }
    }
    return node;
}

void FrontEnd::tryToCheckType(std::unique_ptr<Node> &node) {
    if(this->mode == DS_EXEC_MODE_PARSE_ONLY) {
        return;
    }

    node = this->checker(this->prevType, std::move(node));
    this->prevType = &node->getType();

    if(this->astDumper) {
        this->astDumper(*node);
    }
}

std::pair<std::unique_ptr<Node>, FrontEnd::Status> FrontEnd::operator()(DSError *dsError) {
    // parse
    auto node = this->tryToParse(dsError);
    if(this->parser.hasError()) {
        return {nullptr, IN_MODULE};
    }

    // typecheck
    try {
        auto s = this->tryToCheckModule(node);
        if(s != IN_MODULE) {
            return {nullptr, s};
        }

        this->tryToCheckType(node);
    } catch(const TypeCheckError &e) {
        auto ret = this->handleTypeError(e);
        if(dsError != nullptr) {
            *dsError = ret;
        } else {
            DSError_release(&ret);
        }
        return {nullptr, IN_MODULE};
    }

    auto s = IN_MODULE;
    if(node->is(NodeKind::Source) && static_cast<SourceNode&>(*node).isFirstAppear()) {
        s = EXIT_MODULE;
    }
    return {std::move(node), s};
}

void FrontEnd::setupASTDump() {
    if(this->uastDumper) {
        this->uastDumper.initialize(this->getSourceInfo()->getSourceName(), "### dump untyped AST ###");
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper.initialize(this->getSourceInfo()->getSourceName(), "### dump typed AST ###");
    }
}

void FrontEnd::teardownASTDump() {
    if(this->uastDumper) {
        this->uastDumper.finalize();
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper.finalize();
    }
}

FrontEnd::Status FrontEnd::tryToCheckModule(std::unique_ptr<Node> &node) {
    if(!node) {
        node = this->exitModule();
        return IN_MODULE;
    }

    if(!node->is(NodeKind::Source)) {
        return IN_MODULE;
    }

    auto &srcNode = static_cast<SourceNode&>(*node);
    const char *modPath = srcNode.getPathStr().c_str();
    FilePtr filePtr;
    auto ret = this->checker.getSymbolTable().tryToLoadModule(this->getCurScriptDir().c_str(), modPath, filePtr);
    if(is<ModLoadingError>(ret)) {
        switch(get<ModLoadingError>(ret)) {
        case ModLoadingError::CIRCULAR:
            RAISE_TC_ERROR(CircularMod, *srcNode.getPathNode(), modPath);
        case ModLoadingError::UNRESOLVED:
            RAISE_TC_ERROR(UnresolvedMod, *srcNode.getPathNode(), srcNode.getPathStr().c_str(), strerror(errno));
        }
    } else if(is<const char *>(ret)) {
        this->enterModule(get<const char*>(ret), std::move(filePtr),
                          std::unique_ptr<SourceNode>(static_cast<SourceNode *>(node.release())));
        return ENTER_MODULE;
    } else if(is<ModType *>(ret)) {
        srcNode.setModType(*get<ModType *>(ret));
        return IN_MODULE;
    }
    return IN_MODULE;   // normally unreachable, due to suppress gcc warning
}

void FrontEnd::enterModule(const char *fullPath, FilePtr &&filePtr, std::unique_ptr<SourceNode> &&node) {
    {
        assert(*fullPath == '/');
        Lexer lex(fullPath, filePtr.release());
        node->setFirstAppear(true);
        auto state = this->parser.saveLexicalState();
        auto scope = this->checker.getSymbolTable().createModuleScope();
        this->contexts.emplace_back(std::move(lex), std::move(scope), std::move(state), std::move(node));
    }
    Token token{};
    TokenKind kind = this->contexts.back().lexer.nextToken(token);
    TokenKind ckind{};
    this->parser.restoreLexicalState(this->contexts.back().lexer, kind, token, ckind);

    if(this->uastDumper) {
        this->uastDumper.enterModule(fullPath);
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper.enterModule(fullPath);
    }
}

std::unique_ptr<SourceNode> FrontEnd::exitModule() {
    auto &symbolTable = this->checker.getSymbolTable();
    auto &ctx = this->contexts.back();
    TokenKind kind = ctx.kind;
    Token token = ctx.token;
    TokenKind consumedKind = ctx.consumedKind;
    std::unique_ptr<SourceNode> node(ctx.sourceNode.release());
    auto &modType = symbolTable.createModType(ctx.lexer.getSourceInfoPtr()->getSourceName());
    auto scope = std::move(ctx.scope);
    this->contexts.pop_back();

    auto &lex = this->contexts.empty() ? this->lexer : this->contexts.back().lexer;
    this->parser.restoreLexicalState(lex, kind, token, consumedKind);
    if(this->contexts.empty()) {
        symbolTable.resetCurModule();
    } else {
        symbolTable.setModuleScope(*this->contexts.back().scope);
    }

    if(this->mode != DS_EXEC_MODE_PARSE_ONLY) {
        unsigned int varNum = scope->getMaxVarIndex();
        node->setMaxVarNum(varNum);
        node->setModType(modType);
        if(prevType != nullptr && this->prevType->isNothingType()) {
            this->prevType = &symbolTable.get(TYPE::Void);
            node->setNothing(true);
        }
    }

    if(this->uastDumper) {
        this->uastDumper.leaveModule();
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper.leaveModule();
    }
    return node;
}

} // namespace ydsh
