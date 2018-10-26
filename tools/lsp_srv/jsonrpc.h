/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef TOOLS_JSONRPC_H
#define TOOLS_JSONRPC_H

#include "validate.h"

namespace rpc {

using namespace json;

// Error Code definition
constexpr int ParseError     = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams  = -32602;
constexpr int InternalError  = -32603;

JSON newError(int code, const char *message, JSON &&data);

JSON newError(int code, const char *message);

} // namespace rpc

#endif //TOOLS_JSONRPC_H
