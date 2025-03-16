/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_ANALYZER_CLIENT_H
#define ARSH_TOOLS_ANALYZER_CLIENT_H

#include "../directive/directive.h"

#include "driver.h"
#include "lsp.h"
#include "transport.h"

namespace arsh::lsp {

// LSP client for testing

struct ClientRequest {
  JSON request;
  std::chrono::milliseconds msec{0};

  ClientRequest(JSON &&j, std::chrono::milliseconds n) : request(std::move(j)), msec(n) {}
};

struct ClientInput {
  std::string fileName;
  std::vector<ClientRequest> req;
  Optional<directive::Directive> directive;
};

Result<ClientInput, std::string>
loadInputScript(const std::string &fileName, bool open = false,
                std::chrono::milliseconds waitTime = std::chrono::milliseconds{10});

struct ClientLogger : public LoggerBase {
  ClientLogger() : LoggerBase("ARSHD_CLIENT") {}
};

class Client {
private:
  LSPTransport transport;
  std::function<bool(rpc::Message &&)> replyCallback;

public:
  Client(LoggerBase &logger, int inFd, int outFd) : transport(logger, inFd, outFd) {}

  void setReplyCallback(const std::function<bool(rpc::Message &&)> &func) {
    this->replyCallback = func;
  }

  void run(const ClientInput &input);

  static bool isBrokenOrEmpty(const rpc::Error &error);

private:
  bool send(const JSON &json);

  rpc::Message recv();
};

class TestClientServerDriver : public Driver {
private:
  LogLevel level;
  ClientInput clientInput;

public:
  TestClientServerDriver(LogLevel level, ClientInput &&input)
      : level(level), clientInput(std::move(input)) {}

  int run(const DriverOptions &options, std::function<int(const DriverOptions &)> &&func) override;

private:
  static void prettyPrint(const JSON &json) {
    std::string value = json.serialize(2);
    fputs(value.c_str(), stdout);
    fflush(stdout);
  }
};

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_CLIENT_H
