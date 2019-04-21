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

namespace ydsh {
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
    char b[1];
    this->lexer->appendToBuf(b, 0, true);

    // parse
    auto ret = Parser::operator()();
    if(this->hasError()) {
        return Request(Request::PARSE_ERROR, "Parse error", this->formatError());
    }

    // validate
    auto iface = createInterface(
            "Request",
            field("id", !(number | string)),
            field("method", string),
            field("params", !(array(any) | anyObj))
    );

    Validator validator;
    InterfaceWrapper wrapper(iface);
    if(!wrapper.get()(validator, ret)) {
        return Request(Request::INVALID, "Invalid Request", validator.formatError());
    }

    auto id = std::move(ret["id"]);
    auto method = std::move(ret["method"].asString());
    auto params = std::move(ret["params"]);

    return Request(std::move(id), std::move(method), std::move(params));
}

void ParamIfaceMap::add(const std::string &key, InterfaceWrapper &&wrapper) {
    auto pair = this->map.emplace(key, std::move(wrapper));
    if(!pair.second) {
        fatal("already bound method param: %s\n", key.c_str());
    }
}

const Matcher* ParamIfaceMap::lookup(const std::string &name) const {
    auto iter = this->map.find(name);
    if(iter != this->map.end()) {
        return &iter->second.get();
    }
    return nullptr;
}


// #######################
// ##     Transport     ##
// #######################

JSON Transport::newResponse(JSON &&id, JSON &&result) {
    assert(id.isString() || id.isNumber());
    assert(!result.isInvalid());
    return {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"result", std::move(result)}
    };
}

JSON Transport::newResponse(JSON &&id, ResponseError &&error) {
    assert(id.isString() || id.isNumber() || id.isNull());
    return {
        {"jsonrpc", "2.0"},
        {"id", std::move(id)},
        {"error", error.toJSON()}
    };
}

void Transport::call(JSON &&id, const char *methodName, JSON &&param) {
    auto str = Request(std::move(id), methodName, std::move(param)).toJSON().serialize();
    this->send(str.size(), str.c_str());
}

void Transport::notify(const char *methodName, JSON &&param) {
    auto str = Request(JSON(), methodName, std::move(param)).toJSON().serialize();
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

bool Transport::dispatch(Handler &handler) {
    RequestParser parser;
    int dataSize = this->recvSize();
    if(dataSize < 0) {
        this->logger(LogLevel::ERROR, "may be broken or empty message");
        return false;
    }
    for(int size = 0; size < dataSize;) {
        char data[256];
        int recvSize = this->recv(arraySize(data), data);
        if(recvSize < 0) {
            this->logger(LogLevel::ERROR, "message receiving failed");
            return false;
        }
        parser.append(data, static_cast<unsigned int>(recvSize));
        size += recvSize;
    }
    auto req = parser();
    if(req.isError()) {
        this->reply(nullptr, Request::asError(std::move(req)));
    } else if(req.isCall()) {
        auto id = std::move(req.id);
        auto ret = handler.onCall(req.method, std::move(req.params));
        if(ret) {
            this->reply(std::move(id), ret.take());
        } else {
            this->reply(std::move(id), ret.takeError());
        }
    } else if(req.isNotification()) {
        handler.onNotify(req.method, std::move(req.params));
    }
    return true;
}

// #####################
// ##     Handler     ##
// #####################

ReplyImpl Handler::onCall(const std::string &name, JSON &&param) {
    auto iter = this->callMap.find(name);
    if(iter == this->callMap.end()) {
        std::string str = "undefined method: ";
        str += name;
        this->logger(LogLevel::ERROR, "undefined call: %s", name.c_str());
        return newError(MethodNotFound, std::move(str));
    }

    auto *iface = this->callParamMap.lookup(name);
    assert(iface);
    Validator validator;
    if(!(*iface)(validator, param)) {
        std::string e = validator.formatError();
        this->logger(LogLevel::ERROR, "notification message validation failed: \n%s", e.c_str());
        return newError(InvalidParams, std::move(e));
    }

    return iter->second(std::move(param));
}

void Handler::onNotify(const std::string &name, JSON &&param) {
    auto iter = this->notificationMap.find(name);
    if(iter == this->notificationMap.end()) {
        this->logger(LogLevel::ERROR, "undefined notification: %s", name.c_str());
        return;
    }

    auto *iface = this->notificationParamMap.lookup(name);
    assert(iface);
    Validator validator;
    if(!(*iface)(validator, param)) {
        this->logger(LogLevel::ERROR,
                "notification message validation failed: \n%s", validator.formatError().c_str());
        return;
    }

    iter->second(std::move(param));
}

void Handler::bind(const std::string &methodName, InterfaceWrapper &&wrapper, Call &&func) {
    if(!this->callMap.emplace(methodName, std::move(func)).second) {
        fatal("already defined method: %s\n", methodName.c_str());
    }
    this->callParamMap.add(methodName, std::move(wrapper));
}

void Handler::bind(const std::string &methodName, InterfaceWrapper &&wrapper, Notification &&func) {
    if(!this->notificationMap.emplace(methodName, std::move(func)).second) {
        fatal("already defined method: %s\n", methodName.c_str());
    }
    this->notificationParamMap.add(methodName, std::move(wrapper));
}

} // namespace rpc
} // namespace ydsh