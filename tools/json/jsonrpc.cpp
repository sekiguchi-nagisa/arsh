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
namespace json {

using namespace rpc;

#define EACH_Error_FIELD(T, OP) \
    OP(T, code) \
    OP(T, message) \
    OP(T, data)

DEFINE_JSON_VALIDATE_INTERFACE(Error);

} // namespace json

namespace rpc {

JSON ResponseError::toJSON() {
    return {
            {"code", this->code},
            {"message", std::move(this->message)},
            {"data", std::move(this->data)}
    };
}

std::string ResponseError::toString() const {
    std::string ret = "[";
    ret += std::to_string(this->code);
    ret += ": ";
    ret += this->message;
    ret += "]";
    return ret;
}

std::string Error::toString() const {
    std::string ret = "[";
    ret += std::to_string(this->code);
    ret += ": ";
    ret += this->message;
    ret += "]";
    return ret;
}

JSON toJSON(const Error &error) {
    return {
        {"code", toJSON(error.code)},
        {"message", toJSON(error.message)},
        {"data", toJSON(error.data)}
    };
}

void fromJSON(JSON &&json, Error &error) {
    error.code = static_cast<int>(json["code"].asLong());
    error.message = std::move(json["message"].asString());
    error.data = std::move(json["data"]);
}

JSON Request::toJSON() {
    return {
            {"jsonrpc", "2.0"},
            {"id", std::move(this->id)},
            {"method", std::move(this->method)},
            {"params", std::move(this->params)}
    };
}

void fromJSON(JSON &&json, Request &req) {
    req.id = std::move(json["id"]);
    req.method = std::move(json["method"].asString());
    req.params = std::move(json["params"]);
}

JSON toJSON(const Response &response) {
    bool success = static_cast<bool>(response.value);
    return {
        {"jsonrpc", "2.0"},
        {"id", toJSON(response.id)},
        {"result", success ? toJSON(response.value) : JSON()},
        {"error", success ? JSON() : toJSON(response.value)}
    };
}

void fromJSON(JSON &&value, Response &response) {
    response.id = std::move(value["id"]);
    bool success = value.asObject().find("result") != value.asObject().end();
    if(success) {
        response.value = Ok(std::move(value["result"]));
    } else {
        Error e;
        fromJSON(std::move(value["error"]), e);
        response.value = Err(std::move(e));
    }
}

static InterfaceWrapper initRequestIface() {
    static constexpr auto iface = createInterface(
            "Request",
            field("jsonrpc", string),
            field("id", opt(number | string)),
            field("method", string),
            /**
             * in LSP specification, 'params! : Array<any> | object'.
             */
            field("params", opt(array(any) | anyObj | null))
    );
    return InterfaceWrapper(iface);
}

static InterfaceWrapper initResponseIface() {
    static constexpr auto iface = createInterface(
            "Response",
            field("jsonrpc", string),
            field("id", number | string | null),
            field("result", any)
    );
    return InterfaceWrapper(iface);
}

static InterfaceWrapper initResponseErrorIface() {
    static constexpr auto iface = createInterface(
            "ResponseError",
            field("jsonrpc", string),
            field("id", number | string | null),
            field("error", object(toTypeMatcher<Error>))
    );
    return InterfaceWrapper(iface);
}

Message MessageParser::operator()() {
    // parse
    auto ret = Parser::operator()();
    if(this->hasError()) {
        return Error(ErrorCode::ParseError, "Parse error", this->formatError());
    }

    // validate
    static auto requestIface = initRequestIface();
    static auto responseIface = initResponseIface();
    static auto responseErrorIface = initResponseErrorIface();
    Validator validator;
    if(requestIface(validator, ret)) {
        Request req;
        fromJSON(std::move(ret), req);
        return std::move(req);
    }

    validator.clearError();
    if(!responseIface(validator, ret)) {
        validator.clearError();
        if(!responseErrorIface(validator, ret)) {
            return Error(ErrorCode::InvalidRequest, "Invalid Request", validator.formatError());
        }
    }

    Response res;
    fromJSON(std::move(ret), res);
    return std::move(res);
}

void ParamIfaceMap::add(const std::string &key, InterfaceWrapper &&wrapper) {
    auto pair = this->map.emplace(key, std::move(wrapper));
    if(!pair.second) {
        fatal("already bound method param: %s\n", key.c_str());
    }
}

bool ParamIfaceMap::has(const std::string &key) const {
    auto iter = this->map.find(key);
    return iter != this->map.end();
}

const InterfaceWrapper &ParamIfaceMap::lookup(const std::string &name) const {
    auto iter = this->map.find(name);
    if(iter != this->map.end()) {
        return iter->second;
    }
    fatal("not found corresponding parameter interface to '%s'\n", name.c_str());
}

long CallbackMap::add(const std::string &methodName, ResponseCallback &&callback) {
    std::lock_guard<std::mutex> guard(this->mutex);
    long id = ++this->index;
    auto pair = this->map.emplace(id, std::make_pair(methodName, std::move(callback)));
    if(!pair.second) {
        return 0;
    }
    return id;
}

CallbackMap::Entry CallbackMap::take(long id) {
    std::lock_guard<std::mutex> guard(this->mutex);
    auto iter = this->map.find(id);
    if(iter != this->map.end()) {
        auto entry = std::move(iter->second);
        this->map.erase(iter);
        return entry;
    }
    return {"", ResponseCallback()};
}


// #######################
// ##     Transport     ##
// #######################

void Transport::call(JSON &&id, const std::string &methodName, JSON &&param) {
    auto str = Request(std::move(id), methodName, std::move(param)).toJSON().serialize();
    this->send(str.size(), str.c_str());
}

void Transport::notify(const std::string &methodName, JSON &&param) {
    auto str = Request(JSON(), methodName, std::move(param)).toJSON().serialize();
    this->send(str.size(), str.c_str());
}

void Transport::reply(JSON &&id, JSON &&result) {
    auto str = toJSON(Response(std::move(id), std::move(result))).serialize();
    this->send(str.size(), str.c_str());
}

void Transport::reply(JSON &&id, Error &&error) {
    auto str = toJSON(Response(std::move(id), std::move(error))).serialize();
    this->send(str.size(), str.c_str());
}

bool Transport::dispatch(Handler &handler) {
    int dataSize = this->recvSize();
    if(dataSize < 0) {
        this->logger(LogLevel::ERROR, "may be broken or empty message");
        return false;
    }

    ByteBuffer buf;
    for(int remainSize = dataSize; remainSize > 0;) {
        char data[256];
        constexpr int bufSize = arraySize(data);
        int needSize = remainSize < bufSize ? remainSize : bufSize;
        int recvSize = this->recv(needSize, data);
        if(recvSize < 0) {
            this->logger(LogLevel::ERROR, "message receiving failed");
            return false;
        }
        buf.append(data, static_cast<unsigned int>(recvSize));
        remainSize -= recvSize;
    }

    auto msg = MessageParser(std::move(buf))();
    if(is<Error>(msg)) {
        auto &error = get<Error>(msg);
        this->logger(LogLevel::WARNING, "invalid message => %s", error.toString().c_str());
        this->reply(nullptr, std::move(error));
    } else if(is<Request>(msg)) {
        auto &req = get<Request>(msg);
        if(req.isCall()) {
            auto id = std::move(req.id);
            auto ret = handler.onCall(req.method, std::move(req.params));
            if(ret) {
                this->reply(std::move(id), ret.take());
            } else {
                this->reply(std::move(id), ret.takeError());
            }
        } else {
            handler.onNotify(req.method, std::move(req.params));
        }
    } else {
        assert(is<Response>(msg));
        handler.onResponse(std::move(get<Response>(msg)));
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

    auto &iface = this->callParamMap.lookup(name);
    Validator validator;
    if(!iface(validator, param)) {
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

    auto &iface = this->notificationParamMap.lookup(name);
    Validator validator;
    if(!iface(validator, param)) {
        this->logger(LogLevel::ERROR,
                "notification message validation failed: \n%s", validator.formatError().c_str());
        return;
    }

    iter->second(std::move(param));
}

void Handler::onResponse(Response &&res) {
    assert(res.id.isLong());
    long id = res.id.asLong();
    auto entry = this->callbackMap.take(id);

    if(!entry.second) {
        this->logger(LogLevel::ERROR, "broken response: %ld", id);
        return;
    }

    if(res) {
        auto &iface = this->responseTypeMap.lookup(entry.first);
        Validator validator;
        if(!iface(validator, get<JSON>(res.value))) {
            std::string e = validator.formatError();
            this->logger(LogLevel::ERROR, "response message validation failed: \n%s", e.c_str());
            res.value = newError(InvalidParams, std::move(e), res.value.take());
        }
    }
    entry.second(std::move(res));
}

void Handler::bindImpl(const std::string &methodName, InterfaceWrapper &&wrapper, Call &&func) {
    if(!this->callMap.emplace(methodName, std::move(func)).second) {
        fatal("already defined method: %s\n", methodName.c_str());
    }
    this->callParamMap.add(methodName, std::move(wrapper));
}

void Handler::bindImpl(const std::string &methodName, InterfaceWrapper &&wrapper, Notification &&func) {
    if(!this->notificationMap.emplace(methodName, std::move(func)).second) {
        fatal("already defined method: %s\n", methodName.c_str());
    }
    this->notificationParamMap.add(methodName, std::move(wrapper));
}

long Handler::callImpl(Transport &transport, const std::string &methodName, JSON &&json, ResponseCallback &&func) {
    if(!this->responseTypeMap.has(methodName)) {
        fatal("not found response type corresponding to '%s'\n", methodName.c_str());
    }
    long id = this->callbackMap.add(methodName, std::move(func));
    transport.call(id, methodName, std::move(json));
    return id;
}

} // namespace rpc
} // namespace ydsh