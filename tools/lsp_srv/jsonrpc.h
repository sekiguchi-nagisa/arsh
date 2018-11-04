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

struct ResponseBase {
    virtual ~ResponseBase() = default;

    /**
     * convert to serialized response json
     * after call it, will be empty
     * @return
     */
    virtual std::string serialize(int tab = 0) = 0;
};

struct Response : public ResponseBase {
    JSON id;    // must be `number | string'
    JSON result;

    /**
     *
     * @param id
     * must not be null
     * @param result
     */
    Response(JSON &&id, JSON &&result) : id(std::move(id)), result(std::move(result)) {}

    JSON convertToJSON();

    std::string serialize(int tab) override;
};

struct ResponseError : public ResponseBase {
    int code;
    std::string message;
    JSON data;

    ResponseError(int code, std::string &&message, JSON &&data) :
            code(code), message(std::move(message)), data(std::move(data)) {}

    ResponseError(int code, std::string &&message) : ResponseError(code, std::move(message), JSON()) {}

    ResponseError() : code(0) {}

    /**
     * after call it, will be empty object.
     * @return
     */
    JSON convertToJSON();

    std::string serialize(int tab) override;
};

// Error Code definition
constexpr int ParseError     = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams  = -32602;
constexpr int InternalError  = -32603;


struct Request {
    enum Kind : int {
        SUCCESS = 0,
        PARSE_ERROR = ParseError,
        INVALID = InvalidRequest,
    } kind;

    JSON id;    // optional. must be `number | string'
    std::string method; // if error, indicate error message
    JSON params;    // optional. must be `array<any> | object'

    /**
     * each param must be validated
     * @param id
     * @param method
     * @param params
     */
    Request(JSON &&id, std::string &&method, JSON &&params) :
        kind(SUCCESS), id(std::move(id)), method(std::move(method)), params(std::move(params)) {}

    /**
     *
     * @param kind
     * must not be SUCCESS
     * @param error
     */
    Request(Kind kind, std::string &&error) : kind(kind), method(std::move(error)) {}

    bool isError() const {
        return this->kind != SUCCESS;
    }

    bool isNotification() const {
        return !this->isError() && this->id.isInvalid();
    }

    bool isRequest() const {
        return !this->isError() && !this->id.isInvalid();
    }

    static ResponseError asError(Request &&req) {
        assert(req.isError());
        return ResponseError(static_cast<int>(req.kind), std::move(req.method));
    }

    /**
     * convert to request json.
     * object must be notification or request.
     * after call it, will be empty.
     * @return
     */
    JSON convertToJSON();   // for testing
};



struct RequestParser : public Parser {
    RequestParser() : Parser() {}

    void append(const char *text) {
        this->append(text, strlen(text));
    }

    void append(const char *data, unsigned int size) {
        this->lexer->appendToBuf(reinterpret_cast<const unsigned char *>(data), size, false);
    }

    Request operator()();
};

} // namespace rpc

#endif //TOOLS_JSONRPC_H
