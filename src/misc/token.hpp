/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef MISC_LIB_TOKEN_HPP
#define MISC_LIB_TOKEN_HPP

#include <cassert>
#include <string>

BEGIN_MISC_LIB_NAMESPACE_DECL

struct Token {
  unsigned int pos{0};
  unsigned int size{0};

  unsigned int endPos() const { return this->pos + this->size; }

  bool operator==(const Token &token) const {
    return this->pos == token.pos && this->size == token.size;
  }

  bool operator!=(const Token &token) const { return !(*this == token); }

  /**
   *
   * @param startIndex
   * inclusive
   * @param stopIndex
   * exclusive
   * @return
   */
  Token slice(unsigned int startIndex, unsigned int stopIndex) const {
    assert(startIndex <= stopIndex);
    assert(startIndex < this->size);
    assert(stopIndex <= this->size);

    Token newToken{this->pos, this->size};
    newToken.pos += startIndex;
    newToken.size = stopIndex - startIndex;
    return newToken;
  }

  Token sliceFrom(unsigned int startIndex) const { return this->slice(startIndex, this->size); }

  std::string str() const {
    std::string str = "(pos = ";
    str += std::to_string(this->pos);
    str += ", size = ";
    str += std::to_string(this->size);
    str += ")";
    return str;
  }
};

inline std::string toString(Token token) { return token.str(); }

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_TOKEN_HPP
