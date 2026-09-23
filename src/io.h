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

#ifndef ARSH_IO_H
#define ARSH_IO_H

#include <csignal>
#include <cstdio>

#include "misc/string_ref.hpp"

namespace arsh {

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
int waitForInputReady(int fd, int timeoutMSec, const sigset_t *mask);

struct ReadWithTimeoutParam {
  bool retry;
  int timeoutMSec;
};

/**
 *
 * @param fd
 * @param buf
 * @param bufSize
 * @param param
 * @return
 * if timeout, return -2
 * if error, return -1 and set errno
 * otherwise, return non-negative number
 */
ssize_t readWithTimeout(int fd, char *buf, size_t bufSize, ReadWithTimeoutParam param);

inline ssize_t readRetryWithTimeout(int fd, char *buf, size_t bufSize, int timeoutMSec) {
  return readWithTimeout(fd, buf, bufSize, {.retry = true, .timeoutMSec = timeoutMSec});
}

/**
 * write all content to fd
 * if EINTR, retry write
 * if fd is non-blocking and not ready to write, return false and set errno to EAGAIN
 * @param fd
 * @param data
 * @param size
 * @return
 * if failed, return false and set errno
 */
[[nodiscard]] bool writeAll(int fd, const void *data, size_t size);

[[nodiscard]] inline bool writeAll(const int fd, const StringRef ref) {
  return writeAll(fd, ref.data(), ref.size());
}

inline auto fwriteStrRef(FILE *fp, const StringRef ref) {
  return fwrite(ref.data(), sizeof(char), ref.size(), fp);
}

} // namespace arsh

#endif // ARSH_IO_H
