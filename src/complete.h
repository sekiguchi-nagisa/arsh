/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_COMPLETE_H
#define YDSH_COMPLETE_H

#include "misc/string_ref.hpp"
#include "misc/enum_util.hpp"
#include "misc/resource.hpp"

struct DSState;

namespace ydsh {

class ArrayObject;

enum class CodeCompOp : unsigned int {
    FILE     = 1u << 0u,    /* complete file names (including directory) */
    DIR      = 1u << 1u,    /* complete directory names (directory only) */
    EXEC     = 1u << 2u,    /* complete executable file names (including directory) */
    TILDE    = 1u << 3u,    /* perform tilde expansion before completions */
    EXTERNAL = 1u << 4u,    /* complete external command names */
    BUILTIN  = 1u << 5u,    /* complete builtin command names */
    UDC      = 1u << 6u,    /* complete user-defined command names */
    GVAR     = 1u << 7u,    /* complete global variable names (not start with $) */
    ENV      = 1u << 8u,    /* complete environmental variable names */
    SIGNAL   = 1u << 9u,    /* complete signal names (not start with SIG) */
    USER     = 1u << 10u,   /* complete user names */
    GROUP    = 1u << 11u,   /* complete group names */
    MODULE   = 1u << 12u,   /* complete module path */
    STMT_KW  = 1u << 13u,   /* complete statement keyword */
    EXPR_KW  = 1u << 14u,   /* complete expr keyword */
    COMMAND  = EXTERNAL | BUILTIN | UDC,
};

template <> struct allow_enum_bitop<CodeCompOp> : std::true_type {};

class CodeCompletionHandler {
private:
    DSState &state;

    std::string symbolPrefix;

    /**
     * if empty, use cwd
     */
    std::string scriptDir;  // for module completion

    CodeCompOp compOp{};

public:
    explicit CodeCompletionHandler(DSState &state) : state(state) {}

    void addCompRequest(CodeCompOp op, StringRef prefix) {
        this->compOp = op;
        this->symbolPrefix = prefix.toString();
    }

    void invoke(ArrayObject &results);
};

/**
 * perform completion
 * @param st
 * @param ref
 * @param option
 * @return
 * return size of completion result. (equivalent to size of $COMPREPLY)
 */
unsigned int doCodeCompletion(DSState &st, StringRef ref, CodeCompOp option = {});

} // namespace ydsh

#endif //YDSH_COMPLETE_H
