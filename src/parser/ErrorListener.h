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

#ifndef PARSER_ERRORLISTENER_H_
#define PARSER_ERRORLISTENER_H_

#include <vector>

#include "TypeCheckError.h"
#include "Parser.h"

namespace ydsh {
namespace parser {

struct ErrorListener {
    virtual ~ErrorListener() = default;

    virtual void handleTypeError(const std::string &sourceName,
                                 const TypeCheckError &e) const = 0;

    virtual void handleParseError(Lexer &lexer, const std::string &sourceName, const ParseError &e) const = 0;
};

class ProxyErrorListener : public ErrorListener {
private:
    std::vector<const ErrorListener *> listeners;

public:
    ProxyErrorListener() = default;
    ~ProxyErrorListener() = default;

    void handleTypeError(const std::string &sourceName, const TypeCheckError &e) const {    // override
        for(const ErrorListener *l : this->listeners) {
            l->handleTypeError(sourceName, e);
        }
    }

    void handleParseError(Lexer &lexer, const std::string &sourceName, const ParseError &e) const { // override
        for(const ErrorListener *l : this->listeners) {
            l->handleParseError(lexer, sourceName, e);
        }
    }

    void addListener(const ErrorListener *listener) {
        this->listeners.push_back(listener);
    }
};


class CommonErrorListener : public ErrorListener {
public:
    CommonErrorListener() = default;

    ~CommonErrorListener() = default;

    void handleTypeError(const std::string &sourceName,
                         const TypeCheckError &e) const; // override
    void handleParseError(Lexer &lexer, const std::string &sourceName, const ParseError &e) const; // override
};

} // namespace parser
} // namespace ydsh

#endif /* PARSER_ERRORLISTENER_H_ */
