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

#include "jsonrpc.h"

namespace rpc {

JSON ResponseError::convertToJSON() {
    return {
            {"code", this->code},
            {"message", std::move(this->message)},
            {"data", std::move(this->data)}
    };
}

std::string ResponseError::serialize(int tab) {
    JSON json = {
            {"jsonrpc", "2.0"},
            {"id", nullptr},
            {"error", this->convertToJSON()}
    };
    return json.serialize(tab);
}

JSON Response::convertToJSON() {
    assert(this->id.isString() || this->id.isNumber());
    assert(!this->result.isInvalid());
    return {
            {"jsonrpc", "2.0"},
            {"id", std::move(this->id)},
            {"result", std::move(this->result)},
    };
}

std::string Response::serialize(int tab) {
    return this->convertToJSON().serialize(tab);
}

JSON Request::convertToJSON() {
    assert(!this->isError());
    return {
            {"jsonrpc", "2.0"},
            {"id", std::move(this->id)},
            {"method", std::move(this->method)},
            {"params", std::move(this->params)}
    };
}

Request RequestParser::operator()() {
    // finalize lexer
    unsigned char b[1];
    this->lexer->appendToBuf(b, 0, true);

    // parse
    auto ret = Parser::operator()();
    if(this->hasError()) {
        return Request(Request::PARSE_ERROR, this->formatError());
    }

    // validate
    const char *ifaceName = "Request";
    InterfaceMap map;
    map.interface(ifaceName)
            .field("id", number | string, false)
            .field("method", string)
            .field("params", array(any) | object(""), false);
    Validator validator(map);
    if(!validator(ifaceName, ret)) {
        return Request(Request::INVALID, validator.formatError());
    }

    auto id = std::move(ret["id"]);
    auto method = std::move(ret["method"].asString());
    auto params = std::move(ret["params"]);

    return Request(std::move(id), std::move(method), std::move(params));
}

} // namespace rpc