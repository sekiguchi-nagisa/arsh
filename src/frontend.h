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

class FrontEnd {
private:
    DSExecMode mode;
    Parser parser;
    TypeChecker checker;
    DSType *prevType{nullptr};

public:
    FrontEnd(Lexer &lexer, TypePool &pool, SymbolTable &symbolTable, DSExecMode mode, bool toplevel) :
            mode(mode), parser(lexer), checker(pool, symbolTable, toplevel) {
        this->checker.reset();
    }

    bool frontEndOnly() const {
        return this->mode == DS_EXEC_MODE_PARSE_ONLY || this->mode == DS_EXEC_MODE_CHECK_ONLY;
    }

    operator bool() const {
        return static_cast<bool>(this->parser);
    }

    std::unique_ptr<Node> operator()(DSError *dsError);

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
