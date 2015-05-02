/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include <parser/ErrorListener.h>

namespace ydsh {
namespace parser {

// ###########################
// ##     ErrorListener     ##
// ###########################

ErrorListener::ErrorListener() {
}

ErrorListener::~ErrorListener() {
}

// #################################
// ##     CommonErrorListener     ##
// #################################

CommonErrorListener::CommonErrorListener() {
}

CommonErrorListener::~CommonErrorListener() {
}

void CommonErrorListener::displayTypeError(const std::string &sourceName,
                                           const TypeCheckError &e) const {
    int argSize = e.getArgs().size();
    int messageSize = e.getTemplate().size() + 1;
    for(const std::string &arg : e.getArgs()) {
        messageSize += arg.size();
    }

    // format error message
    char strBuf[messageSize];
    switch(argSize) {
    case 0:
        snprintf(strBuf, messageSize, e.getTemplate().c_str());
        break;
    case 1:
        snprintf(strBuf, messageSize, e.getTemplate().c_str(), e.getArgs()[0].c_str());
        break;
    case 2:
        snprintf(strBuf, messageSize, e.getTemplate().c_str(),
                 e.getArgs()[0].c_str(), e.getArgs()[1].c_str());
        break;
    case 3:
        snprintf(strBuf, messageSize, e.getTemplate().c_str(),
                 e.getArgs()[0].c_str(), e.getArgs()[1].c_str(), e.getArgs()[2].c_str());
        break;
    default:
        snprintf(strBuf, messageSize, "!!broken args!!");
        break;
    }

    fprintf(stderr, "%s:%d: [semantic error] %s\n", sourceName.c_str(), e.getLineNum(), strBuf);
}

class ParseErrorFormatter : public ParseErrorVisitor {
private:
    /**
     * not call destrucutor.
     */
    Lexer<LexerDef, TokenKind> *lexer;

    std::string message;

    ParseErrorFormatter(Lexer<LexerDef, TokenKind> *lexer);

public:
    ~ParseErrorFormatter();

    void visit(const TokenMismatchError &e); // override
    void visit(const NoViableAlterError &e); // override
    void visit(const InvalidTokenError &e); // override
    void visit(const OutOfRangeNumError &e); // override

    static std::string format(Lexer<LexerDef, TokenKind> &lexer, const ParseError &e);
};

ParseErrorFormatter::ParseErrorFormatter(Lexer<LexerDef, TokenKind> *lexer) :
        lexer(lexer), message() {
}

ParseErrorFormatter::~ParseErrorFormatter() {
}

void ParseErrorFormatter::visit(const TokenMismatchError &e) {
    this->message += "mismatch token: ";
    this->message += TO_NAME(e.getTokenKind());
    this->message += ", expect for ";
    this->message += TO_NAME(e.getExpectedTokenKind());
}

void ParseErrorFormatter::visit(const NoViableAlterError &e) {
    this->message += "no viable alternative: ";
    this->message += TO_NAME(e.getTokenKind());
    this->message += ",\n";
    this->message += "expect for ";
    unsigned int size = e.getAlters().size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            this->message += ", ";
        }
        this->message += TO_NAME(e.getAlters()[i]);
    }
}

void ParseErrorFormatter::visit(const InvalidTokenError &e) {
    this->message += "invalid token: ";
    Token token = e.getErrorToken();
    this->message += this->lexer->toTokenText(token);
}

void ParseErrorFormatter::visit(const OutOfRangeNumError &e) {
    this->message += "out of range ";
    this->message += TO_NAME(e.getTokenKind());
    this->message += ": ";
    Token token = e.getErrorToken();
    this->message += this->lexer->toTokenText(token);
}

std::string ParseErrorFormatter::format(Lexer<LexerDef, TokenKind> &lexer, const ParseError &e) {
    ParseErrorFormatter f(&lexer);
    e.accept(f);
    return f.message;
}

static std::string formatErrorLine(Lexer<LexerDef, TokenKind> &lexer, Token errorToken) {
    Token lineToken = lexer.getLineToken(errorToken, true);
    std::string line(lexer.toTokenText(lineToken));
    line += "\n";
    for(unsigned int i = lineToken.startPos; i < errorToken.startPos; i++) {
        line += " ";
    }
    for(unsigned int i = 0; i < errorToken.size; i++) {
        line += "^";    //TODO: support multi byte char
    }
    return line;
}

void CommonErrorListener::displayParseError(Lexer<LexerDef, TokenKind> &lexer,
                                            const std::string &sourceName, const ParseError &e) const {
    std::string msg(ParseErrorFormatter::format(lexer, e));
    std::string line(formatErrorLine(lexer, e.getErrorToken()));
    fprintf(stderr, "%s:%d: [syntax error] %s\n%s\n",
            sourceName.c_str(), e.getLineNum(), msg.c_str(), line.c_str());
}

} // namespace parser
} // namespace ydsh