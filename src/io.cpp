/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#include <unistd.h>

#include <iterator>

#include "io.h"
#include "misc/flag_util.hpp"

#ifdef __linux__

#include <poll.h>

/**
 *
 * @param fd
 * @param timeoutMSec
 * @param mask
 * @return
 * if input is ready, return 0
 * if error, return -1 and set errno
 * if timeout, return -2
 */
static int waitForInputReadyImpl(const int fd, const int timeoutMSec, const sigset_t *mask) {
  struct pollfd fds[1];
  fds[0].fd = fd;
  fds[0].events = POLLIN;
  const timespec timespec = {
      .tv_sec = timeoutMSec / 1000,
      .tv_nsec = static_cast<long>(timeoutMSec) % 1000 * 1000 * 1000,
  };
  int ret = ppoll(fds, std::size(fds), timeoutMSec < 0 ? nullptr : &timespec, mask);
  if (ret <= 0) {
    if (ret == 0) {
      return -2;
    }
    return -1;
  }
  return 0;
}

#else

#include <sys/select.h>

/**
 * for macOS.
 * macOS poll is completely broken for pty
 * @param fd
 * @param timeoutMSec
 * @return
 * if input is ready, return 0
 * if error, return -1 and set errno
 * if timeout, return -2
 */
static int waitForInputReadyImpl(const int fd, const int timeoutMSec, const sigset_t *mask) {
  fd_set fds;
  const timespec timespec = {
      .tv_sec = timeoutMSec / 1000,
      .tv_nsec = static_cast<long>(timeoutMSec) % 1000 * 1000 * 1000,
  };
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  int ret = pselect(fd + 1, &fds, nullptr, nullptr, timeoutMSec < 0 ? nullptr : &timespec, mask);
  if (ret <= 0) {
    if (ret == 0) {
      return -2;
    }
    return -1;
  }
  return 0;
}

#endif

namespace arsh {

int waitForInputReady(const int fd, const int timeoutMSec, const sigset_t *mask) {
  return waitForInputReadyImpl(fd, timeoutMSec, mask);
}

ssize_t readWith(const int fd, char *buf, const size_t bufSize, const ReadWithParam param) {
  if (param.timeoutMSec > -1) {
    while (true) {
      errno = 0;
      const int r = waitForInputReady(fd, param.timeoutMSec, nullptr);
      if (r != 0) {
        if (r == -1 && errno == EINTR && hasFlag(param.retry, ReadRetry::RETRY_EINTR)) {
          continue;
        }
        return r;
      }
      break;
    }
  }
  while (true) {
    errno = 0;
    const ssize_t readSize = read(fd, buf, bufSize);
    if (readSize < 0) {
      if (errno == EINTR && hasFlag(param.retry, ReadRetry::RETRY_EINTR)) {
        continue;
      }
      if (errno == EAGAIN && hasFlag(param.retry, ReadRetry::RETRY_EAGAIN)) {
        continue;
      }
    }
    return readSize;
  }
}

bool writeAll(const int fd, const void *data, const size_t size) {
  size_t total = 0;
  while (total < size) {
    ssize_t n = write(fd, static_cast<const char *>(data) + total, size - total);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    total += static_cast<size_t>(n);
  }
  return true;
}

} // namespace arsh
