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

namespace ydsh {

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

static bool isPassingFD(const RedirObject::Entry &entry) {
  return entry.op == RedirOp::NOP && entry.value.isObject() && isa<UnixFdObject>(entry.value.get());
}

RedirObject::~RedirObject() {
  this->restoreFDs();
  for (int fd : this->oldFds) {
    close(fd);
  }

  // set close-on-exec flag to fds
  for (auto &e : this->entries) {
    if (isPassingFD(e) && e.value.get()->getRefcount() > 1) {
      typeAs<UnixFdObject>(e.value).closeOnExec(true);
    }
  }
}

static int doIOHere(const StringRef &value, int newFd) {
  pipe_t pipe[1];
  initAllPipe(1, pipe);

  dup2(pipe[0][READ_PIPE], newFd);

  if (value.size() + 1 <= PIPE_BUF) {
    int errnum = 0;
    if (write(pipe[0][WRITE_PIPE], value.data(), sizeof(char) * value.size()) < 0) {
      errnum = errno;
    }
    if (errnum == 0 && write(pipe[0][WRITE_PIPE], "\n", 1) < 0) {
      errnum = errno;
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
        if (write(STDOUT_FILENO, "\n", 1) < 0) {
          exit(1);
        }
      }
      exit(0);
    }
    closeAllPipe(1, pipe);
    waitpid(pid, nullptr, 0);
    return 0;
  }
}

enum class RedirOpenFlag {
  READ,
  WRITE,
  APPEND,
};

/**
 *
 * @param fileName must be String
 * @param openFlag
 * @param newFD
 * @return
 * if failed, return non-zero value (errno)
 */
static int redirectToFile(const DSValue &fileName, RedirOpenFlag openFlag, int newFd) {
  assert(fileName.hasStrRef());
  int flag = 0;
  switch (openFlag) {
  case RedirOpenFlag::READ:
    flag = O_RDONLY;
    break;
  case RedirOpenFlag::WRITE:
    flag = O_WRONLY | O_CREAT | O_TRUNC;
    break;
  case RedirOpenFlag::APPEND:
    flag = O_WRONLY | O_APPEND | O_CREAT;
    break;
  }

  auto ref = fileName.asStrRef();
  if (ref.hasNullChar()) {
    return EINVAL;
  }

  int fd = open(ref.data(), flag, 0666);
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

void RedirObject::addEntry(DSValue &&value, RedirOp op, int newFd) {
  if (op == RedirOp::REDIR_OUT_ERR || op == RedirOp::APPEND_OUT_ERR) {
    this->backupFDSet |= 1u << 1u;
    this->backupFDSet |= 1u << 2u;
  } else if (newFd >= 0 && newFd <= 2) {
    this->backupFDSet |= 1u << static_cast<unsigned int>(newFd);
  }
  this->entries.push_back(Entry{
      .value = std::move(value),
      .op = op,
      .newFd = newFd,
  });
}

static RedirOpenFlag resolveOpenFlag(RedirOp op) {
  switch (op) {
  case RedirOp::REDIR_IN:
    return RedirOpenFlag::READ;
  case RedirOp::REDIR_OUT:
    return RedirOpenFlag::WRITE;
  case RedirOp::APPEND_OUT:
    return RedirOpenFlag::APPEND;
  case RedirOp::REDIR_OUT_ERR:
    return RedirOpenFlag::WRITE;
  case RedirOp::APPEND_OUT_ERR:
    return RedirOpenFlag::APPEND;
  case RedirOp::NOP:
  case RedirOp::DUP_FD:
  case RedirOp::HERE_STR:
    break;
  }
  return RedirOpenFlag::READ;
}

static int redirectImpl(const RedirObject::Entry &entry) {
  switch (entry.op) {
  case RedirOp::NOP:
    break;
  case RedirOp::REDIR_IN:
  case RedirOp::REDIR_OUT:
  case RedirOp::APPEND_OUT:
    return redirectToFile(entry.value, resolveOpenFlag(entry.op), entry.newFd);
  case RedirOp::REDIR_OUT_ERR:
  case RedirOp::APPEND_OUT_ERR:
    if (int r = redirectToFile(entry.value, resolveOpenFlag(entry.op), STDOUT_FILENO); r != 0) {
      return r;
    }
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
      return errno;
    }
    return 0;
  case RedirOp::DUP_FD: {
    auto &src = typeAs<UnixFdObject>(entry.value);
    if (dup2(src.getValue(), entry.newFd) < 0) {
      return errno;
    }
    return 0;
  }
  case RedirOp::HERE_STR:
    return doIOHere(entry.value.asStrRef(), entry.newFd);
  }
  return 0;
}

bool RedirObject::redirect(DSState &state) {
  this->saveFDs();
  for (auto &entry : this->entries) {
    int r = redirectImpl(entry);
    if (this->backupFDSet > 0 && r != 0) {
      std::string msg = REDIR_ERROR;
      if (entry.value) {
        if (entry.value.hasType(TYPE::String)) {
          auto ref = entry.value.asStrRef();
          if (!ref.empty()) {
            msg += ": ";
            msg += toPrintable(ref);
          }
        } else if (entry.value.hasType(TYPE::UnixFD)) { // FIXME:
          msg += ": ";
          msg += std::to_string(typeAs<UnixFdObject>(entry.value).getValue());
        }
      }
      raiseSystemError(state, r, std::move(msg));
      return false;
    }
  }
  return true;
}

} // namespace ydsh