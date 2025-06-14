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

namespace arsh::rpc {

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

JSON Response::takeResultAsJSON() && {
  if (is<JSON>(this->result)) {
    return std::move(get<JSON>(this->result));
  }
  if (is<RawJSON>(this->result)) {
    return get<RawJSON>(this->result).toJSON();
  }
  return JSON();
}

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    this->logger.get().enabled(L) && (this->logger.get())(L, __VA_ARGS__);                         \
  } while (false)

Message MessageParser::operator()() {
  // parse
  auto ret = JSONParser::operator()();
  if (this->hasError()) {
    return Response(nullptr, Error(ErrorCode::ParseError, "Parse error", this->formatError()));
  }

  LOG(LogLevel::DEBUG, "received message:\n%s", ret.serialize(2).c_str());
  Union<Request, Response> value;
  JSONDeserializer deserializer(std::move(ret));
  deserializer(value);
  if (deserializer.hasError()) {
    return Response(nullptr, Error(ErrorCode::InvalidRequest, "Invalid Request",
                                   deserializer.getValidationError().formatError()));
  }

  if (is<Request>(value)) {
    auto &req = get<Request>(value);
    if (req.id.hasValue() && !req.id.unwrap().isString() && !req.id.unwrap().isNumber() &&
        !req.id.unwrap().isNull()) {
      return Response(nullptr, Error(ErrorCode::InvalidRequest, "Invalid Request",
                                     "id must be null|string|number"));
    }
    if (req.params.hasValue() && !req.params.unwrap().isNull() && !req.params.unwrap().isObject() &&
        !req.params.unwrap().isArray()) {
      return Response(nullptr, Error(ErrorCode::InvalidRequest, "Invalid Request",
                                     "param must be array|object"));
    }
    return std::move(req);
  } else if (is<Response>(value)) {
    auto &res = get<Response>(value);
    if (static_cast<bool>(res)) {
      if (!res.id.isString() && !res.id.isNumber()) {
        return Response(nullptr, Error(ErrorCode::InvalidRequest, "Invalid Request",
                                       "id must be string|number"));
      }
    }
    return std::move(res);
  } else {
    fatal("broken\n");
  }
}

std::string CallbackMap::add(const std::string &methodName, ResponseCallback &&callback) {
  std::lock_guard<std::mutex> guard(this->mutex);
  std::string id;
  Entry entry = {methodName, std::move(callback)};
  while (true) {
    id = this->idGen("id");
    if (this->map.try_emplace(id, std::move(entry)).second) {
      break;
    }
  }
  return id;
}

CallbackMap::Entry CallbackMap::take(const std::string &id) {
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
  LOG(LogLevel::DEBUG, "call:\n%s", json.serialize(2).c_str());
  auto str = json.serialize();
  this->send(str.size(), str.c_str());
}

void Transport::notify(const std::string &methodName, JSON &&param) {
  auto json = Request(JSON(), methodName, std::move(param)).toJSON();
  LOG(LogLevel::DEBUG, "notify:\n%s", json.serialize(2).c_str());
  auto str = json.serialize();
  this->send(str.size(), str.c_str());
}

void Transport::call(RawRequest &&req) {
  RawJSONSerializer serializer;
  serializer(req);
  auto raw = std::move(serializer).take();
  LOG(LogLevel::DEBUG, "%s:\n%s", req.isCall() ? "call" : "notify",
      raw.toJSON().serialize(2).c_str());
  this->send(raw.jsonStr.size(), raw.jsonStr.c_str());
}

void Transport::reply(Response &&res) {
  RawJSONSerializer serializer;
  serializer(res);
  auto raw = std::move(serializer).take();
  LOG(LogLevel::DEBUG, "reply%s:\n%s", res ? "" : " error", raw.toJSON().serialize(2).c_str());
  this->send(raw.jsonStr.size(), raw.jsonStr.c_str());
}

// #####################
// ##     Handler     ##
// #####################

Handler::Status Handler::dispatch(Transport &transport) {
  ssize_t dataSize = transport.recvSize();
  if (dataSize < 0) {
    LOG(LogLevel::WARNING, "may be broken or empty message");
    return Status::ERROR;
  } else if (dataSize == 0) {
    return Status::DISPATCHED; // do nothing
  }

  ByteBuffer buf;
  for (ssize_t remainSize = dataSize; remainSize > 0;) {
    char data[256];
    constexpr ssize_t bufSize = std::size(data);
    ssize_t needSize = remainSize < bufSize ? remainSize : bufSize;
    ssize_t recvSize = transport.recv(needSize, data);
    if (recvSize < 0) {
      LOG(LogLevel::ERROR, "message receiving failed");
      return Status::ERROR;
    }
    buf.append(data, static_cast<unsigned int>(recvSize));
    remainSize -= recvSize;
  }

  auto msg = MessageParser(this->logger, std::move(buf))();
  if (is<Request>(msg)) {
    auto &req = get<Request>(msg);
    if (req.isCall()) {
      this->onCall(transport, std::move(req));
    } else {
      this->onNotify(std::move(req));
    }
  } else {
    assert(is<Response>(msg));
    if (auto &res = get<Response>(msg)) {
      this->onResponse(std::move(res));
    } else {
      auto &error = res.error.unwrap();
      LOG(LogLevel::WARNING, "invalid message => %s", error.toString().c_str());
      transport.reply(nullptr, std::move(error));
    }
  }
  return Status::DISPATCHED;
}

void Handler::onCall(Transport &transport, Request &&req) {
  auto ret = this->onCallImpl(req.method, std::move(req.params));
  this->reply(transport, std::move(req.id.unwrap()), std::move(ret));
}

ReplyImpl Handler::onCallImpl(const std::string &name, JSON &&param) {
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

void Handler::reply(Transport &transport, JSON &&id, ReplyImpl &&ret) {
  if (ret) {
    transport.reply(std::move(id), std::move(ret).take());
  } else {
    transport.reply(std::move(id), std::move(ret).takeError());
  }
}

void Handler::onNotify(Request &&req) {
  auto iter = this->notificationMap.find(req.method);
  if (iter == this->notificationMap.end()) {
    LOG(LogLevel::ERROR, "undefined notification: %s", req.method.c_str());
    return;
  }
  LOG(LogLevel::INFO, "onNotify: %s", req.method.c_str());
  iter->second(std::move(req.params));
}

void Handler::onResponse(Response &&res) {
  if (res.id.isString()) {
    auto &id = res.id.asString();
    if (auto entry = this->callbackMap.take(id); entry.second) {
      entry.second(std::move(res));
      return;
    }
  }
  LOG(LogLevel::ERROR, "broken response: %s", res.toJSON().serialize(2).c_str());
}

ReplyImpl Handler::requestValidationError(const std::string &name, const ValidationError &e) {
  std::string str = e.formatError();
  LOG(LogLevel::ERROR, "request message validation failed at `%s': \n%s", name.c_str(),
      str.c_str());
  return newError(InvalidParams, std::move(str));
}

void Handler::notificationValidationError(const std::string &name, const ValidationError &e) {
  std::string str = e.formatError();
  LOG(LogLevel::ERROR, "notification message validation failed at `%s': \n%s", name.c_str(),
      str.c_str());
}

Error Handler::responseValidationError(const std::string &name, const ValidationError &e) {
  std::string str = e.formatError();
  LOG(LogLevel::ERROR, "response message validation failed at `%s': \n%s", name.c_str(),
      str.c_str());
  return Error(InvalidParams, std::move(str));
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

void Handler::callImpl(Transport &transport, const std::string &methodName, RawJSON &&json,
                       ResponseCallback &&func) {
  auto id = this->callbackMap.add(methodName, std::move(func));
  transport.call(std::move(id), methodName, std::move(json));
}

} // namespace arsh::rpc