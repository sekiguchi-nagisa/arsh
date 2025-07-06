/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#ifndef ARSH_TOKEN_EDIT_H
#define ARSH_TOKEN_EDIT_H

#include <string>

#include "misc/result.hpp"

namespace arsh {

class LineBuffer;

struct TokenizerResult;

struct MoveOrDeleteTokenParam {
  bool left; // if true, edit left/prev/backward token. otherwise, edit right/next/forward token
  bool move; // if true, move cursor. otherwise, delete bytes
};

/**
 *
 * @param buf
 * @param param
 * @param capture
 * @param cache
 * maybe null, after call it, modify content
 * @return
 */
Optional<bool> moveCursorOrDeleteToken(LineBuffer &buf, MoveOrDeleteTokenParam param,
                                       std::string *capture, TokenizerResult *cache);

inline Optional<bool> moveCursorToLeftByToken(LineBuffer &buf, TokenizerResult *cache = nullptr) {
  return moveCursorOrDeleteToken(buf, {.left = true, .move = true}, nullptr, cache);
}

inline Optional<bool> moveCursorToRightByToken(LineBuffer &buf, TokenizerResult *cache = nullptr) {
  return moveCursorOrDeleteToken(buf, {.left = false, .move = true}, nullptr, cache);
}

inline Optional<bool> deletePrevToken(LineBuffer &buf, std::string *capture,
                                      TokenizerResult *cache = nullptr) {
  return moveCursorOrDeleteToken(buf, {.left = true, .move = false}, capture, cache);
}

inline Optional<bool> deleteNextToken(LineBuffer &buf, std::string *capture,
                                      TokenizerResult *cache = nullptr) {
  return moveCursorOrDeleteToken(buf, {.left = false, .move = false}, capture, cache);
}

} // namespace arsh

#endif // ARSH_TOKEN_EDIT_H
