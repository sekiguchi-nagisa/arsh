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

#ifndef YDSH_TOOLS_JSON_JSONRPC_H
#define YDSH_TOOLS_JSON_JSONRPC_H

#include <functional>

#include "json.h"
#include "serialize.h"
#include <misc/logger_base.hpp>

namespace ydsh::rpc {

using namespace json;

struct ResponseError {
  int code;
  std::string message;
  JSON data;

  ResponseError(int code, std::string &&message, JSON &&data)
      : code(code), message(std::move(message)), data(std::move(data)) {}

  ResponseError(int code, std::string &&message)
      : ResponseError(code, std::move(message), JSON()) {}

  ResponseError() : code(0) {}

  std::string toString() const;

  template <typename T>
  void jsonify(T &t) {
    t("code", this->code);
    t("message", this->message);
    t("data", this->data);
  }
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
  Optional<JSON> data;

  Error() = default;

  Error(int code, std::string &&message, JSON &&data)
      : code(code), message(std::move(message)), data(std::move(data)) {}

  Error(int code, std::string &&message) : Error(code, std::move(message), JSON()) {}

  std::string toString() const;

  template <typename T>
  void jsonify(T &t) {
    t("code", this->code);
    t("message", this->message);
    t("data", this->data);
  }

  JSON toJSON();
};

struct Request {
  static_assert(std::is_same_v<JSON, Optional<JSON>::base_type>);

  std::string jsonrpc{"2.0"};
  Optional<JSON> id;     // optional. must be `number | string | null'
  std::string method;    // if error, indicate error message
  Optional<JSON> params; // optional. must be `array<any> | object | null'

  /**
   * each param must be validated
   * @param i
   * @param method
   * @param params
   */
  Request(JSON &&id, std::string method, JSON &&params)
      : id(std::move(id)), method(std::move(method)), params(std::move(params)) {}

  Request(const std::string &method, JSON &&params) : Request(JSON(), method, std::move(params)) {}

  Request() = default;

  bool isNotification() const { return !this->isCall(); }

  bool isCall() const { return !this->id.isInvalid() && !this->id.unwrap().isNull(); }

  /**
   * convert to request json.
   * object must be notification or request.
   * after call it, will be empty.
   * @return
   */
  JSON toJSON(); // for testing

  template <typename T>
  void jsonify(T &t) {
    t("jsonrpc", this->jsonrpc);
    t("id", this->id);
    t("method", this->method);
    t("params", this->params);
  }
};

struct Response {
  std::string jsonrpc{"2.0"};

  JSON id; // number|string
  Optional<JSON> result;
  Optional<Error> error;

  Response() = default;

  Response(JSON &&id, JSON &&result) : id(std::move(id)), result(std::move(result)) {}

  Response(JSON &&id, Error &&error) : id(std::move(id)), error(std::move(error)) {}

  explicit operator bool() const { return !this->error.hasValue(); }

  template <typename T>
  void jsonify(T &t) {
    t("jsonrpc", this->jsonrpc);
    t("id", this->id);
    t("result", this->result);
    t("error", this->error);
  }

  JSON toJSON();
};

using Message = Union<Request, Response, Error>;

class MessageParser : public JSONParser {
private:
  std::reference_wrapper<LoggerBase> logger;

public:
  MessageParser(LoggerBase &logger, ByteBuffer &&buffer)
      : JSONParser(std::move(buffer)), logger(logger) {}

  Message operator()(); // TODO: currently only support single request (not support batch-request)
};

using ResponseCallback = std::function<void(Response &&)>;

class CallbackMap {
private:
  using Entry = std::pair<std::string, ResponseCallback>;

  std::mutex mutex;
  std::unordered_map<long, Entry> map;
  long index{0};

public:
  /**
   *
   * @param methodName
   * @param callback
   * @return
   * call id.
   * if already added, return 0.
   */
  long add(const std::string &methodName, ResponseCallback &&callback);

  /**
   * take callback entry corresponding to 'id'
   * @param id
   * @return
   * if not found corresponding entry, return empty entry.
   */
  Entry take(long id);
};

class Handler;

class Transport {
protected:
  std::reference_wrapper<LoggerBase> logger;

public:
  explicit Transport(LoggerBase &logger) : logger(logger) {}

  virtual ~Transport() = default;

  LoggerBase &getLogger() const { return this->logger.get(); }

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
  static JSON serialize(T &&value) {
    JSONSerializer serializer;
    serializer(value);
    return std::move(serializer).take();
  }

  Reply(T &&value) : ReplyImpl(Ok(serialize(std::move(value)))) {} // NOLINT

  Reply(ErrHolder<Error> &&err) : ReplyImpl(std::move(err)) {} // NOLINT

  Reply(Reply &&) noexcept = default;

  ~Reply() = default;
};

template <>
struct Reply<void> : public ReplyImpl {
  Reply(std::nullptr_t) : ReplyImpl(Ok(JSON(nullptr))) {} // NOLINT

  Reply(ErrHolder<Error> &&err) : ReplyImpl(std::move(err)) {} // NOLINT

  Reply(Reply &&) noexcept = default;

  ~Reply() = default;
};

template <typename... Arg>
inline ErrHolder<Error> newError(Arg... arg) {
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
  CallbackMap callbackMap;

public:
  explicit Handler(LoggerBase &logger) : logger(logger) {}

  virtual ~Handler() = default;

  virtual ReplyImpl onCall(const std::string &name, JSON &&param);

  virtual void onNotify(const std::string &name, JSON &&param);

  virtual void onResponse(Response &&res);

  template <typename State, typename Ret, typename Param>
  void bind(const std::string &name, State *obj, Reply<Ret> (State::*method)(const Param &)) {
    Call func = [this, obj, method](JSON &&json) -> ReplyImpl {
      JSONDeserializer deserializer(std::move(json));
      Param p;
      deserializer(p);
      if (deserializer.hasError()) {
        return this->requestValidationError(deserializer.getValidationError());
      }
      return (obj->*method)(p);
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename State, typename Ret>
  void bind(const std::string &name, State *obj, Reply<Ret> (State::*method)()) {
    Call func = [this, obj, method](JSON &&json) -> ReplyImpl {
      JSONDeserializer deserializer(std::move(json));
      Optional<std::nullptr_t> p;
      deserializer(p);
      if (deserializer.hasError()) {
        return this->requestValidationError(deserializer.getValidationError());
      }
      return (obj->*method)();
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename State, typename Param>
  void bind(const std::string &name, State *obj, void (State::*method)(const Param &)) {
    Notification func = [this, obj, method](JSON &&json) {
      JSONDeserializer deserializer(std::move(json));
      Param p;
      deserializer(p);
      if (deserializer.hasError()) {
        this->notificationValidationError(deserializer.getValidationError());
        return;
      }
      (obj->*method)(p);
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename State>
  void bind(const std::string &name, State *obj, void (State::*method)()) {
    Notification func = [this, obj, method](JSON &&json) {
      JSONDeserializer deserializer(std::move(json));
      Optional<std::nullptr_t> p;
      deserializer(p);
      if (deserializer.hasError()) {
        this->notificationValidationError(deserializer.getValidationError());
        return;
      }
      (obj->*method)();
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename Ret, typename Param, typename Func, typename Error>
  auto call(Transport &transport, const std::string &name, const Param &param, Func callback,
            Error ecallback) {
    ResponseCallback func = [this, callback, ecallback](Response &&res) {
      if (res) {
        JSONDeserializer deserializer(std::move(res.result));
        Ret ret;
        deserializer(ret);
        if (deserializer.hasError()) {
          this->responseValidationError(deserializer.getValidationError(), res);
        } else {
          callback(ret);
          return;
        }
      }
      ecallback(std::move(res.error.unwrap()));
    };
    JSONSerializer serializer;
    serializer(param);
    return this->callImpl(transport, name, std::move(serializer).take(), std::move(func));
  }

protected:
  ReplyImpl requestValidationError(const ValidationError &e);

  void notificationValidationError(const ValidationError &e);

  void responseValidationError(const ValidationError &e, Response &res);

  void bindImpl(const std::string &methodName, Call &&func);

  void bindImpl(const std::string &methodName, Notification &&func);

  long callImpl(Transport &transport, const std::string &methodName, JSON &&json,
                ResponseCallback &&func);
};

} // namespace ydsh::rpc

#endif // YDSH_TOOLS_JSON_JSONRPC_H
