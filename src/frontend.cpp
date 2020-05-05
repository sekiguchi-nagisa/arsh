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
#include "core.h"

namespace ydsh {

// ###########################
// ##     ErrorReporter     ##
// ###########################

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

ErrorReporter::ErrorReporter(FILE *fp, bool close) :
        fp(fp), close(close), tty(isSupportedTerminal(fileno(fp))) {}

ErrorReporter::~ErrorReporter() {
    if(this->close) {
        fclose(this->fp);
    }
}

void ErrorReporter::operator()(const Lexer &lex, const char *kind, Token token, TermColor c, const char *message) const {
    unsigned lineNumOffset = lex.getLineNumOffset();
    fprintf(this->fp, "%s:", lex.getSourceName().c_str());
    if(lineNumOffset > 0) {
        unsigned int lineNum = lex.getLineNumByPos(token.pos);
        fprintf(this->fp, "%d:", lineNum);
    }
    fprintf(this->fp, " %s%s[%s]%s %s\n",
            this->color(c), this->color(TermColor::Bold), kind, this->color(TermColor::Reset), message);

    if(lineNumOffset > 0) {
        this->printErrorLine(lex, token);
    }
    fflush(this->fp);
}

const char* ErrorReporter::color(ydsh::TermColor c) const {
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

void ErrorReporter::printErrorLine(const ydsh::Lexer &lexer, ydsh::Token token) const {
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

// ######################
// ##     FrontEnd     ##
// ######################

FrontEnd::FrontEnd(Lexer &&lexer, SymbolTable &symbolTable, DSExecMode mode, bool toplevel) :
        lexer(std::move(lexer)), mode(mode),
        parser(this->lexer), checker(symbolTable, toplevel){}

void FrontEnd::handleError(DSErrorKind type, const char *errorKind,
        Token errorToken, const std::string &message, DSError *dsError) const {
    if(!this->reporter) {
        return;
    }

    /**
     * show error message
     */
    this->reporter(*this->parser.getLexer(),
           type == DS_ERROR_KIND_PARSE_ERROR ? "syntax error" : "semantic error",
           errorToken, TermColor::Magenta, message.c_str());

    for(int i = static_cast<int>(this->contexts.size()) - 1; i > -1; i--) {
        auto &node = i > 0 ? this->contexts[i - 1]->srcListNode : this->srcListNode;
        Token token = node->getPathNode().getToken();
        auto &lex = i > 0 ? this->contexts[i - 1]->lexer : this->lexer;
        this->reporter(lex, "note", token, TermColor::Blue, "at module import");
    }

    unsigned int errorLineNum = this->getCurrentLexer().getLineNumByPos(errorToken.pos);
    const char *sourceName = this->getCurrentLexer().getSourceName().c_str();
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

FrontEnd::Ret FrontEnd::operator()(DSError *dsError) {
    do {
        // load module
        Ret ret = this->loadModule(dsError);
        if(!ret || ret.status == ENTER_MODULE) {
            return ret;
        }

        // parse
        if(!ret.node) {
            ret.node = this->tryToParse(dsError);
            if(this->parser.hasError()) {
                return {nullptr, FAILED};
            }
        }

        if(!ret.node) { // when parse reach EOS
            ret.node = this->exitModule();
        }

        // check type
        if(!this->tryToCheckType(ret.node, dsError)) {
            return {nullptr, FAILED};
        }

        if(isa<SourceListNode>(*ret.node)) {
            this->getCurSrcListNode().reset(cast<SourceListNode>(ret.node.release()));
            continue;
        }

        if(isa<SourceNode>(*ret.node) && cast<SourceNode>(*ret.node).isFirstAppear()) {
            ret.status = EXIT_MODULE;
        }
        return ret;
    } while(true);
}

void FrontEnd::setupASTDump() {
    if(this->uastDumper) {
        this->uastDumper->initialize(this->getCurrentLexer().getSourceName(), "### dump untyped AST ###");
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper->initialize(this->getCurrentLexer().getSourceName(), "### dump typed AST ###");
    }
}

void FrontEnd::teardownASTDump() {
    if(this->uastDumper) {
        this->uastDumper->finalize();
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper->finalize();
    }
}

static auto createModLoadingError(const Node &node, const char *path, ModLoadingError e) {
    switch(e) {
    case ModLoadingError::CIRCULAR:
        return createTCError<CircularMod>(node, path);
    case ModLoadingError::NOT_OPEN:
        return createTCError<NotOpenMod>(node, path, strerror(errno));
    default:
        assert(e == ModLoadingError::NOT_FOUND);
        return createTCError<NotFoundMod>(node, path);
    }
}

FrontEnd::Ret FrontEnd::loadModule(DSError *dsError) {
    if(!this->getCurSrcListNode()) {
        return {nullptr, IN_MODULE};
    }

    if(!this->getCurSrcListNode()->hasUnconsumedPath()) {
        this->getCurSrcListNode().reset();
        return {nullptr, IN_MODULE};
    }

    auto &node = *this->getCurSrcListNode();
    unsigned int pathIndex = node.getCurIndex();
    const char *modPath = node.getPathList()[pathIndex].c_str();
    node.setCurIndex(pathIndex + 1);
    FilePtr filePtr;
    auto ret = this->getSymbolTable().tryToLoadModule(this->getCurScriptDir(), modPath, filePtr);
    if(is<ModLoadingError>(ret)) {
        auto e = get<ModLoadingError>(ret);
        if(e == ModLoadingError::NOT_FOUND && node.isOptional()) {
            return {std::make_unique<EmptyNode>(), IN_MODULE};
        }
        auto error = createModLoadingError(node.getPathNode(), modPath, e);
        this->handleTypeError(error, dsError);
        return {nullptr, FAILED};
    } else if(is<const char *>(ret)) {
        ByteBuffer buf;
        if(!readAll(filePtr, buf)) {
            auto e = createTCError<NotOpenMod>(node.getPathNode(), modPath, strerror(errno));
            this->handleTypeError(e, dsError);
            return {nullptr, FAILED};
        }
        this->enterModule(get<const char*>(ret), std::move(buf));
        return {nullptr, ENTER_MODULE};
    } else if(is<ModType *>(ret)) {
        return {node.create(*get<ModType *>(ret), false), IN_MODULE};
    }
    return {nullptr, FAILED};
}

static Lexer createLexer(const char *fullPath, ByteBuffer &&buf) {
    assert(*fullPath == '/');
    char *path = strdup(fullPath);
    const char *ptr = strrchr(path, '/');
    path[ptr == path ? 1 : ptr - path] = '\0';
    return Lexer(fullPath, std::move(buf), CStrPtr(path));
}

void FrontEnd::enterModule(const char *fullPath, ByteBuffer &&buf) {
    {
        auto lex = createLexer(fullPath, std::move(buf));
        auto state = this->parser.saveLexicalState();
        auto scope = this->getSymbolTable().createModuleScope();
        this->contexts.push_back(
                std::make_unique<Context>(std::move(lex), std::move(scope), std::move(state)));
        this->getSymbolTable().setModuleScope(this->contexts.back()->scope);
    }
    Token token{};
    TokenKind kind = this->contexts.back()->lexer.nextToken(token);
    TokenKind ckind{};
    this->parser.restoreLexicalState(this->contexts.back()->lexer, kind, token, ckind);

    if(this->uastDumper) {
        this->uastDumper->enterModule(fullPath);
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper->enterModule(fullPath);
    }
}

std::unique_ptr<SourceNode> FrontEnd::exitModule() {
    auto &ctx = *this->contexts.back();
    TokenKind kind = ctx.kind;
    Token token = ctx.token;
    TokenKind consumedKind = ctx.consumedKind;
    auto &modType = this->getSymbolTable().createModType(ctx.lexer.getSourceName());
    const unsigned int varNum = ctx.scope.getMaxVarIndex();
    this->contexts.pop_back();

    auto &lex = this->contexts.empty() ? this->lexer : this->contexts.back()->lexer;
    this->parser.restoreLexicalState(lex, kind, token, consumedKind);
    if(this->contexts.empty()) {
        this->getSymbolTable().resetCurModule();
    } else {
        this->getSymbolTable().setModuleScope(this->contexts.back()->scope);
    }

    auto node = this->getCurSrcListNode()->create(modType, true);
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY) {
        node->setMaxVarNum(varNum);
        if(prevType != nullptr && this->prevType->isNothingType()) {
            this->prevType = &this->getSymbolTable().get(TYPE::Void);
            node->setNothing(true);
        }
    }

    if(this->uastDumper) {
        this->uastDumper->leaveModule();
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper->leaveModule();
    }
    return node;
}

} // namespace ydsh
