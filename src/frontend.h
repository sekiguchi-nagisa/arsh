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

    struct Ret {
        std::unique_ptr<Node> node;
        Status status;

        explicit operator bool() const {
            return this->node != nullptr || this->status != Status::IN_MODULE;
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
        std::unique_ptr<SourceNode> sourceNode;

        Context(Lexer &&lexer, ModuleScope &&scope,
                std::tuple<TokenKind, Token, TokenKind > &&state, std::unique_ptr<SourceNode> &&oldSourceNode) :
                lexer(std::move(lexer)), scope(std::move(scope)),
                kind(std::get<0>(state)), token(std::get<1>(state)),
                consumedKind(std::get<2>(state)), sourceNode(std::move(oldSourceNode)) {}
    };

    // root lexer state
    Lexer lexer;

    std::vector<std::unique_ptr<Context>> contexts;

    const DSExecMode mode;
    Parser parser;
    TypeChecker checker;
    DSType *prevType{nullptr};
    NodeDumper uastDumper;
    NodeDumper astDumper;

public:
    FrontEnd(Lexer &&lexer, SymbolTable &symbolTable,
             DSExecMode mode, bool toplevel, const DumpTarget &target);

    ~FrontEnd() {
        this->getSymbolTable().clear();
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
    const std::string &getCurScriptDir() const {
        return (this->contexts.empty() ? this->lexer : this->contexts.back()->lexer).getScriptDir();
    }

    std::unique_ptr<Node> tryToParse(DSError *dsError);

    bool tryToCheckType(std::unique_ptr<Node> &node, DSError *dsError);

    /**
     * if module loading failed, throw TypeCheckError
     * @param node
     * after call it, will be null
     * @return
     */
    Result<Status, std::unique_ptr<TypeCheckError>> tryToCheckModule(std::unique_ptr<Node> &node);

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
