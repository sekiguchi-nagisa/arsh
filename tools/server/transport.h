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

#ifndef YDSH_TOOLS_SERVER_TRANSPORT_H
#define YDSH_TOOLS_SERVER_TRANSPORT_H

#include "../json/jsonrpc.h"

namespace ydsh::lsp {

using namespace json;

class LSPTransport : public rpc::Transport {
private:
  FilePtr input;
  FilePtr output;

public:
  LSPTransport(LoggerBase &logger, FilePtr &&in, FilePtr &&out)
      : rpc::Transport(logger), input(std::move(in)), output(std::move(out)) {}

  ~LSPTransport() override = default;

  const FilePtr &getInput() const { return this->input; }

  const FilePtr &getOutput() const { return this->output; }

  int send(unsigned int size, const char *data) override;

  int recvSize() override;

  int recv(unsigned int size, char *data) override;

private:
  /**
   * if reach end of header, set empty string
   * @return
   */
  bool readHeader(std::string &header);
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_SERVER_TRANSPORT_H
