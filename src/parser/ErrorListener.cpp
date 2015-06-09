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

#include "ErrorListener.h"

namespace ydsh {
namespace parser {

// #################################
// ##     CommonErrorListener     ##
// #################################

void CommonErrorListener::displayTypeError(const std::string &sourceName,
                                           const TypeCheckError &e) const {
    unsigned int argSize = e.getArgs().size();
    unsigned int messageSize = e.getTemplate().size() + 1;
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

static std::string format(Lexer &lexer, const TokenMismatchedError &e) {
    std::string message = "mismatch token: ";
    message += TO_NAME(e.getTokenKind());
    message += ", expect for ";
    message += TO_NAME(e.getExpectedKind());
    return message;
}

static std::string format(Lexer &lexer, const NoViableAlterError &e) {
    std::string message = "no viable alternative: ";
    message += TO_NAME(e.getTokenKind());
    message += ",\n";
    message += "expect for ";
    unsigned int size = e.getAlters().size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            message += ", ";
        }
        message += TO_NAME(e.getAlters()[i]);
    }
    return message;
}

static std::string format(Lexer &lexer, const InvalidTokenError &e) {
    std::string message = "invalid token: ";
    Token token = e.getErrorToken();
    message += lexer.toTokenText(token);
    return message;
}

static std::string format(Lexer &lexer, const OutOfRangeNumError &e) {
    std::string message = "out of range ";
    message += TO_NAME(e.getTokenKind());
    message += ": ";
    Token token = e.getErrorToken();
    message += lexer.toTokenText(token);
    return message;
}

static std::string format(Lexer &lexer, const UnexpectedNewLineError &e) {
    std::string message = "unexpected new line: ";
    Token token = e.getErrorToken();
    message += lexer.toTokenText(token);
    return message;
}

static std::string format(Lexer &lexer, const ParseError &e) {
#define EACH_ERROR(E) \
    E(TokenMismatchedError) \
    E(NoViableAlterError) \
    E(InvalidTokenError) \
    E(OutOfRangeNumError) \
    E(UnexpectedNewLineError)

#define DISPATCH(E) if(dynamic_cast<const E *>(&e) != nullptr) { \
    return format(lexer, *static_cast<const E *>(&e)); }

    EACH_ERROR(DISPATCH)

    return std::string();

#undef DISPATCH
#undef EACH_ERROR
}

static std::string formatErrorLine(Lexer &lexer, Token errorToken) {
    Token lineToken = lexer.getLineToken(errorToken, true);
    std::string line(lexer.toTokenText(lineToken));
    line += "\n";
    line += lexer.formatLineMarker(lineToken, errorToken);
    return line;
}

void CommonErrorListener::displayParseError(Lexer &lexer,
                                            const std::string &sourceName, const ParseError &e) const {
    std::string msg(format(lexer, e));
    std::string line(formatErrorLine(lexer, e.getErrorToken()));
    fprintf(stderr, "%s:%d: [syntax error] %s\n%s\n",
            sourceName.c_str(), e.getLineNum(), msg.c_str(), line.c_str());
}

} // namespace parser
} // namespace ydsh