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

#ifndef YDSH_TOOLS_ANALYZER_CLIENT_H
#define YDSH_TOOLS_ANALYZER_CLIENT_H

#include "transport.h"

namespace ydsh::lsp {

// LSP client for testing

struct ClientRequest {
  JSON request;

  explicit ClientRequest(JSON &&j) : request(std::move(j)) {}
};

Result<std::vector<ClientRequest>, std::string> loadInputScript(const std::string &fileName);

struct ClientLogger : public LoggerBase {
  ClientLogger() : LoggerBase("YDSHD_CLIENT") {}
};

class Client {
private:
  LSPTransport transport;
  std::function<bool(rpc::Message &&)> replyCallback;

public:
  Client(LoggerBase &logger, FilePtr &&in, FilePtr &&out)
      : transport(logger, std::move(in), std::move(out)) {}

  void setReplyCallback(const std::function<bool(rpc::Message &&)> &func) {
    this->replyCallback = func;
  }

  void run(const std::vector<ClientRequest> &requests);

private:
  bool send(const JSON &json);

  rpc::Message recv();
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_CLIENT_H
