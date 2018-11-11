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

JSON ResponseError::toJSON() {
    return {
            {"code", this->code},
            {"message", std::move(this->message)},
            {"data", std::move(this->data)}
    };
}

JSON Request::toJSON() {
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
    map.interface(ifaceName, {
        field("id", number | string, false),
        field("method", string),
        field("params", array(any) | object(""), false)
    });


    Validator validator(map);
    if(!validator(ifaceName, ret)) {
        return Request(Request::INVALID, validator.formatError());
    }

    auto id = std::move(ret["id"]);
    auto method = std::move(ret["method"].asString());
    auto params = std::move(ret["params"]);

    return Request(std::move(id), std::move(method), std::move(params));
}

void MethodParamMap::add(const std::string &methodName, const char *ifaceName) {
    auto pair = this->map.emplace(methodName, ifaceName);
    if(!pair.second) {
        fatal("already defined param type mapping: %s -> %s\n", methodName.c_str(), ifaceName);
    }
}

const char* MethodParamMap::lookupIface(const std::string &methodName) const {
    auto iter = this->map.find(methodName);
    if(iter == this->map.end()) {
        return nullptr;
    }
    return iter->second.c_str();
}

// #######################
// ##     Transport     ##
// #######################

JSON Transport::newResponse(json::JSON &&id, json::JSON &&result) {
    assert(id.isString() || id.isNumber());
    assert(!result.isInvalid());
    return {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"result", std::move(result)}
    };
}

JSON Transport::newResponse(json::JSON &&id, rpc::ResponseError &&error) {
    assert(id.isString() || id.isNumber() || id.isNull());
    return {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"error", error.toJSON()}
    };
}

void Transport::call(json::JSON &&id, const char *methodName, json::JSON &&param) {
    auto str = Request(std::move(id), methodName, std::move(param)).toJSON().serialize();
    this->send(str.size(), str.c_str());
}

void Transport::notify(const char *methodName, json::JSON &&param) {
    auto str = Request(nullptr, methodName, std::move(param)).toJSON().serialize();
    this->send(str.size(), str.c_str());
}

void Transport::reply(JSON &&id, JSON &&result) {
    auto str = newResponse(std::move(id), std::move(result)).serialize();
    this->send(str.size(), str.c_str());
}

void Transport::reply(JSON &&id, ResponseError &&error) {
    auto str = newResponse(std::move(id), std::move(error)).serialize();
    this->send(str.size(), str.c_str());
}

bool Transport::dispatch(rpc::Handler &handler) {
    RequestParser parser;
    while(!this->isEnd()) {
        char data[256];
        int recvSize = this->recv(ydsh::arraySize(data), data);
        if(recvSize < 0) {
            fatal("message receiving failed!!");
        }
        parser.append(data, static_cast<unsigned int>(recvSize));
    }
    auto req = parser();
    if(req.isError()) {
        this->reply(nullptr, Request::asError(std::move(req)));
    } else if(req.isCall()) {
        auto id = std::move(req.id);
        auto ret = handler.onCall(req.method, std::move(req.params));
        if(ret) {
            this->reply(std::move(id), std::move(ret.asOk()));
        } else {
            this->reply(std::move(id), std::move(ret.asErr()));
        }
    } else if(req.isNotification()) {
        handler.onNotify(req.method, std::move(req.params));
    }
    return true;
}


// #####################
// ##     Handler     ##
// #####################

MethodResult Handler::onCall(const std::string &name, json::JSON &&param) {
    auto iter = this->callMap.find(name);
    if(iter == this->callMap.end()) {
        std::string str = "undefined method: ";
        str += name;
        return ydsh::Err(ResponseError(MethodNotFound, std::move(str)));
    }

    auto *ifaceName = this->callParamMap.lookupIface(name);
    if(!ifaceName) {
        std::string str = "corresponding interface not found: ";
        str += name;
        return ydsh::Err(ResponseError(InvalidParams, std::move(str)));
    }

    Validator validator(this->ifaceMap);
    if(!validator(ifaceName, param)) {
        return ydsh::Err(ResponseError(InvalidParams, validator.formatError()));
    }

    return iter->second(std::move(param));
}

void Handler::onNotify(const std::string &name, json::JSON &&param) {
    auto iter = this->notificationMap.find(name);
    if(iter == this->notificationMap.end()) {
        return; //FIXME: logging
    }

    auto *ifaceName = this->notificationParamMap.lookupIface(name);
    if(!ifaceName) {
        return; //FIXME: logging
    }

    Validator validator(this->ifaceMap);
    if(!validator(ifaceName, param)) {
        return; //FIXME: logging
    }

    iter->second(std::move(param));
}

void Handler::bind(const std::string &name, const char *paramIface, rpc::Handler::Call &&func) {
    if(!this->callMap.emplace(name, std::move(func)).second) {
        fatal("already defined method: %s\n", name.c_str());
    }
    this->callParamMap.add(name, paramIface);
}

void Handler::bind(const std::string &name, const char *paramIface, rpc::Handler::Nofification &&func) {
    if(!this->notificationMap.emplace(name, std::move(func)).second) {
        fatal("already defined method: %s\n", name.c_str());
    }
    this->notificationParamMap.add(name, paramIface);
}

} // namespace rpc