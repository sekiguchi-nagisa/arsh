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
#include "misc/resource.hpp"

namespace ydsh {

#include <yycond.h>

const char *toModeName(LexerMode mode);


class SourceInfo : public RefCount<SourceInfo> {
private:
    std::string sourceName;

    /**
     * default value is 1.
     */
    unsigned int lineNumOffset{1};

    /**
     * contains newline character position.
     */
    std::vector<unsigned int> lineNumTable;

public:
    explicit SourceInfo(const char *sourceName) : sourceName(sourceName) { }
    ~SourceInfo() = default;

    const std::string &getSourceName() const {
        return this->sourceName;
    }

    void setLineNumOffset(unsigned int offset) {
        this->lineNumOffset = offset;
    }

    unsigned int getLineNumOffset() const {
        return this->lineNumOffset;
    }

    const std::vector<unsigned int> &getLineNumTable() const {
        return this->lineNumTable;
    }

    void addNewlinePos(unsigned int pos);
    unsigned int getLineNum(unsigned int pos) const;
};

using SourceInfoPtr = IntrusivePtr<SourceInfo>;

class Lexer : public ydsh::parser_base::LexerBase {
private:
    SourceInfoPtr srcInfoPtr;

    /**
     * default mode is yycSTMT
     */
    std::vector<LexerMode> modeStack;

    bool prevNewLine{false};

    /**
     * only available in command mode.
     */
    bool prevSpace{false};

    LexerMode prevMode{yycSTMT};

public:
    NON_COPYABLE(Lexer);

    Lexer() = default;

    /**
     *
     * @param sourceName
     * must not be null.
     * @param source
     * must be null terminated.
     * @return
     */
    Lexer(const char *sourceName, const char *source) : Lexer(sourceName, source, strlen(source)) {}

    /**
     *
     * @param sourceName
     * must not be null
     * @param source
     * must not be null
     * @param size
     * @return
     */
    Lexer(const char *sourceName, const char *source, unsigned int size) :
            LexerBase(source, size), srcInfoPtr(SourceInfoPtr::create(sourceName)), modeStack(1, yycSTMT) {}

    /**
     * 
     * @param sourceName
     * must not be null.
     * @param fp
     * must be opened with binary mode.
     * @return
     */
    Lexer(const char *sourceName, FILE *fp) :
            LexerBase(fp), srcInfoPtr(SourceInfoPtr::create(sourceName)), modeStack(1, yycSTMT) {}

    Lexer(Lexer &&lex) noexcept :
            LexerBase(std::move(lex)), srcInfoPtr(std::move(lex.srcInfoPtr)),
            modeStack(std::move(lex.modeStack)), prevNewLine(lex.prevNewLine),
            prevSpace(lex.prevSpace), prevMode(lex.prevMode) {}

    ~Lexer() = default;

    Lexer &operator=(Lexer &&lex) noexcept {
        auto tmp(std::move(lex));
        this->swap(lex);
        return *this;
    }

    void swap(Lexer &lex) {
        LexerBase::swap(lex);
        std::swap(this->srcInfoPtr, lex.srcInfoPtr);
        std::swap(this->modeStack, lex.modeStack);
        std::swap(this->prevNewLine, lex.prevNewLine);
        std::swap(this->prevSpace, lex.prevSpace);
        std::swap(this->prevMode, lex.prevMode);
    }

    void setPos(unsigned int pos) {
        assert(this->buf.get() + pos <= this->limit);
        this->cursor = this->buf.get() + pos;
    }

    const SourceInfoPtr &getSourceInfoPtr() const {
        return this->srcInfoPtr;
    }

    void setLineNum(unsigned int lineNum) {
        this->srcInfoPtr->setLineNumOffset(lineNum);
    }

    unsigned int getLineNum() const {
        return this->srcInfoPtr->getLineNumOffset() +
               this->srcInfoPtr->getLineNumTable().size();
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
        this->modeStack.back() = mode;
    }

    void pushLexerMode(LexerMode mode) {
        this->modeStack.push_back(mode);
    }

    void popLexerMode() {
        this->modeStack.pop_back();
    }

    LexerMode getLexerMode() const {
        return this->modeStack.back();
    }

    unsigned int lexerModeSize() const {
        return this->modeStack.size();
    }

    static bool isInvalidToken(TokenKind kind) {
        return kind == INVALID;
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
    unsigned char toUint8(Token token, int &status) const;
    short toInt16(Token token, int &status) const;
    unsigned short toUint16(Token token, int &status) const;
    int toInt32(Token token, int &status) const;
    unsigned int toUint32(Token token, int &status) const;
    long toInt64(Token token, int &status) const;
    unsigned long toUint64(Token token, int &status) const;

    /**
     * if converted number is out of range, status is 1.
     */
    double toDouble(Token token, int &status) const;

private:
    bool isDecimal(Token token) const;

    void updateNewline(unsigned int pos) {
        const unsigned int stopPos = this->getPos();
        for(unsigned int i = pos; i < stopPos; ++i) {
            if(this->buf[i] == '\n') {
                this->srcInfoPtr->addNewlinePos(i);
            }
        }
    }
};

} // namespace ydsh

#endif //YDSH_LEXER_H
