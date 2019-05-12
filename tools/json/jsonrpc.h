/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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
#include "conv.hpp"

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

    std::string toString() const;
};

// Error Code definition
enum ErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603
};

struct Error {
    int code{0};
    std::string message;
    JSON data;

    Error() = default;

    Error(int code, std::string &&message, JSON &&data) :
            code(code), message(std::move(message)), data(std::move(data)) {}

    Error(int code, std::string &&message) : Error(code, std::move(message), JSON()) {}

    std::string toString() const;
};

JSON toJSON(const Error &error);
void fromJSON(JSON &&json, Error &error);

struct Request {
    JSON id;    // optional. must be `number | string'
    std::string method; // if error, indicate error message
    JSON params;    // optional. must be `array<any> | object'

    /**
     * each param must be validated
     * @param i
     * @param method
     * @param params
     */
    Request(JSON &&id, const std::string &method, JSON &&params) :
            id(std::move(id)), method(method), params(std::move(params)) {}

    Request(const std::string &method, JSON &&params) : Request(JSON(), method, std::move(params)) {}

    Request() = default;

    bool isNotification() const {
        return this->id.isInvalid();
    }

    bool isCall() const {
        return !this->id.isInvalid();
    }

    /**
     * convert to request json.
     * object must be notification or request.
     * after call it, will be empty.
     * @return
     */
    JSON toJSON();   // for testing
};

void fromJSON(JSON &&json, Request &req);

struct Response {
    JSON id;
    /**
     * in LSP specification, Response is defined as follow. but reduce memory consumption,
     * merge 'result' and 'error' field.
     *
     * interface Response {
     *   id : number | string;
     *   result? : JSON
     *   error? : Error
     * }
     */
    Result<JSON, Error> value; // result or error

    Response() : value(Ok(JSON())) {}

    Response(JSON &&id, JSON &&result) : id(std::move(id)), value(Ok(std::move(result))) {}

    Response(JSON &&id, Error &&error) : id(std::move(id)), value(Err(std::move(error))) {}
};

JSON toJSON(const Response &response);
void fromJSON(JSON &&value, Response &response);

using Message = Union<Request, Response, Error>;

struct MessageParser : public Parser {  //TODO: currently only support single request (not support batch-request)
    explicit MessageParser(ByteBuffer &&buffer) : Parser(std::move(buffer)) {}

    Message operator()();
};

class ParamIfaceMap {
private:
    std::unordered_map<std::string, InterfaceWrapper> map;

public:
    void add(const std::string &key, InterfaceWrapper &&wrapper);

    const InterfaceWrapper &lookup(const std::string &name) const;
};

class Handler;

class Transport {
protected:
    std::reference_wrapper<LoggerBase> logger;

public:
    explicit Transport(LoggerBase &logger) : logger(logger) {}

    virtual ~Transport() = default;

    void call(JSON &&id, const std::string &methodName, JSON &&param);

    void notify(const std::string &methodName, JSON &&param);

    void reply(JSON &&id, JSON &&result);

    /**
     *
     * @param id
     * may be null
     * @param error
     */
    void reply(JSON &&id, Error &&error);

    bool dispatch(Handler &handler);

    // raw level message send/recv api. not directly use them.

    /**
     * send whole json text
     * @param size
     * size of data
     * @param data
     * @return
     * sent data size
     */
    virtual int send(unsigned int size, const char *data) = 0;

    /**
     * read header and get total size of json text
     * @return
     * return -1, if cannot read message size
     * return 0, may be broken message
     */
    virtual int recvSize() = 0;

    /**
     * receive chunk of json text
     * @param size
     * receiving size. must be less than or equal to data size
     * @param data
     * @return
     * received size
     */
    virtual int recv(unsigned int size, char *data) = 0;
};


using ReplyImpl = Result<JSON, Error>;

template <typename T>
struct Reply : public ReplyImpl {
    Reply(T &&value) : ReplyImpl(Ok(toJSON(value))) {}  //NOLINT

    Reply(ErrHolder<Error> &&err) : ReplyImpl(std::move(err)) {}    //NOLINT

    Reply(Reply &&) noexcept = default;

    ~Reply() = default;
};

template <>
struct Reply<void> : public ReplyImpl {
    Reply(std::nullptr_t) : ReplyImpl(Ok(toJSON(nullptr))) {}   //NOLINT

    Reply(ErrHolder<Error> &&err) : ReplyImpl(std::move(err)) {}    //NOLINT

    Reply(Reply &&) noexcept = default;

    ~Reply() = default;
};

template <typename ...Arg>
inline ErrHolder<Error> newError(Arg ...arg) {
    return Err(Error(std::forward<Arg>(arg)...));
}

class Handler {
protected:
    using Call = std::function<ReplyImpl(JSON &&)>;
    using Notification = std::function<void(JSON &&)>;

    std::reference_wrapper<LoggerBase> logger;

private:
    std::unordered_map<std::string, Call> callMap;
    std::unordered_map<std::string, Notification> notificationMap;

    ParamIfaceMap callParamMap;
    ParamIfaceMap notificationParamMap;

public:
    explicit Handler(LoggerBase &logger) : logger(logger) {}

    virtual ~Handler() = default;

    virtual ReplyImpl onCall(const std::string &name, JSON &&param);

    virtual void onNotify(const std::string &name, JSON &&param);

    template<typename State, typename Ret, typename Param>
    void bind(const std::string &name, State *obj, Reply<Ret>(State::*method)(const Param &)) {
        Call func = [obj, method](JSON &&json) -> ReplyImpl {
            Param p;
            fromJSON(std::move(json), p);
            return (obj->*method)(p);
        };
        this->bindImpl(name, toTypeMatcher<Param>, std::move(func));
    }

    template<typename State, typename Ret>
    void bind(const std::string &name, State *obj, Reply<Ret>(State::*method)()) {
        Call func = [obj, method](JSON &&) -> ReplyImpl {
            return (obj->*method)();
        };
        this->bindImpl(name, InterfaceWrapper(voidIface), std::move(func));
    }

    template<typename State, typename Param>
    void bind(const std::string &name, State *obj, void(State::*method)(const Param &)) {
        Notification func = [obj, method](JSON &&json) {
            Param p;
            fromJSON(std::move(json), p);
            (obj->*method)(p);
        };
        this->bindImpl(name, toTypeMatcher<Param>, std::move(func));
    }

    template<typename State>
    void bind(const std::string &name, State *obj, void(State::*method)()) {
        Notification func = [obj, method](JSON &&) {
            (obj->*method)();
        };
        this->bindImpl(name, InterfaceWrapper(voidIface), std::move(func));
    }

protected:
    void bindImpl(const std::string &methodName, InterfaceWrapper &&wrapper, Call &&func);

    void bindImpl(const std::string &methodName, InterfaceWrapper &&wrapper, Notification &&func);
};

} // namespace rpc
} // namespace ydsh

#endif //YDSH_TOOLS_JSONRPC_H
