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
#include <sys/uio.h>

#include <constant.h>
#include <misc/num_util.hpp>

#include "transport.h"

namespace arsh::lsp {

#define LOG(L, ...)                                                                                \
  do {                                                                                             \
    this->logger.get().enabled(L) && (this->logger.get())(L, __VA_ARGS__);                         \
  } while (false)

static constexpr char HEADER_LENGTH[] = "Content-Length: ";

// ##########################
// ##     LSPTransport     ##
// ##########################

LSPTransport::LSPTransport(LoggerBase &logger, int inFd, int outFd) : rpc::Transport(logger) {
  if (inFd < 0) {
    fatal_perror("broken input");
  }
  this->inputFd = inFd;

  if (outFd < 0) {
    fatal_perror("broken output");
  }
  this->outputFd = outFd;
}

LSPTransport::~LSPTransport() {
  close(this->inputFd);
  close(this->outputFd);
}

ssize_t LSPTransport::send(size_t size, const char *data) {
  if (size == 0 || data == nullptr) {
    return 0;
  }

  if (size > SYS_LIMIT_INPUT_SIZE) {
    errno = ENOMEM;
    return -1;
  }

  // header
  std::string header;
  header.reserve(64);
  header += HEADER_LENGTH;
  header += std::to_string(size);
  header += "\r\n\r\n";

  const iovec vec[] = {
      {.iov_base = header.data(), .iov_len = header.size()},
      {.iov_base = const_cast<char *>(data), .iov_len = size},
  };
  if (const ssize_t writeSize = writev(this->outputFd, vec, std::size(vec));
      writeSize < static_cast<ssize_t>(size + header.size())) {
    return -1;
  }
  errno = 0;
  if (fsync(this->outputFd) != 0 && errno != EINVAL &&
      errno != ENOTSUP) { // ignore EINVAL/ENOTSUP for pipe, socket
    return -1;
  }
  return static_cast<ssize_t>(size);
}

static bool isContentLength(const std::string &line) {
  return strncmp(line.c_str(), HEADER_LENGTH, std::size(HEADER_LENGTH) - 1) == 0 &&
         line.size() == strlen(line.c_str());
}

static int parseContentLength(const std::string &line) {
  const char *ptr = line.c_str();
  ptr += std::size(HEADER_LENGTH) - 1;
  auto ret = convertToNum10<int32_t>(ptr);
  if (ret && ret.value >= 0) {
    return ret.value;
  }
  return -1;
}

static bool readLine(int fd, std::string &header) {
  while (true) {
    char ch;
    errno = 0;
    ssize_t readSize = read(fd, &ch, 1);
    if (readSize <= 0) {
      if (readSize == -1 && (errno == EINTR || errno == EAGAIN)) {
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
    if (!readLine(this->inputFd, header)) {
      LOG(LogLevel::ERROR, "invalid header: `%s', size: %zu", header.c_str(), header.size());
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

ssize_t LSPTransport::recv(size_t size, char *data) { return read(this->inputFd, data, size); }

} // namespace arsh::lsp