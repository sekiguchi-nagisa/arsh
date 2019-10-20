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
        uastDumper(target.files[DS_DUMP_KIND_UAST].get(), symbolTable),
        astDumper(target.files[DS_DUMP_KIND_AST].get(), symbolTable) {
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

class ErrorReporter {
private:
    FILE *fp;
    bool tty;

public:
    explicit ErrorReporter(FILE *fp) : fp(fp), tty(isSupportedTerminal(fileno(fp))) {}

    void operator()(const Lexer &lex, const char *kind, Token token, TermColor c, const char *message) const {
        unsigned lineNumOffset = lex.getSourceInfo()->getLineNumOffset();
        fprintf(this->fp, "%s:", lex.getSourceInfo()->getSourceName().c_str());
        if(lineNumOffset > 0) {
            unsigned int lineNum = lex.getSourceInfo()->getLineNum(token.pos);
            fprintf(this->fp, "%d:", lineNum);
        }
        fprintf(this->fp, " %s%s[%s]%s %s\n",
                this->color(c), this->color(TermColor::Bold), kind, this->color(TermColor::Reset), message);

        if(lineNumOffset > 0) {
            this->printErrorLine(lex, token);
        }
        fflush(this->fp);
    }

private:
    const char *color(TermColor c) const {
        if(this->tty) {
#define GEN_STR(E, C) "\033[" #C "m",
            const char *ansi[] = {
                    EACH_TERM_COLOR(GEN_STR)
            };
#undef GEN_STR
            return ansi[static_cast<unsigned int>(c)];
        }
        return "";
    }

    void printErrorLine(const Lexer &lexer, Token token) const {
        if(token.pos + token.size == 0) {
            return;
        }

        Token errorToken = lexer.shiftEOS(token);
        Token lineToken = lexer.getLineToken(errorToken);
        auto line = lexer.formatTokenText(lineToken);
        auto marker = lexer.formatLineMarker(lineToken, errorToken);

        auto lines = split(line);
        auto markers = split(marker);
        unsigned int size = lines.size();
        assert(size == markers.size());
        for(unsigned int i = 0; i < size; i++) {
            // print error line
            fprintf(this->fp, "%s%s%s\n", this->color(TermColor::Cyan), lines[i].c_str(), this->color(TermColor::Reset));

            // print line marker
            fprintf(this->fp, "%s%s%s%s\n", this->color(TermColor::Green), this->color(TermColor::Bold),
                    markers[i].c_str(), this->color(TermColor::Reset));
        }

        fflush(this->fp);
    }
};

void FrontEnd::handleError(DSErrorKind type, const char *errorKind,
        Token errorToken, const std::string &message, DSError *dsError) const {
#ifdef FUZZING_BUILD_MODE
    bool ignore = getenv("YDSH_SUPPRESS_COMPILE_ERROR") != nullptr;
    FilePtr file(ignore ? fopen("/dev/null", "w") : fdopen(dup(STDERR_FILENO), "w"));
    ErrorReporter stream(file.get());
#else
    ErrorReporter stream(stderr);
#endif

    /**
     * show error message
     */
    stream(*this->parser.getLexer(),
           type == DS_ERROR_KIND_PARSE_ERROR ? "syntax error" : "semantic error",
           errorToken, TermColor::Magenta, message.c_str());

    for(int i = static_cast<int>(this->contexts.size()) - 1; i > -1; i--) {
        Token token = this->contexts[i].sourceNode->getPathNode()->getToken();
        auto &lex = i > 0 ? this->contexts[i - 1].lexer : this->lexer;
        stream(lex, "note", token, TermColor::Blue, "at module import");
    }

    unsigned int errorLineNum = this->getCurrentSourceInfo()->getLineNum(errorToken.pos);
    const char *sourceName = this->getCurrentSourceInfo()->getSourceName().c_str();
    if(dsError) {
        *dsError = {
                .kind = type,
                .fileName = strdup(sourceName),
                .lineNum = errorLineNum,
                .name = strdup(errorKind)
        };
    }
}

std::unique_ptr<Node> FrontEnd::tryToParse(DSError *dsError) {
    std::unique_ptr<Node> node;
    if(this->parser) {
        node = this->parser();
        if(this->parser.hasError()) {
            this->handleParseError(dsError);
        } else if(this->uastDumper) {
            this->uastDumper(*node);
        }
    }
    return node;
}

bool FrontEnd::tryToCheckType(std::unique_ptr<Node> &node, DSError *dsError) {
    if(this->mode == DS_EXEC_MODE_PARSE_ONLY) {
        return true;
    }

    try {
        node = this->checker(this->prevType, std::move(node));
        this->prevType = &node->getType();

        if(this->astDumper) {
            this->astDumper(*node);
        }
        return true;
    } catch(const TypeCheckError &e) {
        this->handleTypeError(e, dsError);
        return false;
    }
}

std::pair<std::unique_ptr<Node>, FrontEnd::Status> FrontEnd::operator()(DSError *dsError) {
    // parse
    auto node = this->tryToParse(dsError);
    if(this->parser.hasError()) {
        return {nullptr, IN_MODULE};
    }

    // load module
    auto s = IN_MODULE;
    auto ret = this->tryToCheckModule(node);
    if(!ret) {
        this->handleTypeError(*ret.asErr(), dsError);
        return {nullptr, IN_MODULE};
    }
    s = ret.take();
    if(s != IN_MODULE) {
        return {nullptr, s};
    }

    // check type
    if(!this->tryToCheckType(node, dsError)) {
        return {nullptr, IN_MODULE};
    }

    if(node->is(NodeKind::Source) && static_cast<SourceNode&>(*node).isFirstAppear()) {
        s = EXIT_MODULE;
    }
    return {std::move(node), s};
}

void FrontEnd::setupASTDump() {
    if(this->uastDumper) {
        this->uastDumper.initialize(this->getCurrentSourceInfo()->getSourceName(), "### dump untyped AST ###");
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper.initialize(this->getCurrentSourceInfo()->getSourceName(), "### dump typed AST ###");
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

static ErrHolder<std::unique_ptr<TypeCheckError>> wrap(TypeCheckError &&e) {
    auto ret = std::make_unique<TypeCheckError>();
    *ret = std::move(e);
    return Err(std::move(ret));
}

Result<FrontEnd::Status, std::unique_ptr<TypeCheckError>>
FrontEnd::tryToCheckModule(std::unique_ptr<Node> &node) {
    if(!node) {
        node = this->exitModule();
        return Ok(IN_MODULE);
    }

    if(!node->is(NodeKind::Source)) {
        return Ok(IN_MODULE);
    }

    auto &srcNode = static_cast<SourceNode&>(*node);
    bool optional = srcNode.isOptional();
    const char *modPath = srcNode.getPathStr().c_str();
    FilePtr filePtr;
    auto ret = this->getSymbolTable().tryToLoadModule(this->getCurScriptDir().c_str(), modPath, filePtr);
    if(is<ModLoadingError>(ret)) {
        switch(get<ModLoadingError>(ret)) {
        case ModLoadingError::CIRCULAR:
            return wrap(createTCError<CircularMod>(*srcNode.getPathNode(), modPath));
        case ModLoadingError::NOT_OPEN:
            return wrap(createTCError<NotOpenMod>(
                    *srcNode.getPathNode(), srcNode.getPathStr().c_str(), strerror(errno)));
        case ModLoadingError::NOT_FOUND:
            if(optional) {
                return Ok(IN_MODULE);
            }
            return wrap(createTCError<NotFoundMod>(
                    *srcNode.getPathNode(), srcNode.getPathStr().c_str()));
        }
    } else if(is<const char *>(ret)) {
        ByteBuffer buf;
        if(!readAll(filePtr, buf)) {
            return wrap(createTCError<NotOpenMod>(
                    *srcNode.getPathNode(), srcNode.getPathStr().c_str(), strerror(errno)));
        }
        this->enterModule(get<const char*>(ret), std::move(buf),
                          std::unique_ptr<SourceNode>(static_cast<SourceNode *>(node.release())));
        return Ok(ENTER_MODULE);
    } else if(is<ModType *>(ret)) {
        srcNode.setModType(*get<ModType *>(ret));
        return Ok(IN_MODULE);
    }
    return Ok(IN_MODULE);   // normally unreachable, due to suppress gcc warning
}

void FrontEnd::enterModule(const char *fullPath, ByteBuffer &&buf, std::unique_ptr<SourceNode> &&node) {
    {
        assert(*fullPath == '/');
        Lexer lex(fullPath, std::move(buf));
        node->setFirstAppear(true);
        auto state = this->parser.saveLexicalState();
        auto scope = this->getSymbolTable().createModuleScope();
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
    auto &ctx = this->contexts.back();
    TokenKind kind = ctx.kind;
    Token token = ctx.token;
    TokenKind consumedKind = ctx.consumedKind;
    std::unique_ptr<SourceNode> node(ctx.sourceNode.release());
    auto &modType = this->getSymbolTable().createModType(ctx.lexer.getSourceInfo()->getSourceName());
    auto scope = std::move(ctx.scope);
    this->contexts.pop_back();

    auto &lex = this->contexts.empty() ? this->lexer : this->contexts.back().lexer;
    this->parser.restoreLexicalState(lex, kind, token, consumedKind);
    if(this->contexts.empty()) {
        this->getSymbolTable().resetCurModule();
    } else {
        this->getSymbolTable().setModuleScope(*this->contexts.back().scope);
    }

    if(this->mode != DS_EXEC_MODE_PARSE_ONLY) {
        unsigned int varNum = scope->getMaxVarIndex();
        node->setMaxVarNum(varNum);
        node->setModType(modType);
        if(prevType != nullptr && this->prevType->isNothingType()) {
            this->prevType = &this->getSymbolTable().get(TYPE::Void);
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
