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

#include <climits>
#include <sys/wait.h>

#include "logger.h"
#include "redir.h"
#include "vm.h"

namespace arsh {

PipelineObject::~PipelineObject() {
  /**
   * due to prevent write blocking of child processes, force to restore stdin before call wait.
   * in some situation, raise SIGPIPE in child processes.
   */
  bool restored = this->entry->restoreStdin();
  auto waitOp = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp ::BLOCKING;
  this->state.jobTable.waitForJob(this->entry, waitOp);
  this->state.updatePipeStatus(this->entry->getProcSize(), this->entry->getProcs(), true);

  if (restored) {
    int ret = this->state.tryToBeForeground();
    LOG(DUMP_EXEC, "tryToBeForeground: %d, %s", ret, strerror(errno));
  }
  this->state.jobTable.waitForAny();
}

RedirObject::~RedirObject() {
  this->restoreFDs();
  for (int fd : this->oldFds) {
    if (fd > -1) {
      close(fd);
    }
  }
}

static int doIOHere(const StringRef &value, int newFd, bool insertNewline) {
  pipe_t pipe[1];
  initAllPipe(1, pipe);

  dup2(pipe[0][READ_PIPE], newFd);

  if (value.size() + (insertNewline ? 1 : 0) <= PIPE_BUF) {
    int errnum = 0;
    if (write(pipe[0][WRITE_PIPE], value.data(), sizeof(char) * value.size()) < 0) {
      errnum = errno;
    }
    if (insertNewline) { // for here str (insert newline)
      if (errnum == 0 && write(pipe[0][WRITE_PIPE], "\n", 1) < 0) {
        errnum = errno;
      }
    }
    closeAllPipe(1, pipe);
    return errnum;
  } else {
    pid_t pid = fork();
    if (pid < 0) {
      return errno;
    }
    if (pid == 0) {   // child
      pid = fork();   // double-fork (not wait IO-here process termination.)
      if (pid == 0) { // child
        close(pipe[0][READ_PIPE]);
        dup2(pipe[0][WRITE_PIPE], STDOUT_FILENO);
        if (write(STDOUT_FILENO, value.data(), value.size()) < 0) {
          exit(1);
        }
        if (insertNewline) { // for here str (insert newline)
          if (write(STDOUT_FILENO, "\n", 1) < 0) {
            exit(1);
          }
        }
      }
      exit(0);
    }
    closeAllPipe(1, pipe);
    waitpid(pid, nullptr, 0);
    return 0;
  }
}

enum class RedirOpenFlag : unsigned char {
  READ,
  WRITE,   // if file exists, error
  CLOBBER, // if file exists, truncate 0
  APPEND,
  READ_WRITE,
};

/**
 *
 * @param fileName
 * @param openFlag
 * @param newFd
 * @return
 * if failed, return non-zero value (errno)
 */
static int redirectToFile(const StringRef fileName, const RedirOpenFlag openFlag, const int newFd) {
  int flag = 0;
  switch (openFlag) {
  case RedirOpenFlag::READ:
    flag = O_RDONLY;
    break;
  case RedirOpenFlag::WRITE:
    flag = O_WRONLY | O_CREAT | O_EXCL;
    break;
  case RedirOpenFlag::CLOBBER:
    flag = O_WRONLY | O_CREAT | O_TRUNC;
    break;
  case RedirOpenFlag::APPEND:
    flag = O_WRONLY | O_CREAT | O_APPEND;
    break;
  case RedirOpenFlag::READ_WRITE:
    flag = O_RDWR | O_CREAT;
    break;
  }

  if (fileName.hasNullChar()) {
    return EINVAL;
  }

  int fd = open(fileName.data(), flag, 0666);
  if (fd < 0) {
    return errno;
  }
  if (dup2(fd, newFd) < 0) {
    int e = errno;
    close(fd);
    return e;
  }
  close(fd);
  return 0;
}

void RedirObject::addEntry(Value &&value, RedirOp op, int newFd) {
  if (op == RedirOp::REDIR_OUT_ERR || op == RedirOp::APPEND_OUT_ERR ||
      op == RedirOp::CLOBBER_OUT_ERR) {
    this->backupFDSet.add(STDOUT_FILENO);
    this->backupFDSet.add(STDERR_FILENO);
  } else if (newFd >= 0 && newFd <= MAX_FD_NUM) {
    this->backupFDSet.add(newFd);
  }
  assert(newFd < 0 || newFd <= MAX_FD_NUM);
  this->entries.push_back(Entry{
      .value = std::move(value),
      .op = op,
      .newFd = newFd,
  });
}

static RedirOpenFlag resolveOpenFlag(const RedirOp op, const bool overwrite) {
  switch (op) {
  case RedirOp::REDIR_IN:
    return RedirOpenFlag::READ;
  case RedirOp::REDIR_OUT:
  case RedirOp::REDIR_OUT_ERR:
    return overwrite ? RedirOpenFlag::CLOBBER : RedirOpenFlag::WRITE;
  case RedirOp::APPEND_OUT:
  case RedirOp::APPEND_OUT_ERR:
    return RedirOpenFlag::APPEND;
  case RedirOp::REDIR_IN_OUT:
    return RedirOpenFlag::READ_WRITE;
  case RedirOp::NOP:
  case RedirOp::DUP_FD:
  case RedirOp::HERE_DOC:
  case RedirOp::HERE_STR:
    break;
  case RedirOp::CLOBBER_OUT:
  case RedirOp::CLOBBER_OUT_ERR:
    return RedirOpenFlag::CLOBBER;
  }
  return RedirOpenFlag::READ;
}

static int redirectImpl(const RedirObject::Entry &entry, const bool overwrite) {
  switch (entry.op) {
  case RedirOp::NOP:
    break;
  case RedirOp::REDIR_IN:
  case RedirOp::REDIR_OUT:
  case RedirOp::CLOBBER_OUT:
  case RedirOp::APPEND_OUT:
  case RedirOp::REDIR_IN_OUT:
    return redirectToFile(entry.value.asStrRef(), resolveOpenFlag(entry.op, overwrite),
                          entry.newFd);
  case RedirOp::REDIR_OUT_ERR:
  case RedirOp::CLOBBER_OUT_ERR:
  case RedirOp::APPEND_OUT_ERR:
    if (int r = redirectToFile(entry.value.asStrRef(), resolveOpenFlag(entry.op, overwrite),
                               STDOUT_FILENO);
        r != 0) {
      return r;
    }
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
      return errno;
    }
    return 0;
  case RedirOp::DUP_FD: {
    int fd;
    if (entry.value.isObject() && isa<UnixFdObject>(entry.value.get())) {
      fd = typeAs<UnixFdObject>(entry.value).getRawFd();
    } else if (entry.value.kind() == ValueKind::INT) {
      fd = static_cast<int>(entry.value.asInt());
    } else {
      assert(entry.value.isInvalid());
      close(entry.newFd);
      return 0;
    }
    if (dup2(fd, entry.newFd) < 0) {
      return errno;
    }
    return 0;
  }
  case RedirOp::HERE_DOC:
  case RedirOp::HERE_STR:
    return doIOHere(entry.value.asStrRef(), entry.newFd, entry.op == RedirOp::HERE_STR);
  }
  return 0;
}

bool RedirObject::redirect(ARState &state) {
  this->saveFDs();
  for (auto &entry : this->entries) {
    int r = redirectImpl(entry, state.has(RuntimeOption::CLOBBER));
    if (!this->backupFDSet.empty() && r != 0) {
      std::string msg = ERROR_REDIR;
      if (entry.value) {
        if (entry.value.hasType(TYPE::String)) {
          auto ref = entry.value.asStrRef();
          if (!ref.empty()) {
            msg += ": ";
            msg += toPrintable(ref);
          }
        } else if (entry.value.hasType(TYPE::FD) || entry.value.hasType(TYPE::Int)) {
          msg += ": ";
          int rawFd = entry.value.hasType(TYPE::FD) ? typeAs<UnixFdObject>(entry.value).getRawFd()
                                                    : static_cast<int>(entry.value.asInt());
          msg += std::to_string(rawFd);
        }
      }
      raiseSystemError(state, r, std::move(msg));
      return false;
    }
  }
  return true;
}

} // namespace arsh