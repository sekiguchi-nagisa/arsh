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

const char* ErrorReporter::color(TermColor c) const {
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

void ErrorReporter::printErrorLine(const Lexer &lexer, Token token) const {
    if(token.pos + token.size == 0) {
        return;
    }

    Token errorToken = lexer.shiftEOS(token);
    Token lineToken = lexer.getLineToken(errorToken);
    auto line = lexer.formatTokenText(lineToken);
    auto marker = lexer.formatLineMarker(lineToken, errorToken);

    auto lines = split(line);
    auto markers = split(marker);
    size_t size = lines.size();
    assert(size == markers.size());
    bool omitLine = size > 30;
    std::pair<size_t, size_t> pairs[2] = {
            {0, omitLine ? 15 : size},
            {omitLine ? size - 10 : size, size}
    };
    for(unsigned int i = 0; i < 2; i++) {
        if(i == 1 && omitLine) {
            fprintf(this->fp, "%s%s%s\n", this->color(TermColor::Yellow), "\n| ~~~ omit error lines ~~~ |\n",
                    this->color(TermColor::Reset));
        }

        size_t start = pairs[i].first;
        size_t stop = pairs[i].second;
        for(size_t index = start; index < stop; index++) {
            // print error line
            fprintf(this->fp, "%s%s%s\n", this->color(TermColor::Cyan), lines[index].c_str(), this->color(TermColor::Reset));

            // print line marker
            fprintf(this->fp, "%s%s%s%s\n", this->color(TermColor::Green), this->color(TermColor::Bold),
                    markers[index].c_str(), this->color(TermColor::Reset));
        }
    }
    fflush(this->fp);
}

// ######################
// ##     FrontEnd     ##
// ######################

FrontEnd::FrontEnd(ModuleLoader &loader, Lexer &&lexer, TypePool &pool,
                   IntrusivePtr<NameScope> builtin, IntrusivePtr<NameScope> scope,
                   DSExecMode mode, bool toplevel,
                   ObserverPtr<CodeCompletionHandler> ccHandler) :
        modLoader(loader), builtin(builtin), mode(mode), checker(pool, toplevel, nullptr){
    this->contexts.push_back(std::make_unique<Context>(std::move(lexer), std::move(scope), ccHandler));
    this->curScope()->clearLocalSize();
    this->checker.setLexer(this->getCurrentLexer());
    this->checker.setCodeCompletionHandler(ccHandler);
}

static const char *toString(DSErrorKind kind) {
    switch(kind) {
    case DS_ERROR_KIND_PARSE_ERROR:
        return "syntax error";
    case DS_ERROR_KIND_TYPE_ERROR:
        return "semantic error";
    case DS_ERROR_KIND_CODEGEN_ERROR:
        return "codegen error";
    default:
        return "";
    }
}

void FrontEnd::handleError(DSErrorKind type, const char *errorKind,
        Token errorToken, const char *message, DSError *dsError) const {
    if(!this->reporter) {
        return;
    }

    /**
     * show error message
     */
    this->reporter(this->getCurrentLexer(), toString(type), errorToken, TermColor::Magenta, message);

    auto end = this->contexts.crend();
    for(auto iter = this->contexts.crbegin() + 1; iter != end; ++iter) {
        auto &node = (*iter)->srcListNode;
        Token token = node->getPathNode().getToken();
        auto &lex = (*iter)->lexer;
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
    if(this->parser()) {
        node = this->parser()();
        if(this->parser().hasError()) {
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
        node = this->checker(this->prevType, std::move(node), this->curScope());
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
            if(this->parser().hasError()) {
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
            auto &src = cast<SourceListNode>(*ret.node);
            if(!src.getPathList().empty()) {
                this->getCurSrcListNode().reset(cast<SourceListNode>(ret.node.release()));
                continue;
            }
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
        this->uastDumper->finalize(*this->curScope());
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper->finalize(*this->curScope());
    }
}

static auto createModLoadingError(const Node &node, const char *path, ModLoadingError e) {
    if(e.isCircularLoad()) {
        return createTCError<CircularMod>(node, path);
    } else if(e.isFileNotFound()) {
        return createTCError<NotFoundMod>(node, path);
    } else {
        return createTCError<NotOpenMod>(node, path, strerror(e.getErrNo()));
    }
}

FrontEnd::Ret FrontEnd::loadModule(DSError *dsError) {
    if(!this->hasUnconsumedPath()) {
        this->getCurSrcListNode().reset();
        return {nullptr, IN_MODULE};
    }

    auto &node = *this->getCurSrcListNode();
    unsigned int pathIndex = node.getCurIndex();
    const char *modPath = node.getPathList()[pathIndex].c_str();
    node.setCurIndex(pathIndex + 1);
    FilePtr filePtr;
    auto ret = this->modLoader.load(
            node.getPathNode().getType().is(TYPE::String) ? this->getCurScriptDir() : nullptr,
            modPath, filePtr, ModLoadOption::IGNORE_NON_REG_FILE);
    if(is<ModLoadingError>(ret)) {
        auto e = get<ModLoadingError>(ret);
        if(e.isFileNotFound() && node.isOptional()) {
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
    } else if(is<unsigned int>(ret)) {
        auto &type = this->getTypePool().get(get<unsigned int>(ret));
        assert(type.isModType());
        return {node.create(static_cast<ModType&>(type), false), IN_MODULE};
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
        auto scope = this->modLoader.createGlobalScopeFromFullpath(fullPath, this->builtin);
        this->contexts.push_back(
                std::make_unique<Context>(std::move(lex), std::move(scope), nullptr));
    }
    this->checker.setLexer(this->getCurrentLexer());

    if(this->uastDumper) {
        this->uastDumper->enterModule(fullPath);
    }
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY && this->astDumper) {
        this->astDumper->enterModule(fullPath);
    }
}

std::unique_ptr<SourceNode> FrontEnd::exitModule() {
    assert(!this->contexts.empty());
    auto &ctx = *this->contexts.back();
    auto &modType = this->modLoader.createModType(this->getTypePool(), *ctx.scope, ctx.lexer.getSourceName());
    const unsigned int varNum = ctx.scope->getMaxLocalVarIndex();
    this->contexts.pop_back();
    this->checker.setLexer(this->getCurrentLexer());

    auto node = this->getCurSrcListNode()->create(modType, true);
    if(this->mode != DS_EXEC_MODE_PARSE_ONLY) {
        node->setMaxVarNum(varNum);
        if(prevType != nullptr && this->prevType->isNothingType()) {
            this->prevType = &this->getTypePool().get(TYPE::Void);
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
