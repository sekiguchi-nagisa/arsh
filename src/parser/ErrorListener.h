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

#include "TypeCheckError.h"
#include "Parser.h"

namespace ydsh {
namespace parser {

struct ErrorListener {
    virtual ~ErrorListener() = default;

    virtual void displayTypeError(const std::string &sourceName,
                                  const TypeCheckError &e) const = 0;

    virtual void displayParseError(Lexer &lexer, const std::string &sourceName, const ParseError &e) const = 0;
};

class CommonErrorListener : public ErrorListener {
public:
    CommonErrorListener() = default;

    ~CommonErrorListener() = default;

    void displayTypeError(const std::string &sourceName,
                          const TypeCheckError &e) const; // override
    void displayParseError(Lexer &lexer, const std::string &sourceName, const ParseError &e) const; // override
};

} // namespace parser
} // namespace ydsh

#endif /* PARSER_ERRORLISTENER_H_ */
