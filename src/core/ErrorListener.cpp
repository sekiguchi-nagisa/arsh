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
#include "../misc/term.h"

namespace ydsh {
namespace core {

// #################################
// ##     CommonErrorListener     ##
// #################################

static std::ostream &format(std::ostream &stream, const ParseError &e) {
#define EACH_ERROR(E) \
    E(TokenMismatchedError) \
    E(NoViableAlterError) \
    E(InvalidTokenError) \
    E(OutOfRangeNumError)

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
    stream << misc::TermColor::Cyan << lexer.toTokenText(lineToken) << misc::reset << std::endl;
    stream << misc::TermColor::Green << lexer.formatLineMarker(lineToken, errorToken) << misc::reset;
    return stream;
}

void CommonErrorListener::handleParseError(Lexer &lexer, const ParseError &e) noexcept {
    this->stream << lexer.getSourceInfoPtr()->getSourceName() << ":" << e.getLineNum()
    << ":" << misc::TermColor::Magenta << " [syntax error] " << misc::reset;
    format(this->stream, e) << std::endl;
    formatErrorLine(this->stream, lexer, e.getErrorToken()) << std::endl;
}

void CommonErrorListener::handleTypeError(const std::string &sourceName,
                                          const TypeCheckError &e) noexcept {
    this->stream << sourceName << ":" << e.getLineNum() << ":"
    << misc::TermColor::Magenta << " [semantic error] " << misc::reset
    << e.getMessage() << std::endl;
}

void CommonErrorListener::handleRuntimeError(const TypePool &, const DSValue &) noexcept {
}// do nothing


// ################################
// ##     ProxyErrorListener     ##
// ################################

void ProxyErrorListener::handleParseError(Lexer &lexer, const ParseError &e) noexcept {
    for(ErrorListener *l : this->listeners) {
        l->handleParseError(lexer, e);
    }
}

void ProxyErrorListener::handleTypeError(const std::string &sourceName, const TypeCheckError &e) noexcept {
    for(ErrorListener *l : this->listeners) {
        l->handleTypeError(sourceName, e);
    }
}

void ProxyErrorListener::handleRuntimeError(const TypePool &pool, const DSValue &raisedObj) noexcept {
    for(ErrorListener *l : this->listeners) {
        l->handleRuntimeError(pool, raisedObj);
    }
}

void ProxyErrorListener::addListener(ErrorListener *const listener) {
    this->listeners.push_back(listener);
}


// ###############################
// ##     ReportingListener     ##
// ###############################

void ReportingListener::handleParseError(Lexer &, const ParseError &e) noexcept {
#define EACH_ERROR(E) \
    E(TokenMismatched  , 0) \
    E(NoViableAlter    , 1) \
    E(InvalidToken     , 2) \
    E(OutOfRangeNum    , 3)

    static const char *strs[] = {
#define GEN_STR(K, N) #K,
            EACH_ERROR(GEN_STR)
#undef GEN_STR
    };

#define DISPATCH(K, N) if(dynamic_cast<const K##Error *>(&e) != nullptr) { this->messageKind = strs[N]; }

    EACH_ERROR(DISPATCH)

#undef DISPATCH

    this->lineNum = e.getLineNum();

#undef EACH_ERROR
}

void ReportingListener::handleTypeError(const std::string &, const TypeCheckError &e) noexcept {
    this->lineNum = e.getLineNum();
    this->messageKind = e.getKind();
}

void ReportingListener::handleRuntimeError(const TypePool &pool, const DSValue &raisedObj) noexcept {
    if(!pool.getInternalStatus().isSameOrBaseTypeOf(*raisedObj->getType())) {
        this->messageKind = pool.getTypeName(*raisedObj->getType()).c_str();
    }

    if(dynamic_cast<Error_Object *>(raisedObj.get()) != nullptr) {
        Error_Object *obj = typeAs<Error_Object>(raisedObj);
        this->lineNum = getOccuredLineNum(obj->getStackTrace());
    }
}

} // namespace core
} // namespace ydsh