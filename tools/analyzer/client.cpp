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

#include <fstream>

#include "client.h"

namespace ydsh::lsp {

static Result<JSON, std::string> parseJSON(const std::string &fileName, const std::string &content,
                                           unsigned int lineNumOffset) {
  JSONLexer lexer(fileName.c_str(), content.c_str());
  lexer.setLineNumOffset(lineNumOffset);
  JSONParser parser(std::move(lexer));
  auto json = parser(true);
  if (parser.hasError()) {
    return Err(parser.formatError());
  } else {
    return Ok(std::move(json));
  }
}

Result<std::vector<ClientRequest>, std::string> loadInputScript(const std::string &fileName) {
  std::ifstream input(fileName);
  if (!input) {
    std::string error = "cannot read: ";
    error += fileName;
    return Err(std::move(error));
  }
  std::vector<ClientRequest> requests;
  std::string content;
  unsigned int lineNum = 0;
  unsigned int lineNumOffset = 0;
  for (std::string line; std::getline(input, line);) {
    lineNum++;
    if (!lineNumOffset) {
      lineNumOffset = lineNum;
    }
    if (line[0] == '#') {
      line = "";
    }
    if (line == "---" || line == "<<<") {
      auto ret = parseJSON(fileName, content, lineNumOffset);
      if (!ret) {
        return Err(std::move(ret).takeError());
      }
      content = "";
      lineNumOffset = 0;
      if (ret.asOk().isInvalid()) {
        continue;
      }
      requests.emplace_back(std::move(ret).take(), line[0] == '<');
    } else {
      content += line;
      content += '\n';
    }
  }
  if (!content.empty()) {
    auto ret = parseJSON(fileName, content, lineNumOffset);
    if (!ret) {
      return Err(std::move(ret).takeError());
    }
    requests.emplace_back(std::move(ret).take(), false);
  }
  return Ok(std::move(requests));
}

// ####################
// ##     Client     ##
// ####################

void Client::run(const std::vector<ClientRequest> &requests) {
  for (auto &e : requests) {
    bool r = this->send(e.request);
    if (!r) {
      this->transport.getLogger()(LogLevel::FATAL, "request sending failed");
    }
    if (e.waitReply) {
      auto ret = this->recv();
      if (!this->replyCallback) {
        continue;
      }
      if (!this->replyCallback(std::move(ret))) {
        return;
      }
    }
  }
}

bool Client::send(const JSON &json) {
  auto value = json.serialize();
  auto writeSize = this->transport.send(value.size(), value.c_str());
  return writeSize > -1 && static_cast<size_t>(writeSize) >= value.size();
}

rpc::Message Client::recv() {
  int dataSize = this->transport.recvSize();
  if (dataSize < 0) {
    std::string error = "may be broken or empty message";
    return rpc::Error(rpc::ErrorCode::InternalError, std::move(error));
  }

  ByteBuffer buf;
  for (int remainSize = dataSize; remainSize > 0;) {
    char data[256];
    constexpr int bufSize = std::size(data);
    int needSize = remainSize < bufSize ? remainSize : bufSize;
    int recvSize = this->transport.recv(needSize, data);
    if (recvSize < 0) {
      std::string error = "message receiving failed";
      return rpc::Error(rpc::ErrorCode::InternalError, std::move(error));
    }
    buf.append(data, static_cast<unsigned int>(recvSize));
    remainSize -= recvSize;
  }
  return rpc::MessageParser(this->transport.getLogger(), std::move(buf))();
}

} // namespace ydsh::lsp