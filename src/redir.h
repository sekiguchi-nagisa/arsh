/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#ifndef ARSH_REDIR_H
#define ARSH_REDIR_H

#include <cstdio>

#include "job.h"
#include "misc/files.hpp"
#include "misc/inlined_array.hpp"
#include "object.h"

namespace arsh {

enum class PipeAccessor : unsigned char {};
constexpr auto READ_PIPE = PipeAccessor{0};
constexpr auto WRITE_PIPE = PipeAccessor{1};

[[nodiscard]] inline bool tryToDup(int srcFd, int targetFd) {
  if (srcFd > -1) {
    return dup2(srcFd, targetFd) > -1;
  }
  return true;
}

class Pipe {
private:
  int fds[2]{-1, -1};

public:
  /**
   * open pipe with CLOEXEC
   * @return
   * if has error, return false
   */
  [[nodiscard]] bool open();

  [[nodiscard]] bool tryToOpen(const bool shouldOpen) {
    if (shouldOpen) {
      return this->open();
    }
    return true;
  }

  int &operator[](PipeAccessor i) { return this->fds[toUnderlying(i)]; }

  void close() {
    this->close(READ_PIPE);
    this->close(WRITE_PIPE);
  }

  void close(PipeAccessor i) { tryToClose(this->fds[toUnderlying(i)]); }

private:
  static void tryToClose(int &fd) {
    if (fd > -1) {
      ::close(fd);
      fd = -1;
    }
  }
};

class PipeList : public InlinedArray<Pipe, 6> {
public:
  explicit PipeList(size_t size) : InlinedArray(size) {}

  [[nodiscard]] bool openAll() {
    for (size_t i = 0; i < this->size(); i++) {
      if (!(*this)[i].open()) {
        const int old = errno;
        this->closeAll();
        errno = old;
        return false;
      }
    }
    return true;
  }

  void closeAll() {
    for (size_t i = 0; i < this->size(); i++) {
      (*this)[i].close();
    }
  }
};

struct PipeSet {
  Pipe in;
  Pipe out;

  [[nodiscard]] bool openAll(ForkKind kind) {
    bool useInPipe = false;
    bool useOutPipe = false;

    switch (kind) {
    case ForkKind::STR:
    case ForkKind::ARRAY:
      useOutPipe = true;
      break;
    case ForkKind::IN_PIPE:
      useInPipe = true;
      break;
    case ForkKind::OUT_PIPE:
      useOutPipe = true;
      break;
    case ForkKind::COPROC:
      useInPipe = true;
      useOutPipe = true;
      break;
    case ForkKind::JOB:
    case ForkKind::DISOWN:
    case ForkKind::NONE:
    case ForkKind::PIPE_FAIL:
      break;
    }
    if (!this->in.tryToOpen(useInPipe) || !this->out.tryToOpen(useOutPipe)) {
      const int old = errno;
      this->closeAll();
      errno = old;
      return false;
    }
    return true;
  }

  /**
   * only call once in child
   */
  [[nodiscard]] bool setupChildStdin(ForkKind forkKind, bool jobctl) {
    if (!tryToDup(this->in[READ_PIPE], STDIN_FILENO)) {
      return false;
    }
    if ((forkKind == ForkKind::DISOWN || forkKind == ForkKind::JOB) && !jobctl) {
      // redirect stdin to null
      int fd = open("/dev/null", O_RDONLY);
      const bool s = fd > -1 && dup2(fd, STDIN_FILENO) > -1;
      const int old = errno;
      close(fd);
      errno = old;
      return s;
    }
    return true;
  }

  /**
   * only call once in child
   */
  [[nodiscard]] bool setupChildStdout() { return tryToDup(this->out[WRITE_PIPE], STDOUT_FILENO); }

  /**
   * call in parent and child
   */
  void closeAll() {
    this->in.close();
    this->out.close();
  }
};

inline void flushStdFD() {
  fflush(stdin);
  fflush(stdout);
  fflush(stderr);
}

/**
 * for pipeline
 */
class PipelineObject : public ObjectWithRtti<ObjectKind::Pipeline> {
private:
  ARState &state;
  Job entry;

public:
  NON_COPYABLE(PipelineObject);

  PipelineObject(ARState &state, Job &&entry)
      : ObjectWithRtti(TYPE::Void), state(state), entry(std::move(entry)) {}

  ~PipelineObject();

  Job syncStatusAndDispose();
};

/**
 * for io redirection
 */
class RedirObject : public ObjectWithRtti<ObjectKind::Redir> {
public:
  struct Entry {
    Value value;
    RedirOp op;
    int newFd; // ignore it when op is REDIR_OUT_ERR, APPEND_OUT_ERR or CLOBBER_OUT_ERR
  };

  static constexpr int MAX_FD_NUM = 9;

private:
  StaticBitSet<uint16_t> backupFDSet; // if corresponding bit is set, backup old fd

  bool saved{false};

  int oldFds[MAX_FD_NUM + 1];

  std::vector<Entry> entries;

  static_assert(decltype(backupFDSet)::checkRange(MAX_FD_NUM));

public:
  NON_COPYABLE(RedirObject);

  RedirObject() : ObjectWithRtti(TYPE::Void) { // NOLINT
    for (size_t i = 0; i < std::size(this->oldFds); i++) {
      this->oldFds[i] = -1;
    }
  }

  ~RedirObject();

  void addEntry(Value &&value, RedirOp op, int newFd);

  void ignoreBackup() { this->backupFDSet.clear(); }

  bool redirect(ARState &state);

private:
  [[nodiscard]] bool saveFDs() {
    for (unsigned int i = 0; i < std::size(this->oldFds); i++) {
      if (this->backupFDSet.has(i)) {
        this->oldFds[i] = dupFDCloseOnExec(static_cast<int>(i));
        if (this->oldFds[i] < 0 && errno != EBADF) {
          return false;
        }
      }
    }
    this->saved = true;
    return true;
  }

  void restoreFDs() {
    if (!this->saved) {
      return;
    }
    for (unsigned int i = 0; i < std::size(this->oldFds); i++) {
      if (this->backupFDSet.has(i)) {
        int oldFd = this->oldFds[i];
        int fd = static_cast<int>(i);
        if (oldFd < 0) {
          close(fd);
        } else {
          dup2(oldFd, fd); // FIXME: report error
        }
      }
    }
  }
};

} // namespace arsh

#endif // ARSH_REDIR_H
