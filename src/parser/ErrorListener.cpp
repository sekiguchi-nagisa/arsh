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

#include <iostream>

#include "ErrorListener.h"
#include "misc/debug.h"

namespace ydsh {
namespace parser {

// #################################
// ##     CommonErrorListener     ##
// #################################

void CommonErrorListener::handleTypeError(const std::string &sourceName,
                                          const TypeCheckError &e) const {
    std::cerr << sourceName << ":" << e.getLineNum() << ": [semantic error] "
    << e.getMessage() << std::endl;
}

static std::ostream &format(std::ostream &stream, const ParseError &e) {
#define EACH_ERROR(E) \
    E(TokenMismatchedError) \
    E(NoViableAlterError) \
    E(InvalidTokenError) \
    E(OutOfRangeNumError) \
    E(UnexpectedNewLineError)

#define DISPATCH(E) if(dynamic_cast<const E *>(&e) != nullptr) { \
    stream << *static_cast<const E *>(&e); return stream; }

    EACH_ERROR(DISPATCH)

    fatal("unsupported parse error kind\n");
    return stream;

#undef DISPATCH
#undef EACH_ERROR
}

static std::ostream &formatErrorLine(std::ostream &stream, Lexer &lexer, const Token &errorToken) {
    Token lineToken = lexer.getLineToken(errorToken, true);
    stream << lexer.toTokenText(lineToken) << std::endl;
    stream << lexer.formatLineMarker(lineToken, errorToken);
    return stream;
}

void CommonErrorListener::handleParseError(Lexer &lexer,
                                           const std::string &sourceName, const ParseError &e) const {
    std::cerr << sourceName << ":" << e.getLineNum() << ": [syntax error] ";
    format(std::cerr, e) << std::endl;
    formatErrorLine(std::cerr, lexer, e.getErrorToken()) << std::endl;
}

} // namespace parser
} // namespace ydsh