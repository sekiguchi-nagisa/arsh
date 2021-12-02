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

ssize_t LSPTransport::send(unsigned int size, const char *data) {
  if (size == 0) {
    return 0;
  }
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
  return -1;
}

static bool readLine(FILE *fp, std::string &header) {
  while (true) {
    char ch;
    errno = 0;
    if (fread(&ch, 1, 1, fp) != 1) {
      if (ferror(fp) && (errno == EINTR || errno == EAGAIN)) {
        clearerr(fp);
        continue;
      }
      return false;
    }

    if (ch == '\n' && !header.empty() && header.back() == '\r') {
      header.pop_back();
      break;
    } else if (ch == '\0') {
      break;
    } else {
      header += ch;
    }
  }
  return true;
}

ssize_t LSPTransport::recvSize() {
  if (feof(this->input.get()) || ferror(this->input.get())) {
    LOG(LogLevel::ERROR, "io stream reach eof or fatal error. terminate immediately");
    exit(1);
  }

  int size = 0;
  while (true) {
    std::string header;
    if (!readLine(this->input.get(), header)) {
      LOG(LogLevel::ERROR, "invalid header: %s", header.c_str());
      return -1;
    }

    if (header.empty()) {
      break;
    }
    if (isContentLength(header)) {
      LOG(LogLevel::INFO, "%s", header.c_str());
      if (size > 0) {
        LOG(LogLevel::WARNING, "previous read message length: %d", size);
      }
      int ret = parseContentLength(header);
      if (ret < 0) {
        LOG(LogLevel::ERROR, "may be broken content length");
      }
      size = ret;
    } else { // may be other header
      LOG(LogLevel::WARNING, "other header: %s", header.c_str());
    }
  }
  return size;
}

ssize_t LSPTransport::recv(unsigned int size, char *data) {
  return fread(data, sizeof(char), size, this->input.get());
}

} // namespace ydsh::lsp