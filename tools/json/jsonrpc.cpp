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

namespace ydsh::rpc {

std::string Error::toString() const {
  std::string ret = "[";
  ret += std::to_string(this->code);
  ret += ": ";
  ret += this->message;
  ret += "]";
  return ret;
}

JSON Error::toJSON() {
  JSONSerializer serializer;
  serializer(*this);
  return std::move(serializer).take();
}

JSON Request::toJSON() {
  JSONSerializer serializer;
  serializer(*this);
  return std::move(serializer).take();
}

JSON Response::toJSON() {
  JSONSerializer serializer;
  serializer(*this);
  return std::move(serializer).take();
}

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    this->logger.get().enabled(L) && (this->logger.get())(L, __VA_ARGS__);                         \
  } while (false)

Message MessageParser::operator()() {
  // parse
  auto ret = JSONParser::operator()();
  if (this->hasError()) {
    return Error(ErrorCode::ParseError, "Parse error", this->formatError());
  }

  LOG(LogLevel::DEBUG, "server to client:\n%s", ret.serialize(2).c_str());
  Union<Request, Response> value;
  JSONDeserializer deserializer(std::move(ret));
  deserializer(value);
  if (deserializer.hasError()) {
    return Error(ErrorCode::InvalidRequest, "Invalid Request",
                 deserializer.getValidationError().formatError());
  }

  if (is<Request>(value)) {
    auto &req = get<Request>(value);
    if (req.id.hasValue() && !req.id.unwrap().isString() && !req.id.unwrap().isNumber() &&
        !req.id.unwrap().isNull()) {
      return Error(ErrorCode::InvalidRequest, "Invalid Request", "id must be null|string|number");
    }
    if (req.params.hasValue() && !req.params.unwrap().isNull() && !req.params.unwrap().isObject() &&
        !req.params.unwrap().isArray()) {
      return Error(ErrorCode::InvalidRequest, "Invalid Request", "param must be array|object");
    }
    return std::move(req);
  } else if (is<Response>(value)) {
    auto &res = get<Response>(value);
    if (!static_cast<bool>(res)) {
      return std::move(res.error.unwrap());
    }
    if (!res.id.isString() && !res.id.isNumber()) {
      return Error(ErrorCode::InvalidRequest, "Invalid Request", "id must be string|number");
    }
    return std::move(res);
  } else {
    fatal("broken\n");
  }
}

long CallbackMap::add(const std::string &methodName, ResponseCallback &&callback) {
  std::lock_guard<std::mutex> guard(this->mutex);
  long id = ++this->index;
  auto pair = this->map.emplace(id, std::make_pair(methodName, std::move(callback)));
  if (!pair.second) {
    return 0;
  }
  return id;
}

CallbackMap::Entry CallbackMap::take(long id) {
  std::lock_guard<std::mutex> guard(this->mutex);
  auto iter = this->map.find(id);
  if (iter != this->map.end()) {
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
  auto json = Request(std::move(id), methodName, std::move(param)).toJSON();
  LOG(LogLevel::DEBUG, "client to server call:\n%s", json.serialize(2).c_str());
  auto str = json.serialize();
  this->send(str.size(), str.c_str());
}

void Transport::notify(const std::string &methodName, JSON &&param) {
  auto json = Request(JSON(), methodName, std::move(param)).toJSON();
  LOG(LogLevel::DEBUG, "client to server notify:\n%s", json.serialize(2).c_str());
  auto str = json.serialize();
  this->send(str.size(), str.c_str());
}

void Transport::reply(JSON &&id, JSON &&result) {
  auto json = Response(std::move(id), std::move(result)).toJSON();
  LOG(LogLevel::DEBUG, "client to server reply:\n%s", json.serialize(2).c_str());
  auto str = json.serialize();
  this->send(str.size(), str.c_str());
}

void Transport::reply(JSON &&id, Error &&error) {
  auto json = Response(std::move(id), std::move(error)).toJSON();
  LOG(LogLevel::DEBUG, "client to server error:\n%s", json.serialize(2).c_str());
  auto str = json.serialize();
  this->send(str.size(), str.c_str());
}

bool Transport::dispatch(Handler &handler) {
  int dataSize = this->recvSize();
  if (dataSize < 0) {
    LOG(LogLevel::WARNING, "may be broken or empty message");
    return false;
  }

  ByteBuffer buf;
  for (int remainSize = dataSize; remainSize > 0;) {
    char data[256];
    constexpr int bufSize = std::size(data);
    int needSize = remainSize < bufSize ? remainSize : bufSize;
    int recvSize = this->recv(needSize, data);
    if (recvSize < 0) {
      LOG(LogLevel::ERROR, "message receiving failed");
      return false;
    }
    buf.append(data, static_cast<unsigned int>(recvSize));
    remainSize -= recvSize;
  }

  auto msg = MessageParser(this->logger, std::move(buf))();
  if (is<Error>(msg)) {
    auto &error = get<Error>(msg);
    LOG(LogLevel::WARNING, "invalid message => %s", error.toString().c_str());
    this->reply(nullptr, std::move(error));
  } else if (is<Request>(msg)) {
    auto &req = get<Request>(msg);
    if (req.isCall()) {
      auto id = std::move(req.id);
      auto ret = handler.onCall(req.method, std::move(req.params));
      if (ret) {
        this->reply(std::move(id), std::move(ret).take());
      } else {
        this->reply(std::move(id), std::move(ret).takeError());
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
  if (iter == this->callMap.end()) {
    std::string str = "undefined method: ";
    str += name;
    LOG(LogLevel::ERROR, "undefined call: %s", name.c_str());
    return newError(MethodNotFound, std::move(str));
  }
  LOG(LogLevel::INFO, "onCall: %s", name.c_str());
  return iter->second(std::move(param));
}

void Handler::onNotify(const std::string &name, JSON &&param) {
  auto iter = this->notificationMap.find(name);
  if (iter == this->notificationMap.end()) {
    LOG(LogLevel::ERROR, "undefined notification: %s", name.c_str());
    return;
  }
  LOG(LogLevel::INFO, "onNotify: %s", name.c_str());
  iter->second(std::move(param));
}

void Handler::onResponse(Response &&res) {
  assert(res.id.isLong());
  long id = res.id.asLong();
  auto entry = this->callbackMap.take(id);

  if (!entry.second) {
    LOG(LogLevel::ERROR, "broken response: %ld", id);
    return;
  }
  entry.second(std::move(res));
}

ReplyImpl Handler::requestValidationError(const ValidationError &e) {
  std::string str = e.formatError();
  LOG(LogLevel::ERROR, "request message validation failed: \n%s", str.c_str());
  return newError(InvalidParams, std::move(str));
}

void Handler::notificationValidationError(const ValidationError &e) {
  std::string str = e.formatError();
  LOG(LogLevel::ERROR, "notification message validation failed: \n%s", str.c_str());
}

void Handler::responseValidationError(const ValidationError &e, Response &res) {
  std::string str = e.formatError();
  LOG(LogLevel::ERROR, "response message validation failed: \n%s", str.c_str());
  res.error = Error(InvalidParams, std::move(str));
}

void Handler::bindImpl(const std::string &methodName, Call &&func) {
  if (!this->callMap.emplace(methodName, std::move(func)).second) {
    fatal("already defined method: %s\n", methodName.c_str());
  }
}

void Handler::bindImpl(const std::string &methodName, Notification &&func) {
  if (!this->notificationMap.emplace(methodName, std::move(func)).second) {
    fatal("already defined method: %s\n", methodName.c_str());
  }
}

long Handler::callImpl(Transport &transport, const std::string &methodName, JSON &&json,
                       ResponseCallback &&func) {
  long id = this->callbackMap.add(methodName, std::move(func));
  transport.call(static_cast<int64_t>(id), methodName, std::move(json));
  return id;
}

} // namespace ydsh::rpc