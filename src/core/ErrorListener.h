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

#ifndef YDSH_ERRORLISTENER_H
#define YDSH_ERRORLISTENER_H

#include <vector>
#include <ostream>
#include <memory>

#include "../parser/TypeCheckError.hpp"
#include "../parser/Parser.h"

namespace ydsh {
namespace core {

class DSValue;
class TypePool;

} // namespace core
} // namespace ydsh


namespace ydsh {

using namespace ydsh::parser;

struct ErrorListener {
    virtual ~ErrorListener() = default;

    virtual void handleParseError(Lexer &lexer,
                                  const std::string &sourceName, const ParseError &e) noexcept = 0;

    virtual void handleTypeError(const std::string &sourceName,
                                 const TypeCheckError &e) noexcept = 0;

    virtual void handleRuntimeError(const TypePool &pool, const DSValue &raisedObj) noexcept = 0;
};

class ProxyErrorListener : public ErrorListener {
private:
    std::vector<ErrorListener *> listeners;

public:
    ProxyErrorListener() = default;
    ~ProxyErrorListener() = default;

    void handleParseError(Lexer &lexer,
                          const std::string &sourceName, const ParseError &e) noexcept { // override
        for(ErrorListener *l : this->listeners) {
            l->handleParseError(lexer, sourceName, e);
        }
    }

    void handleTypeError(const std::string &sourceName, const TypeCheckError &e) noexcept {    // override
        for(ErrorListener *l : this->listeners) {
            l->handleTypeError(sourceName, e);
        }
    }

    void handleRuntimeError(const TypePool &pool, const DSValue &raisedObj) noexcept {  // override
        for(ErrorListener *l : this->listeners) {
            l->handleRuntimeError(pool, raisedObj);
        }
    }

    void addListener(ErrorListener * const listener) {
        this->listeners.push_back(listener);
    }
};


class CommonErrorListener : public ErrorListener {
private:
    /**
     *  not delete it.
     */
    std::ostream * const stream;

public:
    CommonErrorListener();
    explicit CommonErrorListener(std::ostream &stream);

    ~CommonErrorListener() = default;

    void handleParseError(Lexer &lexer,
                          const std::string &sourceName, const ParseError &e) noexcept; // override
    void handleTypeError(const std::string &sourceName,
                         const TypeCheckError &e) noexcept; // override
    void handleRuntimeError(const TypePool &pool, const DSValue &raisedObj) noexcept;    // override
};

class ReportingListener : public ErrorListener {
private:
    /**
     * for error location
     */
    unsigned int lineNum;

    /**
     * parse error or type error
     */
    const char *messageKind;

public:
    ReportingListener() : lineNum(0), messageKind("") { }
    ~ReportingListener() = default;

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    const char *getMessageKind() const {
        return this->messageKind;
    }

    void handleParseError(Lexer &lexer,
                          const std::string &sourceName, const ParseError &e) noexcept; // override
    void handleTypeError(const std::string &sourceName,
                         const TypeCheckError &e) noexcept; // override
    void handleRuntimeError(const TypePool &pool, const DSValue &raisedObj) noexcept;    // override
};

} // namespace ydsh

#endif //YDSH_ERRORLISTENER_H
