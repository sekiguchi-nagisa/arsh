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
    FILE     = 1u << 0u,    /* complete file names */
    DIR      = 1u << 1u,    /* complete directory names */
    COMMAND  = 1u << 2u,    /* complete command names including user-defined, builtin, external */
    BUILTIN  = 1u << 3u,    /* complete builtin command names */
    UDC      = 1u << 4u,    /* complete user-defined command names */
    GVAR     = 1u << 5u,    /* complete global variable names (not start with $) */
    ENV      = 1u << 6u,    /* complete environmental variable names */
    SIGNAL   = 1u << 7u,    /* complete signal names (not start with SIG) */
    USER     = 1u << 8u,    /* complete user names */
    GROUP    = 1u << 9u,    /* complete group names */
    MODULE   = 1u << 10u,   /* complete module path */
    KEYWORD  = 1u << 11u,   /* complete keyword */
};

template <> struct allow_enum_bitop<CodeCompOp> : std::true_type {};

class CodeCompletionHandler {
private:
    DSState &state;

    std::string symbolPrefix;

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
