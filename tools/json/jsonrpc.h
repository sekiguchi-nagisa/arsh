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

#ifndef ARSH_TOOLS_JSON_JSONRPC_H
#define ARSH_TOOLS_JSON_JSONRPC_H

#include <functional>

#include "json.h"
#include "serialize.h"
#include <misc/logger_base.hpp>
#include <misc/split_random.hpp>

namespace arsh::rpc {

using namespace json;

// Error Code definition
enum ErrorCode : int {
  ParseError = -32700,
  InvalidRequest = -32600,
  MethodNotFound = -32601,
  InvalidParams = -32602,
  InternalError = -32603,
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

struct RequestBase {
  static_assert(std::is_same_v<JSON, Optional<JSON>::base_type>);

  std::string jsonrpc{"2.0"};
  Optional<JSON> id;  // optional. must be `number | string | null'
  std::string method; // if error, indicate error message

  RequestBase() = default;

  RequestBase(JSON &&id, const std::string &method) : id(std::move(id)), method(method) {}

  bool isNotification() const { return !this->isCall(); }

  bool isCall() const { return !this->id.isInvalid() && !this->id.unwrap().isNull(); }

  template <typename T>
  void jsonify(T &t) {
    t("jsonrpc", this->jsonrpc);
    t("id", this->id);
    t("method", this->method);
  }
};

struct Request : RequestBase {
  Optional<JSON> params; // optional. must be `array<any> | object | null`

  /**
   * each param must be validated
   * @param id
   * @param method
   * @param params
   */
  Request(JSON &&id, const std::string &method, JSON &&params)
      : RequestBase(std::move(id), method), params(std::move(params)) {}

  Request(const std::string &method, JSON &&params) : Request(JSON(), method, std::move(params)) {}

  Request() = default;

  /**
   * convert to request json.
   * object must be notification or request.
   * after called it, will be empty.
   * @return
   */
  JSON toJSON(); // for testing

  template <typename T>
  void jsonify(T &t) {
    RequestBase::jsonify(t);
    t("params", this->params);
  }
};

struct RawRequest : RequestBase {
  RawJSON params; // optional. must be `array<any> | object | null`

  RawRequest(JSON &&id, const std::string &method, RawJSON &&params)
      : RequestBase(std::move(id), method), params(std::move(params)) {}

  RawRequest(const std::string &method, RawJSON &&params)
      : RawRequest(JSON(), method, std::move(params)) {}

  template <typename T>
  void jsonify(T &t) {
    RequestBase::jsonify(t);
    t("params", this->params);
  }
};

struct Response {
  std::string jsonrpc{"2.0"};

  JSON id; // number|string|null
  Optional<Union<JSON, RawJSON>> result;
  Optional<Error> error;

  Response() = default;

  Response(JSON &&id, JSON &&result) : id(std::move(id)), result(std::move(result)) {}

  Response(JSON &&id, RawJSON &&result) : id(std::move(id)), result(std::move(result)) {}

  Response(JSON &&id, Error &&error) : id(std::move(id)), error(std::move(error)) {}

  explicit operator bool() const { return !this->error.hasValue(); }

  template <typename T>
  void jsonify(T &t) {
    t("id", this->id);
    t("jsonrpc", this->jsonrpc);
    t("result", this->result);
    t("error", this->error);
  }

  JSON toJSON();

  JSON takeResultAsJSON() &&;
};

using Message = Union<Request, Response>;

class MessageParser : public JSONParser {
private:
  std::reference_wrapper<LoggerBase> logger;

public:
  MessageParser(LoggerBase &logger, ByteBuffer &&buffer)
      : JSONParser(std::move(buffer)), logger(logger) {}

  Message operator()(); // TODO: currently only support single request (not support batch-request)
};

class IDGenerator {
private:
  L64X128MixRNG rng;

public:
  explicit IDGenerator(uint64_t seed) : rng(seed) {}

  std::string operator()(const char *prefix) {
    std::string value;
    if (prefix && *prefix) {
      value += prefix;
      value += '-';
    }
    const auto v1 = static_cast<uintmax_t>(this->rng.next());
    const auto v2 = static_cast<uintmax_t>(this->rng.next());
    char data[128];
    const ssize_t s = snprintf(data, std::size(data), "%jx-%jX", v1, v2);
    value.append(data, s);
    return value;
  }
};

using ResponseCallback = std::function<void(Response &&)>;

class CallbackMap {
private:
  using Entry = std::pair<std::string, ResponseCallback>;

  std::mutex mutex;
  IDGenerator idGen;
  std::unordered_map<std::string, Entry> map;

public:
  explicit CallbackMap(uint64_t seed) : idGen(seed) {}

  /**
   *
   * @param methodName
   * @param callback
   * @return
   * call id.
   */
  std::string add(const std::string &methodName, ResponseCallback &&callback);

  /**
   * take callback entry corresponding to 'id'
   * @param id
   * @return
   * if not found corresponding entry, return empty entry.
   */
  Entry take(const std::string &id);
};

class Transport {
protected:
  std::reference_wrapper<LoggerBase> logger;

public:
  explicit Transport(LoggerBase &logger) : logger(logger) {}

  virtual ~Transport() = default;

  LoggerBase &getLogger() const { return this->logger.get(); }

  void call(JSON &&id, const std::string &methodName, RawJSON &&param) {
    this->call(RawRequest(std::move(id), methodName, std::move(param)));
  }

  void notify(const std::string &methodName, RawJSON &&param) {
    this->call(RawRequest(methodName, std::move(param)));
  }

  void call(RawRequest &&req);

  void reply(JSON &&id, RawJSON &&result) {
    this->reply(Response(std::move(id), std::move(result)));
  }

  /**
   *
   * @param id
   * may be null
   * @param error
   */
  void reply(JSON &&id, Error &&error) { this->reply(Response(std::move(id), std::move(error))); }

  void reply(Response &&res);

  // raw level message send/recv api. not directly use them.

  /**
   * send whole json text
   * @param size
   * size of data
   * @param data
   * must be null terminated
   * @return
   * sent data size
   */
  virtual ssize_t send(size_t size, const char *data) = 0;

  /**
   * read header and get total size of json text
   * @return
   * return -1, if cannot read message size
   * return 0, may be broken message
   */
  virtual ssize_t recvSize() = 0;

  /**
   * receive chunk of json text
   * @param size
   * receiving size. must be less than or equal to data size
   * @param data
   * @return
   * received size
   */
  virtual ssize_t recv(size_t size, char *data) = 0;
};

using ReplyImpl = Result<RawJSON, Error>;

template <typename T>
struct Reply : public ReplyImpl {
  static RawJSON serialize(T &&value) {
    RawJSONSerializer serializer;
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
  Reply(std::nullptr_t) : ReplyImpl(Ok(RawJSON::null())) {} // NOLINT

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
  Handler(LoggerBase &logger, uint64_t seed) : logger(logger), callbackMap(seed) {}

  explicit Handler(LoggerBase &logger) : Handler(logger, 42) {}

  virtual ~Handler() = default;

  enum class Status : unsigned char {
    DISPATCHED,
    ERROR,
  };

  /**
   *
   * @param transport
   * @return
   * return false if received message is invalid
   */
  Status dispatch(Transport &transport);

  virtual void onCall(Transport &transport, Request &&req);

  virtual void onNotify(Request &&req);

  virtual void onResponse(Response &&res);

  template <typename State, typename Ret, typename Param>
  void bind(const std::string &name, State *obj, Reply<Ret> (State::*method)(const Param &)) {
    Call func = [this, obj, method, name](JSON &&json) -> ReplyImpl {
      JSONDeserializer deserializer(std::move(json));
      Param p;
      deserializer(p);
      if (deserializer.hasError()) {
        return this->requestValidationError(name, deserializer.getValidationError());
      }
      return (obj->*method)(p);
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename State, typename Ret>
  void bind(const std::string &name, State *obj, Reply<Ret> (State::*method)()) {
    Call func = [this, obj, method, name](JSON &&json) -> ReplyImpl {
      JSONDeserializer deserializer(std::move(json));
      Optional<std::nullptr_t> p;
      deserializer(p);
      if (deserializer.hasError()) {
        return this->requestValidationError(name, deserializer.getValidationError());
      }
      return (obj->*method)();
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename State, typename Param>
  void bind(const std::string &name, State *obj, void (State::*method)(const Param &)) {
    Notification func = [this, obj, method, name](JSON &&json) {
      JSONDeserializer deserializer(std::move(json));
      Param p;
      deserializer(p);
      if (deserializer.hasError()) {
        this->notificationValidationError(name, deserializer.getValidationError());
        return;
      }
      (obj->*method)(p);
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename State>
  void bind(const std::string &name, State *obj, void (State::*method)()) {
    Notification func = [this, obj, method, name](JSON &&json) {
      JSONDeserializer deserializer(std::move(json));
      Optional<std::nullptr_t> p;
      deserializer(p);
      if (deserializer.hasError()) {
        this->notificationValidationError(name, deserializer.getValidationError());
        return;
      }
      (obj->*method)();
    };
    this->bindImpl(name, std::move(func));
  }

  template <typename Ret, typename Param, typename Func, typename Error>
  void call(Transport &transport, const std::string &name, Param &&param, Func callback,
            Error ecallback) {
    ResponseCallback func = [this, callback, ecallback, name](Response &&res) {
      if (res) {
        JSONDeserializer deserializer(std::move(res).takeResultAsJSON());
        Ret ret;
        deserializer(ret);
        if (deserializer.hasError()) {
          res.error = this->responseValidationError(name, deserializer.getValidationError());
        } else {
          callback(ret);
          return;
        }
      }
      ecallback(res.error.unwrap());
    };
    RawJSONSerializer serializer;
    serializer(param);
    this->callImpl(transport, name, std::move(serializer).take(), std::move(func));
  }

  template <typename Param>
  void notify(Transport &transport, const std::string &name, Param &&param) {
    RawJSONSerializer serializer;
    serializer(param);
    transport.notify(name, std::move(serializer).take());
  }

protected:
  ReplyImpl onCallImpl(const std::string &name, JSON &&param);

  virtual void reply(Transport &transport, JSON &&id, ReplyImpl &&ret);

  ReplyImpl requestValidationError(const std::string &name, const ValidationError &e);

  void notificationValidationError(const std::string &name, const ValidationError &e);

  Error responseValidationError(const std::string &name, const ValidationError &e);

  void bindImpl(const std::string &methodName, Call &&func);

  void bindImpl(const std::string &methodName, Notification &&func);

  void callImpl(Transport &transport, const std::string &methodName, RawJSON &&json,
                ResponseCallback &&func);
};

} // namespace arsh::rpc

#endif // ARSH_TOOLS_JSON_JSONRPC_H
