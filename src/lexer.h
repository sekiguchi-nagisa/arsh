/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef YDSH_LEXER_H
#define YDSH_LEXER_H

#include <cstdlib>
#include <cstdint>

#include "token_kind.h"
#include "misc/lexer_base.hpp"

namespace ydsh {

#include <yycond.h>

const char *toModeName(LexerMode mode);


class Lexer : public ydsh::LexerBase {
private:
    CStrPtr scriptDir;

    std::vector<LexerMode> modeStack;

    LexerMode curMode{yycSTMT};

    LexerMode prevMode{yycSTMT};

    bool prevNewLine{false};

    /**
     * only available in command mode.
     */
    bool prevSpace{false};

    /**
     * if true, enable code completion (may emit complete token)
     */
    bool complete{false};

    TokenKind compTokenKind{TokenKind::INVALID};

public:
    NON_COPYABLE(Lexer);

    Lexer(Lexer&&) = default;

    Lexer() = default;

    Lexer(const char *sourceName, ByteBuffer &&buf, CStrPtr &&scriptDir) :
            LexerBase(sourceName, std::move(buf)), scriptDir(std::move(scriptDir)) {
        if(!this->scriptDir || *this->scriptDir == '\0') {
            this->scriptDir.reset(strdup("."));
        }
    }

    ~Lexer() = default;

    /**
     *
     * @return
     * not null
     */
    const char *getScriptDir() const {
        return this->scriptDir.get();
    }

    void setPos(unsigned int pos) {
        assert(this->buf.data() + pos <= this->limit);
        this->cursor = this->buf.data() + pos;
    }

    bool isPrevNewLine() const {
        return this->prevNewLine;
    }

    bool isPrevSpace() const {
        return this->prevSpace;
    }

    LexerMode getPrevMode() const {
        return this->prevMode;
    }

    void setLexerMode(LexerMode mode) {
        this->curMode = mode;
    }

    void pushLexerMode(LexerMode mode) {
        this->modeStack.push_back(this->curMode);
        this->setLexerMode(mode);
    }

    void popLexerMode() {
        if(!this->modeStack.empty()) {
            this->curMode = this->modeStack.back();
            this->modeStack.pop_back();
        }
    }

    LexerMode getLexerMode() const {
        return this->curMode;
    }

    void setComplete(bool allow) {
        this->complete = allow;
    }

    bool isComplete() const {
        return this->complete;
    }

    bool inCompletionPoint() const {
        return this->complete && this->cursor + 1 == this->limit;
    }

    void setCompTokenKind(TokenKind kind) {
        this->compTokenKind = kind;
    }

    TokenKind getCompTokenKind() const {
        return this->compTokenKind;
    }

    /**
     * lexer entry point.
     * write next token to token.
     * return the kind of next token.
     */
    TokenKind nextToken(Token &token);

    // token to value converting api.

    /**
     * convert single quote string literal token to string.
     * if token is illegal format(ex. illegal escape sequence), return false.
     */
    bool singleToString(Token token, std::string &out) const;

    /**
     * convert escaped single quote string literal token to string.
     * if token is illegal format(ex. illegal escape sequence), return false.
     */
    bool escapedSingleToString(Token token, std::string &out) const;

    /**
     * convert double quote string element token to string.
     */
    std::string doubleElementToString(Token token) const;

    /**
     * convert token to command argument
     */
    std::string toCmdArg(Token token) const;

    /**
     * convert token to name(remove '$' char)
     * ex. $hoge, ${hoge}, hoge
     */
    std::string toName(Token token) const;

    /**
     * if converted number is out of range, status is 1.
     */
    int64_t toInt64(Token token, int &status) const;

    /**
     * if converted number is out of range, status is 1.
     */
    double toDouble(Token token, int &status) const;
};

} // namespace ydsh

#endif //YDSH_LEXER_H
