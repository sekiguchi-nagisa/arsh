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
private:
    struct Context {
        Lexer lexer;
        ModuleScope scope;

        // for saving old state
        TokenKind kind;
        Token token;
        std::unique_ptr<SourceNode> sourceNode;

        Context(Lexer &&lexer, ModuleScope &&scope, TokenKind oldKind,
                Token oldToken, SourceNode *oldSourceNode) :
                lexer(std::move(lexer)), scope(std::move(scope)),
                kind(oldKind), token(oldToken) {
            this->sourceNode.reset(oldSourceNode);
        }
    };

    // root lexer state
    Lexer lexer;

    std::vector<Context> contexts;

    DSExecMode mode;
    Parser parser;
    TypeChecker checker;
    DSType *prevType{nullptr};
    NodeDumper uastDumper;
    NodeDumper astDumper;

public:
    FrontEnd(Lexer &&lexer, SymbolTable &symbolTable,
             DSExecMode mode, bool toplevel, const DumpTarget &target);

    ~FrontEnd() {
        this->checker.getSymbolTable().clear();
    }

    const SourceInfoPtr &getSourceInfo() const {
        return this->parser.getLexer()->getSourceInfoPtr();
    }

    bool frontEndOnly() const {
        return this->mode == DS_EXEC_MODE_PARSE_ONLY || this->mode == DS_EXEC_MODE_CHECK_ONLY;
    }

    unsigned int lineNum() const {
        return this->parser.getLexer()->getLineNum();
    }

    explicit operator bool() const {
        return static_cast<bool>(this->parser) || !this->contexts.empty();
    }

    std::unique_ptr<Node> operator()(DSError *dsError);

    void setupASTDump();

    void teardownASTDump();

private:
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
};

} // namespace ydsh

#endif //YDSH_FRONTEND_H
