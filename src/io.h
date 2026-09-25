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

#include <sys/uio.h>

#include <csignal>
#include <cstdio>

#include "misc/enum_util.hpp"
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

enum class ReadRetry : unsigned char {
  NONE = 0u,
  RETRY_EAGAIN = 1u << 0u,
  RETRY_EINTR = 1u << 1u,
  RETRY_ALL = RETRY_EAGAIN | RETRY_EINTR,
};

struct ReadWithParam {
  ReadRetry retry{ReadRetry::NONE};
  int timeoutMSec{-1}; // if negative, no-timeout
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
ssize_t readWith(int fd, char *buf, size_t bufSize, ReadWithParam param);

inline ssize_t readRetryWithTimeout(const int fd, char *buf, const size_t bufSize,
                                    const int timeoutMSec) {
  return readWith(fd, buf, bufSize, {ReadRetry::RETRY_ALL, timeoutMSec});
}

inline ssize_t readRetryEAGAINWithTimeout(const int fd, char *buf, const size_t bufSize,
                                          const int timeoutMSec) {
  return readWith(fd, buf, bufSize, {ReadRetry::RETRY_EAGAIN, timeoutMSec});
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

/**
 * write all content to fd
 * if EINTR, retry write
 * if fd is non-blocking and not ready to write, return false and set errno to EAGAIN
 * after call it, vec maybe modified
 * @param fd
 * @param vec
 * @param size
 * @return
 * if failed, return false and set errno
 */
[[nodiscard]] bool writevAll(int fd, iovec *vec, unsigned short size);

} // namespace arsh

template <>
struct arsh::allow_enum_bitop<arsh::ReadRetry> : std::true_type {};

#endif // ARSH_IO_H
