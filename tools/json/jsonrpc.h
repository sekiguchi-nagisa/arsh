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

#ifndef YDSH_TOOLS_JSONRPC_H
#define YDSH_TOOLS_JSONRPC_H

#include <functional>

#include <misc/logger_base.hpp>
#include "validate.h"

namespace ydsh {
namespace rpc {

using namespace json;

struct ResponseError {
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
    JSON toJSON();
};

// Error Code definition
constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;


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

    Request(std::string &&method, JSON &&params) : Request(JSON(), std::move(method), std::move(params)) {}

    /**
     * for error
     * @param kind
     * must not be SUCCESS
     * @param error
     */
    Request(Kind kind, std::string &&error, JSON &&data) :
            kind(kind), method(std::move(error)), params(std::move(data)) {}

    bool isError() const {
        return this->kind != SUCCESS;
    }

    bool isNotification() const {
        return !this->isError() && this->id.isInvalid();
    }

    bool isCall() const {
        return !this->isError() && !this->id.isInvalid();
    }

    static ResponseError asError(Request &&req) {
        assert(req.isError());
        return ResponseError(static_cast<int>(req.kind), std::move(req.method), std::move(req.params));
    }

    /**
     * convert to request json.
     * object must be notification or request.
     * after call it, will be empty.
     * @return
     */
    JSON toJSON();   // for testing
};

struct RequestParser : public Parser {  //TODO: currently only support single request (not support batch-request)
    RequestParser() : Parser() {}

    RequestParser &append(const char *text) {
        return this->append(text, strlen(text));
    }

    RequestParser &append(const char *data, unsigned int size) {
        this->lexer->appendToBuf(data, size, false);
        return *this;
    }

    Request operator()();
};

class MethodParamMap {
private:
    std::unordered_map<std::string, std::string> map;

public:
    void add(const std::string &methodName, const std::string &ifaceName);

    const char *lookupIface(const std::string &methodName) const;
};

class Handler;

class Transport {
protected:
    std::reference_wrapper<LoggerBase> logger;

public:
    explicit Transport(LoggerBase &logger) : logger(logger) {}

    virtual ~Transport() = default;

    void call(JSON &&id, const char *methodName, JSON &&param);

    void notify(const char *methodName, JSON &&param);

    void reply(JSON &&id, JSON &&result);

    /**
     *
     * @param id
     * may be null
     * @param error
     */
    void reply(JSON &&id, ResponseError &&error);

    bool dispatch(Handler &handler);

    static JSON newResponse(JSON &&id, JSON &&result);

    static JSON newResponse(JSON &&id, ResponseError &&error);

private:
    /**
     * send whole json text
     * @param size
     * size of data
     * @param data
     * @return
     * sent size
     */
    virtual int send(unsigned int size, const char *data) = 0;

    /**
     * read header and get total size of json text
     * @return
     */
    virtual int recvSize() = 0;

    /**
     * receive chunk of json text
     * @param size
     * size of data
     * @param data
     * @return
     * received size
     */
    virtual int recv(unsigned int size, char *data) = 0;
};

using MethodResult = Result<JSON, ResponseError>;

class Handler {
protected:
    using Call = std::function<MethodResult(JSON &&)>;
    using Notification = std::function<void(JSON &&)>;

private:
    std::unordered_map<std::string, Call> callMap;
    std::unordered_map<std::string, Notification> notificationMap;

    MethodParamMap callParamMap;
    MethodParamMap notificationParamMap;

    InterfaceMap ifaceMap;

protected:
    std::reference_wrapper<LoggerBase> logger;

public:
    explicit Handler(LoggerBase &logger) : logger(logger) {}

    MethodResult onCall(const std::string &name, JSON &&param);

    void onNotify(const std::string &name, JSON &&param);

    InterfacePtr interface(const char *name, Fields &&fields) {
        return this->ifaceMap.interface(name, std::move(fields));
    }

    VoidInterfacePtr interface() {
        return this->ifaceMap.interface();
    }

    void bind(const std::string &name, const InterfaceBasePtr &paramIface, Call &&func);

    void bind(const std::string &name, const InterfaceBasePtr &paramIface, Notification &&func);

    template<typename State, typename Param>
    void bind(const std::string &name, const InterfacePtr &paramIface, State *obj,
              MethodResult(State::*method)(const Param &)) {
        Call func = [obj, method](JSON &&json) -> MethodResult {
            Param p;
            fromJSON(std::move(json), p);
            return (obj->*method)(p);
        };
        this->bind(name, paramIface, std::move(func));
    }

    template<typename State>
    void bind(const std::string &name, const VoidInterfacePtr &paramIface, State *obj,
              MethodResult(State::*method)(void)) {
        Call func = [obj, method](JSON &&) -> MethodResult {
            return (obj->*method)();
        };
        this->bind(name, paramIface, std::move(func));
    }

    template<typename State, typename Param>
    void bind(const std::string &name, const InterfacePtr &paramIface, State *obj,
              void(State::*method)(const Param &)) {
        Notification func = [obj, method](JSON &&json) {
            Param p;
            fromJSON(std::move(json), p);
            (obj->*method)(p);
        };
        this->bind(name, paramIface, std::move(func));
    }

    template<typename State>
    void bind(const std::string &name, const VoidInterfacePtr &paramIface, State *obj,
              void(State::*method)(void)) {
        Notification func = [obj, method](JSON &&) {
            (obj->*method)();
        };
        this->bind(name, paramIface, std::move(func));
    }
};

} // namespace rpc
} // namespace ydsh

#endif //YDSH_TOOLS_JSONRPC_H
