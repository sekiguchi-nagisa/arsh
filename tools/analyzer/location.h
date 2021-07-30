/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_ANALYZER_LOCATION_H
#define YDSH_TOOLS_ANALYZER_LOCATION_H

#include <misc/token.hpp>

#include "lsp.h"

namespace ydsh::lsp {

/**
 *
 * @param content
 * may not be terminated with newline
 * @param position
 * @return
 */
Optional<unsigned int> toTokenPos(const std::string &content, const Position &position);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param pos
 * @return
 */
Optional<Position> toPosition(const std::string &content, unsigned int pos);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param range
 * @return
 */
Optional<ydsh::Token> toToken(const std::string &content, const Range &range);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param token
 * @return
 */
Optional<Range> toRange(const std::string &content, Token token);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_LOCATION_H
