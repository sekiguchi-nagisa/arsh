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

#ifndef YDSH_REDIR_H
#define YDSH_REDIR_H

#include <cstdio>

#include "job.h"
#include "misc/files.hpp"
#include "object.h"

namespace ydsh {

constexpr unsigned int READ_PIPE = 0;
constexpr unsigned int WRITE_PIPE = 1;

inline void tryToDup(int srcFd, int targetFd) {
  if (srcFd > -1) {
    dup2(srcFd, targetFd);
  }
}

inline void tryToClose(int fd) {
  if (fd > -1) {
    close(fd);
  }
}

using pipe_t = int[2];

inline void tryToClose(pipe_t &pipefds) {
  tryToClose(pipefds[0]);
  tryToClose(pipefds[1]);
}

inline void tryToPipe(pipe_t &pipefds, bool openPipe) {
  if (openPipe) {
    if (pipe(pipefds) < 0) {
      perror("pipe creation failed\n");
      exit(1); // FIXME: throw exception
    }
  } else {
    pipefds[0] = -1;
    pipefds[1] = -1;
  }
}

inline void initAllPipe(unsigned int size, pipe_t *pipes) {
  for (unsigned int i = 0; i < size; i++) {
    tryToPipe(pipes[i], true);
  }
}

inline void closeAllPipe(unsigned int size, pipe_t *pipefds) {
  for (unsigned int i = 0; i < size; i++) {
    tryToClose(pipefds[i]);
  }
}

inline void redirInToNull() {
  int fd = open("/dev/null", O_RDONLY);
  dup2(fd, STDIN_FILENO);
  close(fd);
}

struct PipeSet {
  pipe_t in{-1};
  pipe_t out{-1};

  explicit PipeSet(ForkKind kind) { // FIXME: error reporting
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
    tryToPipe(this->in, useInPipe);
    tryToPipe(this->out, useOutPipe);
  }

  /**
   * only call once in child
   */
  void setupChildStdin(ForkKind forkKind, bool jobctl) {
    tryToDup(this->in[READ_PIPE], STDIN_FILENO);
    if ((forkKind == ForkKind::DISOWN || forkKind == ForkKind::JOB) && !jobctl) {
      redirInToNull();
    }
  }

  /**
   * only call once in child
   */
  void setupChildStdout() { tryToDup(this->out[WRITE_PIPE], STDOUT_FILENO); }

  /**
   * call in parent and child
   */
  void closeAll() {
    tryToClose(this->in);
    tryToClose(this->out);
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
  DSState &state;
  Job entry;

public:
  NON_COPYABLE(PipelineObject);

  PipelineObject(DSState &state, Job &&entry)
      : ObjectWithRtti(TYPE::Void), state(state), entry(std::move(entry)) {}

  ~PipelineObject();
};

/**
 * for io redirection
 */
class RedirObject : public ObjectWithRtti<ObjectKind::Redir> {
public:
  struct Entry {
    DSValue value;
    RedirOp op;
    int newFd; // ignore it when op is REDIR_OUT_ERR, APPEND_OUT_ERR or CLOBBER_OUT_ERR
  };

  static constexpr unsigned int MAX_FD_NUM = 2;

private:
  unsigned int backupFDSet{0}; // if corresponding bit is set, backup old fd

  int oldFds[MAX_FD_NUM + 1];

  std::vector<Entry> entries;

public:
  NON_COPYABLE(RedirObject);

  RedirObject() : ObjectWithRtti(TYPE::Void) { // NOLINT
    for (size_t i = 0; i < std::size(this->oldFds); i++) {
      this->oldFds[i] = -1;
    }
  }

  ~RedirObject();

  void addEntry(DSValue &&value, RedirOp op, int newFd);

  void ignoreBackup() { this->backupFDSet = 0; }

  bool redirect(DSState &state);

private:
  void saveFDs() {
    for (unsigned int i = 0; i < std::size(this->oldFds); i++) {
      if (this->backupFDSet & (1u << i)) {
        this->oldFds[i] = dupFDCloseOnExec(static_cast<int>(i));
      }
    }
  }

  void restoreFDs() {
    for (unsigned int i = 0; i < std::size(this->oldFds); i++) {
      if (this->backupFDSet & (1u << i)) {
        dup2(this->oldFds[i], static_cast<int>(i));
      }
    }
  }
};

} // namespace ydsh

#endif // YDSH_REDIR_H
