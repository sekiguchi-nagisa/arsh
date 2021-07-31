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

#include <misc/num_util.hpp>

#include "transport.h"

namespace ydsh::lsp {

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    this->logger.get().enabled(L) && (this->logger.get())(L, __VA_ARGS__);                         \
  } while (false)

static constexpr const char HEADER_LENGTH[] = "Content-Length: ";

// ##########################
// ##     LSPTransport     ##
// ##########################

int LSPTransport::send(unsigned int size, const char *data) {
  std::string header = HEADER_LENGTH;
  header += std::to_string(size);
  header += "\r\n";
  header += "\r\n";

  fwrite(header.c_str(), sizeof(char), header.size(), this->output.get());
  int writeSize = fwrite(data, sizeof(char), size, this->output.get());
  fflush(this->output.get());
  return writeSize; // FIXME: error checking.
}

static bool isContentLength(const std::string &line) {
  return strncmp(line.c_str(), HEADER_LENGTH, std::size(HEADER_LENGTH) - 1) == 0 &&
         line.size() == strlen(line.c_str());
}

static int parseContentLength(const std::string &line) {
  const char *ptr = line.c_str();
  ptr += std::size(HEADER_LENGTH) - 1;
  auto ret = convertToNum<int32_t>(ptr);
  if (ret.second && ret.first >= 0) {
    return ret.first;
  }
  return 0;
}

int LSPTransport::recvSize() {
  int size = 0;
  while (true) {
    std::string header;
    if (!this->readHeader(header)) {
      LOG(LogLevel::ERROR, "invalid header: %s", header.c_str());
      return -1;
    }

    if (header.empty()) {
      break;
    }
    if (isContentLength(header)) {
      LOG(LogLevel::DEBUG, "%s", header.c_str());
      if (size > 0) {
        LOG(LogLevel::WARNING, "previous read message length: %d", size);
      }
      int ret = parseContentLength(header);
      if (!ret) {
        LOG(LogLevel::ERROR, "may be broken content length");
      }
      size = ret;
    } else { // may be other header
      LOG(LogLevel::INFO, "other header: %s", header.c_str());
    }
  }
  return size;
}

int LSPTransport::recv(unsigned int size, char *data) {
  return fread(data, sizeof(char), size, this->input.get());
}

bool LSPTransport::readHeader(std::string &header) {
  clearerr(this->input.get());
  char prev = '\0';
  while (true) {
    signed char ch;
    if (fread(&ch, 1, 1, this->input.get()) != 1) {
      if (ferror(this->input.get()) && (errno == EINTR || errno == EAGAIN)) {
        continue;
      }
      return false;
    }

    if (ch == '\n' && prev == '\r') {
      header.pop_back(); // pop \r
      break;
    }
    prev = ch;
    header += ch;
  }
  return true;
}

} // namespace ydsh::lsp