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

struct DumpTarget;

class FrontEnd {
public:
    enum Status : unsigned char {
        IN_MODULE,
        ENTER_MODULE,
        EXIT_MODULE,
    };

private:
    struct Context {
        std::string scriptDir;
        Lexer lexer;
        std::unique_ptr<ModuleScope> scope;

        // for saving old state
        TokenKind kind;
        Token token;
        TokenKind consumedKind;
        std::unique_ptr<SourceNode> sourceNode;

        Context(Lexer &&lexer, std::unique_ptr<ModuleScope> &&scope,
                std::tuple<TokenKind, Token, TokenKind > &&state, std::unique_ptr<SourceNode> &&oldSourceNode) :
                lexer(std::move(lexer)), scope(std::move(scope)),
                kind(std::get<0>(state)), token(std::get<1>(state)),
                consumedKind(std::get<2>(state)), sourceNode(std::move(oldSourceNode)) {
            const char *fileName = this->lexer.getSourceInfo()->getSourceName().c_str();
            const char *ptr = strrchr(fileName, '/');
            this->scriptDir.append(fileName, ptr == fileName ? 1 : ptr - fileName);
        }
    };

    const std::string scriptDir;

    // root lexer state
    Lexer lexer;

    std::vector<Context> contexts;

    const DSExecMode mode;
    Parser parser;
    TypeChecker checker;
    DSType *prevType{nullptr};
    NodeDumper uastDumper;
    NodeDumper astDumper;

    bool ignoreNotFoundMod; // ignore 'NotFoundMod' error

public:
    FrontEnd(const char *scriptDir, Lexer &&lexer, SymbolTable &symbolTable,
             DSExecMode mode, bool toplevel, const DumpTarget &target, bool ignore);

    ~FrontEnd() {
        this->getSymbolTable().clear();
    }

    SymbolTable &getSymbolTable() {
        return this->checker.getSymbolTable();
    }

    const SourceInfo &getCurrentSourceInfo() const {
        return this->parser.getLexer()->getSourceInfo();
    }

    bool frontEndOnly() const {
        return this->mode == DS_EXEC_MODE_PARSE_ONLY || this->mode == DS_EXEC_MODE_CHECK_ONLY;
    }

    unsigned int getRootLineNum() const {
        return this->lexer.getLineNum();
    }

    explicit operator bool() const {
        return static_cast<bool>(this->parser) || !this->contexts.empty();
    }

    std::pair<std::unique_ptr<Node>, Status> operator()(DSError *dsError);

    void setupASTDump();

    void teardownASTDump();

private:
    const std::string getCurScriptDir() const {
        return this->contexts.empty() ? this->scriptDir : this->contexts.back().scriptDir;
    }

    std::unique_ptr<Node> tryToParse(DSError *dsError);

    void tryToCheckType(std::unique_ptr<Node> &node);

    /**
     * if module loading failed, throw TypeCheckError
     * @param node
     * after call it, will be null
     * @return
     */
    Status tryToCheckModule(std::unique_ptr<Node> &node);

    /**
     *
     * @param fullPath
     * must be full file path (not directory)
     * @param buf
     * @param node
     */
    void enterModule(const char *fullPath, ByteBuffer &&buf, std::unique_ptr<SourceNode> &&node);

    std::unique_ptr<SourceNode> exitModule();

    // for error reporting
    DSError handleError(DSErrorKind type, const char *errorKind,
                        Token errorToken, const std::string &message) const;

    DSError handleParseError() const {
        auto &e = this->parser.getError();
        Token errorToken = this->parser.getLexer()->shiftEOS(e.getErrorToken());
        return this->handleError(DS_ERROR_KIND_PARSE_ERROR, e.getErrorKind(), errorToken, e.getMessage());
    }

    DSError handleTypeError(const TypeCheckError &e) const {
        return this->handleError(DS_ERROR_KIND_TYPE_ERROR, e.getKind(), e.getToken(), e.getMessage());
    }

    bool suppressError(const char *kind) const {
        return this->ignoreNotFoundMod && strcmp(kind, NotFoundMod::kind) == 0;
    }
};

} // namespace ydsh

#endif //YDSH_FRONTEND_H
