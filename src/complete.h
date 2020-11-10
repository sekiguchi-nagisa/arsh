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

#include <vector>

#include "misc/string_ref.hpp"
#include "misc/enum_util.hpp"
#include "misc/resource.hpp"

#include "parser.h"
#include "symbol_table.h"

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
    ARG_KW   = 1u << 15u,   /* complete command argument keyword */
    EXPECT   = 1u << 16u,   /* complete expetced token */
    HOOK     = 1u << 17u,   /* for user-defined completion hook */
    COMMAND  = EXTERNAL | BUILTIN | UDC,
};

template <> struct allow_enum_bitop<CodeCompOp> : std::true_type {};

inline bool isKeyword(StringRef value) {
    return !value.startsWith("<") || !value.endsWith(">");
}

class CodeCompletionHandler {
private:
    DSState &state;

    ObserverPtr<const Lexer> lex;

    /**
     * if empty, use cwd
     */
    std::string scriptDir;  // for module completion

    /**
     * current completion word
     */
    std::string compWord;

    /**
     * for expected tokens
     */
    std::vector<std::string> extraWords;

    /**
     * for COMP_HOOK
     */
    std::unique_ptr<CmdNode> cmdNode;

    CodeCompOp compOp{};

    /**
     * whem result of COMP_HOOK is empty, fallback to file name completion
     */
    CodeCompOp fallbackOp{};

public:
    explicit CodeCompletionHandler(DSState &state) : state(state) {}

    void setLexer(const Lexer &lexer) {
        this->lex.reset(&lexer);
    }

    void addCompRequest(CodeCompOp op, std::string &&word) {
        this->compOp = op;
        this->compWord = std::move(word);
    }

    void addExpectedTokenRequest(TokenKind kind) {
        const char *value = toString(kind);
        if(isKeyword(value)) {
            this->compOp = CodeCompOp::EXPECT;
            this->extraWords.emplace_back(value);
        }
    }

    template <unsigned int N>
    void addExpectedTokenRequests(const TokenKind (&kinds)[N]) {
        this->addExpectedTokenRequests(N, kinds);
    }
    void addExpectedTokenRequests(unsigned int size, const TokenKind *kinds) {
        for(unsigned int i = 0; i < size; i++) {
            this->addExpectedTokenRequest(kinds[i]);
        }
    }

    void addVarNameRequest(Token token);

    void addCmdOrKeywordRequest(Token token, bool isStmt) {
        this->addCmdRequest(token);
        CodeCompOp kwOp = isStmt ? CodeCompOp::STMT_KW : CodeCompOp::EXPR_KW;
        setFlag(this->compOp, kwOp);
    }

    void addCmdArgOrModRequest(Token token, CmdArgParseOpt opt);

    void addCompHookRequest(std::unique_ptr<CmdNode> &&node) {
        this->fallbackOp = this->compOp;
        this->compOp = CodeCompOp::HOOK;
        this->cmdNode = std::move(node);
    }

    bool hasCompRequest() const {
        return !empty(this->compOp);
    }

    CodeCompOp getCompOp() const {
        return this->compOp;
    }

    void invoke(ArrayObject &results);

private:
    void addCmdRequest(Token token);
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
