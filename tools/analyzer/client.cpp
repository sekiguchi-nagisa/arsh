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

#include <poll.h>

#include <chrono>
#include <fstream>
#include <regex>
#include <thread>

#include <misc/files.hpp>
#include <misc/num_util.hpp>

#include "../tools/process/process.h"
#include "../tools/uri/uri.h"

#include "client.h"
#include "lsp.h"

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

static bool matchSectionEnd(const std::string &line, std::smatch &match) {
  static std::regex re(R"(^(<<<|---)[ \t]*(\d*)[ \t]*$)", std::regex_constants::ECMAScript);
  return std::regex_match(line, match, re);
}

static bool isSectionEnd(const std::string &line) {
  std::smatch match;
  return matchSectionEnd(line, match);
}

static IntConversionResult<unsigned int> parseNum(const std::string &line) {
  std::smatch match;
  if (matchSectionEnd(line, match) && match.length(2) > 0) {
    auto value = match.str(2);
    return convertToDecimal<unsigned int>(value.c_str());
  }
  return {
      .kind = IntConversionStatus::ILLEGAL_CHAR,
      .consumedSize = 0,
      .value = 0,
  };
}

static ClientInput loadWholeFile(const std::string &fileName, std::istream &input,
                                 unsigned int waitTime) {
  ClientInput clientInput = {
      .fileName = getRealpath(fileName.c_str()).get(),
      .req = {},
      .directive = directive::Directive(),
  };
  if (!directive::Directive::init(fileName.c_str(), clientInput.directive.unwrap())) {
    fatal("broken directive\n");
  }

  int64_t id = 0;

  // send 'initialize' request
  {
    InitializeParams params;
    params.processId = getpid();
    params.rootUri = "file:///test";

    JSONSerializer serializer;
    serializer(params);

    clientInput.req.emplace_back(
        rpc::Request(id, "initialize", std::move(serializer).take()).toJSON(), 0);
  }

  // send 'textDocument/didOpen' notification
  {
    std::string content;
    for (std::string line; getline(input, line);) {
      content += line;
      content += '\n';
    }

    DidOpenTextDocumentParams params;
    std::string value = getRealpath(fileName.c_str()).get();
    auto uri = uri::URI::fromPath("file", std::move(value)).toString();
    if (content.size() > static_cast<size_t>(1024 * 1024) && waitTime < 2000) {
      waitTime = 2000;
    }

    params.textDocument = TextDocumentItem{
        .uri = std::move(uri),
        .languageId = "ydsh",
        .version = 1,
        .text = std::move(content),
    };

    JSONSerializer serializer;
    serializer(params);

    clientInput.req.emplace_back(
        rpc::Request("textDocument/didOpen", std::move(serializer).take()).toJSON(), waitTime);
  }

  // send 'shutdown' request
  clientInput.req.emplace_back(rpc::Request(++id, "shutdown", JSON()).toJSON(), 10);

  // send 'exit' notification
  clientInput.req.emplace_back(rpc::Request("exit", JSON()).toJSON(), 10);

  return clientInput;
}

Result<ClientInput, std::string> loadInputScript(const std::string &fileName, bool open,
                                                 unsigned int waitTime) {
  std::ifstream input(fileName);
  if (!input) {
    std::string error = "cannot read: ";
    error += fileName;
    return Err(std::move(error));
  }
  if (open) {
    return Ok(loadWholeFile(fileName, input, waitTime));
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
    if (isSectionEnd(line)) {
      auto ret = parseJSON(fileName, content, lineNumOffset);
      if (!ret) {
        return Err(std::move(ret).takeError());
      }
      content = "";
      lineNumOffset = 0;
      if (ret.asOk().isInvalid()) { // skip empty
        continue;
      }
      auto pair = parseNum(line);
      unsigned int n = pair ? pair.value : 0;
      requests.emplace_back(std::move(ret).take(), n);
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
    requests.emplace_back(std::move(ret).take(), 0);
  }
  return Ok(ClientInput{.fileName = fileName, .req = std::move(requests), .directive = {}});
}

// ####################
// ##     Client     ##
// ####################

static bool waitReply(int fd, int timeout) {
  while (true) {
    struct pollfd pollfd[1]{};
    pollfd[0].fd = fd;
    pollfd[0].events = POLLIN;

    int ret = poll(pollfd, 1, timeout);
    if (ret <= 0) {
      if (ret == -1 && errno == EINTR) {
        continue;
      }
      break;
    }
    const auto revents = pollfd[0].revents;
    if (revents & POLLERR || revents & POLLHUP || revents & POLLNVAL) {
      return false;
    }
    if (revents & POLLIN) {
      return true;
    }
    break;
  }
  return false;
}

void Client::run(const ClientInput &input) {
  const unsigned int size = input.req.size();
  for (unsigned int index = 0; index < size; index++) {
    auto &req = input.req[index];
    if (this->transport.getLogger().enabled(LogLevel::DEBUG)) {
      std::string v = req.request.serialize(2);
      this->transport.getLogger()(LogLevel::DEBUG, "%s", v.c_str());
    }
    if (!this->send(req.request)) {
      this->transport.getLogger()(LogLevel::FATAL, "request sending failed: `%s'", strerror(errno));
    }
    if (req.msec > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(req.msec));
    }
    int timeout = index == size - 1 ? 200 : 50;
    while (waitReply(this->transport.getInputFd(), timeout)) {
      auto ret = this->recv();
      assert(ret.hasValue());
      if (this->replyCallback) {
        if (!this->replyCallback(std::move(ret))) {
          return;
        }
      }
    }
  }
}

bool Client::send(const JSON &json) {
  auto value = json.serialize();
  errno = 0;
  auto writeSize = this->transport.send(value.size(), value.c_str());
  return writeSize > -1 && static_cast<size_t>(writeSize) >= value.size();
}

static constexpr const char *ERROR_BROKEN_OR_EMPTY = "may be broken or empty message";

bool Client::isBrokenOrEmpty(const rpc::Error &error) {
  return error.code == rpc::ErrorCode::InternalError && error.message == ERROR_BROKEN_OR_EMPTY;
}

rpc::Message Client::recv() {
  ssize_t dataSize = this->transport.recvSize();
  if (dataSize < 0) {
    std::string error = ERROR_BROKEN_OR_EMPTY;
    return rpc::Error(rpc::ErrorCode::InternalError, std::move(error));
  }

  ByteBuffer buf;
  for (ssize_t remainSize = dataSize; remainSize > 0;) {
    char data[256];
    constexpr ssize_t bufSize = std::size(data);
    ssize_t needSize = remainSize < bufSize ? remainSize : bufSize;
    ssize_t recvSize = this->transport.recv(needSize, data);
    if (recvSize < 0) {
      std::string error = "message receiving failed";
      return rpc::Error(rpc::ErrorCode::InternalError, std::move(error));
    }
    buf.append(data, static_cast<unsigned int>(recvSize));
    remainSize -= recvSize;
  }
  return rpc::MessageParser(this->transport.getLogger(), std::move(buf))();
}

// ####################################
// ##     TestClientServerDriver     ##
// ####################################

static void collectDiagnostic(JSON &&json, std::vector<PublishDiagnosticsParams> &results) {
  JSONDeserializer deserializer(std::move(json));
  PublishDiagnosticsParams params;
  deserializer(params);
  results.push_back(std::move(params));
}

static bool expectDiagnostic(const std::vector<PublishDiagnosticsParams> &values,
                             const directive::Directive &directive,
                             const std::string &targetFileName, ClientLogger &logger) {
  if (logger.enabled(LogLevel::DEBUG)) {
    logger(LogLevel::DEBUG, "directive: (%s, %u, %u)", directive.getErrorKind().c_str(),
           directive.getLineNum(), directive.getChars());
  }

  for (auto &v : values) {
    auto uri = uri::URI::parse(v.uri);
    if (uri.getPath() != targetFileName) {
      logger(LogLevel::DEBUG, "uri: %s\ntarget: %s", v.uri.c_str(), targetFileName.c_str());
      continue;
    }

    for (auto &d : v.diagnostics) {
      auto &startPos = d.range.start;
      if (logger.enabled(LogLevel::DEBUG)) {
        logger(LogLevel::DEBUG, "diagnostic: (%s, %d + 1, %d + 1)", d.code.c_str(), startPos.line,
               startPos.character);
      }
      if (StringRef(d.code).endsWith(directive.getErrorKind()) &&
          directive.getLineNum() == static_cast<unsigned int>(startPos.line) + 1 &&
          directive.getChars() == static_cast<unsigned int>(startPos.character) + 1) {
        return true;
      }
    }
  }
  return false;
}

static std::string getTargetFileName(const directive::Directive &directive,
                                     const std::string &targetFileName) {
  auto &fileName = directive.getFileName();
  auto &target = fileName.empty() ? targetFileName : fileName;
  auto targetFullPath = getRealpath(target.c_str());
  if (!targetFullPath) {
    fatal_perror("broken target: %s", target.c_str());
  }
  return targetFullPath.get();
}

int TestClientServerDriver::run(const DriverOptions &options,
                                std::function<int(const DriverOptions &)> &&func) {
  using namespace process;
  IOConfig ioConfig;
  ioConfig.in = IOConfig::PIPE;
  ioConfig.out = IOConfig::PIPE;
  auto proc = ProcBuilder::spawn(ioConfig, [&func, &options] { return func(options); });

  ClientLogger logger;
  logger.setSeverity(this->level);
  logger(LogLevel::INFO, "run lsp test client");
  Client client(logger, dup(proc.out()), dup(proc.in()));
  std::vector<PublishDiagnosticsParams> receivedDiagnostics;
  client.setReplyCallback([&logger, &options, &receivedDiagnostics](rpc::Message &&msg) -> bool {
    if (is<rpc::Error>(msg)) {
      auto &error = get<rpc::Error>(msg);
      if (Client::isBrokenOrEmpty(error)) {
        logger(LogLevel::INFO, "%s", error.toString().c_str());
        return false;
      }
      prettyPrint(error.toJSON());
    } else if (is<rpc::Request>(msg)) {
      auto &req = get<rpc::Request>(msg);
      prettyPrint(req.toJSON());
      if (options.open && req.method == "textDocument/publishDiagnostics") {
        collectDiagnostic(std::move(req.params.unwrap()), receivedDiagnostics);
      }
    } else if (is<rpc::Response>(msg)) {
      auto &res = get<rpc::Response>(msg);
      prettyPrint(res.toJSON());
    } else {
      fatal("broken\n");
    }
    return true;
  });
  client.run(this->clientInput);
  proc.waitWithTimeout(100);
  if (proc) {
    logger(LogLevel::INFO, "kill lsp server");
    proc.kill(SIGKILL);
  }
  const auto ret = proc.wait();

  if (options.open) {
    assert(!receivedDiagnostics.empty());
    auto kind = static_cast<DSErrorKind>(this->clientInput.directive.unwrap().getKind());
    if (kind == DS_ERROR_KIND_PARSE_ERROR || kind == DS_ERROR_KIND_TYPE_ERROR) {
      auto targetFile =
          getTargetFileName(this->clientInput.directive.unwrap(), this->clientInput.fileName);
      if (!expectDiagnostic(receivedDiagnostics, this->clientInput.directive.unwrap(), targetFile,
                            logger)) {
        return 255;
      }
    }
  }

  return ret.toShellStatus();
}

} // namespace ydsh::lsp