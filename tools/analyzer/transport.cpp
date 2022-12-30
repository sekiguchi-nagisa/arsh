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

#include <poll.h>

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
  if (size == 0 || data == nullptr) {
    return 0;
  }
  if (fprintf(this->output.get(), "%s%d\r\n\r\n%s", HEADER_LENGTH, size, data) < 0) {
    return -1;
  }
  if (fflush(this->output.get()) != 0) {
    return -1;
  }
  return size;
}

static bool isContentLength(const std::string &line) {
  return strncmp(line.c_str(), HEADER_LENGTH, std::size(HEADER_LENGTH) - 1) == 0 &&
         line.size() == strlen(line.c_str());
}

static int parseContentLength(const std::string &line) {
  const char *ptr = line.c_str();
  ptr += std::size(HEADER_LENGTH) - 1;
  auto ret = convertToDecimal<int32_t>(ptr);
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
  int size = 0;
  while (true) {
    std::string header;
    if (!readLine(this->input.get(), header)) {
      LOG(LogLevel::ERROR, "invalid header: `%s', size: %zu", header.c_str(), header.size());
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

bool LSPTransport::available() const {
  return !feof(this->input.get()) && !ferror(this->input.get());
}

bool LSPTransport::poll(int timeout) {
  struct pollfd pollfd[1]{};
  pollfd[0].fd = fileno(this->input.get());
  pollfd[0].events = POLLIN;
  while (true) {
    LOG(LogLevel::DEBUG, "poll: %d", timeout);
    int ret = ::poll(pollfd, 1, timeout);
    if (ret == 0) {
      return false;
    } else if (ret == -1 && errno == EINTR) {
      continue;
    }
    break;
  }
  return true;
}

} // namespace ydsh::lsp