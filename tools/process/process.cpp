/*
 * Copyright (C) 2017-2018 Nagisa Sekiguchi
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
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include <constant.h>
#include <misc/fatal.h>
#include <misc/files.hpp>
#include <misc/inlined_array.hpp>
#include <misc/unicode.hpp>

#include "ansi.h"
#include "process.h"

namespace process {

static WaitStatus inspectStatus(int status) {
  int s = 0;
  auto kind = WaitStatus::RUNNING;
  if (WIFEXITED(status)) {
    s = WEXITSTATUS(status);
    kind = WaitStatus::EXITED;
  } else if (WIFSIGNALED(status)) {
    s = WTERMSIG(status);
    kind = WaitStatus::SIGNALED;
  } else if (WIFSTOPPED(status)) {
    s = WSTOPSIG(status);
    kind = WaitStatus::STOPPED;
  }
  return {.kind = kind, .value = s};
}

// ########################
// ##     ProcHandle     ##
// ########################

ProcHandle::~ProcHandle() {
  this->kill(SIGKILL);
  this->wait();
}

std::pair<unsigned short, unsigned short> ProcHandle::getWinSize() const {
  std::pair<unsigned short, unsigned short> ret{0, 0};
  if (this->hasPty()) {
    winsize ws{};
    if (ioctl(this->pty(), TIOCGWINSZ, &ws) == 0) {
      ret.first = ws.ws_row;
      ret.second = ws.ws_col;
    }
  }
  return ret;
}

static int toOption(ProcHandle::WaitOp op) {
  switch (op) {
  case ProcHandle::WaitOp::BLOCKING:
    return 0;
  case ProcHandle::WaitOp::BLOCK_UNTRACED:
    return WUNTRACED;
  case ProcHandle::WaitOp::NONBLOCKING:
    return WUNTRACED | WCONTINUED | WNOHANG;
  }
  return 0;
}

WaitStatus ProcHandle::wait(WaitOp op) {
  if (this->pid() > -1) {
    // wait for exit
    int s;
    int r = waitpid(this->pid(), &s, toOption(op));
    if (r < 0) {
      this->status_ = {.kind = WaitStatus::ERROR, .value = errno};
    } else if (r == 0) {
      this->status_ = {.kind = WaitStatus::RUNNING, .value = 0};
    } else {
      this->status_ = inspectStatus(s);
    }

    if (this->status_.isTerminated()) {
      this->closeIn();
      this->closeOut();
      this->closeErr();
      this->detach();
    }
  }
  return this->status_;
}

static bool readData(unsigned int index, int fd, const ProcHandle::ReadCallback &readCallback) {
  char buf[1024];
  unsigned int bufSize = std::size(buf);
  ssize_t readSize;
  do {
    readSize = read(fd, buf, bufSize);
    int old = errno;
    if (readSize > 0) {
      readCallback(index, buf, readSize);
    }
    errno = old;
  } while (readSize == -1 && (errno == EINTR || errno == EAGAIN));
  return readSize > 0;
}

void ProcHandle::readAll(int timeout, const ReadCallback &readCallback) const {
  struct pollfd pollfds[2]{};
  pollfds[0].fd = this->out();
  pollfds[0].events = POLLIN;
  pollfds[1].fd = this->err();
  pollfds[1].events = POLLIN;

  while (true) {
    constexpr unsigned int pollfdSize = std::size(pollfds);
    int ret = poll(pollfds, pollfdSize, timeout);
    if (ret <= 0) {
      if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
        continue;
      }
      break;
    }

    unsigned int breakCount = 0;
    for (unsigned int i = 0; i < pollfdSize; i++) {
      if (pollfds[i].revents & POLLIN) {
        if (!readData(i, pollfds[i].fd, readCallback)) {
          breakCount++;
          continue;
        }
      } else {
        breakCount++;
      }
    }
    if (breakCount == 2) {
      break;
    }
  }
}

std::pair<std::string, std::string> ProcHandle::readAll(int timeout) const {
  std::pair<std::string, std::string> output;
  if (this->out() < 0 && this->err() < 0) {
    return output;
  }

  this->readAll(timeout, [&](unsigned int index, const char *buf, unsigned int size) {
    (index == 0 ? output.first : output.second).append(buf, size);
  });
  return output;
}

static void trimLastSpace(std::string &str) {
  for (; !str.empty(); str.pop_back()) {
    switch (str.back()) {
    case ' ':
    case '\t':
    case '\n':
      continue;
    default:
      return;
    }
  }
}

Output ProcHandle::waitAndGetResult(bool removeLastSpace) {
  auto output = this->readAll();
  auto status = this->wait();

  if (removeLastSpace) {
    trimLastSpace(output.first);
    trimLastSpace(output.second);
  }

  return {.status = status, .out = std::move(output.first), .err = std::move(output.second)};
}

WaitStatus ProcHandle::waitWithTimeout(unsigned int msec) {
  WaitStatus s = this->wait(WaitOp::NONBLOCKING);
  if (s.kind != WaitStatus::RUNNING) {
    return s;
  }
  for (unsigned int i = 0; i < msec; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    s = this->wait(WaitOp::NONBLOCKING);
    if (s.kind != WaitStatus::RUNNING) {
      break;
    }
  }
  return s;
}

// #########################
// ##     ProcBuilder     ##
// #########################

ProcBuilder &ProcBuilder::addArgs(const std::vector<std::string> &values) {
  for (auto &e : values) {
    this->args.push_back(e);
  }
  return *this;
}

ProcHandle ProcBuilder::operator()() const {
  return spawn(this->config, [&] {
    arsh::InlinedArray<char *, 6> argv(this->args.size() + 1);
    for (unsigned int i = 0; i < this->args.size(); i++) {
      argv[i] = const_cast<char *>(this->args[i].c_str());
    }
    argv[this->args.size()] = nullptr;

    this->syncEnv();
    this->syncPWD();
    if (this->beforeExec) {
      this->beforeExec();
    }
    execvp(argv[0], argv.ptr());
    return -errno;
  });
}

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

static void loginPTY(int fd) {
  if (fd < 0) {
    return;
  }

  if (setsid() == -1) {
    fatal_perror("failed");
  }

  if (ioctl(fd, TIOCSCTTY, 0) == -1) {
    fatal_perror("failed");
  }
}

static void setPTYSetting(int fd, const IOConfig &config) {
  if (fd < 0) {
    return;
  }

  if (tcsetattr(fd, TCSAFLUSH, &config.term) == -1) {
    fatal_perror("failed");
  }

  winsize ws{};
  ws.ws_row = config.row;
  ws.ws_col = config.col;
  if (ioctl(fd, TIOCSWINSZ, &ws) == -1) {
    fatal_perror("failed");
  }
}

static void openPTY(const IOConfig &config, int &masterFD, int &slaveFD) {
  if (config.in.is(IOConfig::PTY) || config.out.is(IOConfig::PTY) || config.err.is(IOConfig::PTY)) {
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd == -1) {
      fatal_perror("open pty master failed");
    }
    if (grantpt(fd) != 0 || unlockpt(fd) != 0) {
      fatal_perror("failed");
    }
    masterFD = fd;
    fd = open(ptsname(masterFD), O_RDWR | O_NOCTTY);
    arsh::remapFD(fd);
    if (fd == -1) {
      fatal_perror("open pty slave failed");
    }
    /**
     * set pty setting before call fork() due to prevent potential race condition
     */
    setPTYSetting(fd, config);
    slaveFD = fd;
  }
}

class StreamBuilder {
private:
  const IOConfig config;
  int inpipe[2];
  int outpipe[2];
  int errpipe[2];

  int masterFD{-1};
  int slaveFD{-1};

public:
  // for parent process

  /**
   * called in parent process
   * @param config
   */
  explicit StreamBuilder(const IOConfig &config)
      : config(config), inpipe{arsh::dupFD(this->config.in.fd), -1},
        outpipe{-1, arsh::dupFD(this->config.out.fd)},
        errpipe{-1, arsh::dupFD(this->config.err.fd)} {
    openPTY(this->config, this->masterFD, this->slaveFD);
    this->initPipe();
  }

  void setParentStream() {
    close(this->slaveFD);
    this->closeInParent();
    if (this->masterFD > -1) {
      if (this->config.in.is(IOConfig::PTY)) {
        this->inpipe[WRITE_PIPE] = arsh::dupFD(this->masterFD);
      }
      if (this->config.out.is(IOConfig::PTY)) {
        this->outpipe[READ_PIPE] = arsh::dupFD(this->masterFD);
      }
      if (this->config.err.is(IOConfig::PTY)) {
        this->errpipe[READ_PIPE] = arsh::dupFD(this->masterFD);
      }
      close(this->masterFD);
    }

    arsh::setCloseOnExec(this->inputWriter(), true);
    arsh::setCloseOnExec(this->outputReader(), true);
    arsh::setCloseOnExec(this->errorReader(), true);
  }

  int findPTY() const {
    if (this->config.in.is(IOConfig::PTY)) {
      return this->inputWriter();
    }
    if (this->config.out.is(IOConfig::PTY)) {
      return this->outputReader();
    }
    if (this->config.err.is(IOConfig::PTY)) {
      return this->errorReader();
    }
    return -1;
  }

  int inputWriter() const { return this->inpipe[WRITE_PIPE]; }

  int outputReader() const { return this->outpipe[READ_PIPE]; }

  int errorReader() const { return this->errpipe[READ_PIPE]; }

  // for child process

  /**
   * called from child process
   */
  void setChildStream() {
    close(this->masterFD);
    this->initPTYSlave();
    this->setInChild();
  }

private:
  // for parent process

  void initPipe() {
    if (this->config.in.is(IOConfig::PIPE) && pipe(this->inpipe) < 0) {
      fatal_perror("pipe creation failed");
    }
    if (this->config.out.is(IOConfig::PIPE) && pipe(this->outpipe) < 0) {
      fatal_perror("pipe creation failed");
    }
    if (this->config.err.is(IOConfig::PIPE) && pipe(this->errpipe) < 0) {
      fatal_perror("pipe creation failed");
    }
  }

  void closeInParent() {
    close(this->inpipe[READ_PIPE]);
    close(this->outpipe[WRITE_PIPE]);
    close(this->errpipe[WRITE_PIPE]);
  }

  // for child process

  void initPTYSlave() {
    int fd = this->slaveFD;
    loginPTY(fd);

    if (this->config.in.is(IOConfig::PTY)) {
      this->inpipe[READ_PIPE] = arsh::dupFD(fd);
    }
    if (this->config.out.is(IOConfig::PTY)) {
      this->outpipe[WRITE_PIPE] = arsh::dupFD(fd);
    }
    if (this->config.err.is(IOConfig::PTY)) {
      this->errpipe[WRITE_PIPE] = arsh::dupFD(fd);
    }
    close(fd);
  }

  void setInChild() {
    dup2(this->inpipe[READ_PIPE], STDIN_FILENO);
    dup2(this->outpipe[WRITE_PIPE], STDOUT_FILENO);
    dup2(this->errpipe[WRITE_PIPE], STDERR_FILENO);

    for (unsigned int i = 0; i < 2; i++) {
      close(this->inpipe[i]);
      close(this->outpipe[i]);
      close(this->errpipe[i]);
    }
  }
};

ProcHandle ProcBuilder::spawnImpl(const IOConfig &config) {
  // flush standard stream due to prevent mixing io buffer
  fflush(stdin);
  fflush(stdout);
  fflush(stderr);

  StreamBuilder builder(config);
  pid_t pid = fork();
  if (pid > 0) {
    builder.setParentStream();
    return {pid, builder.findPTY(), builder.inputWriter(), builder.outputReader(),
            builder.errorReader()};
  } else if (pid == 0) {
    builder.setChildStream();
    return {};
  } else {
    fatal_perror("fork failed");
  }
}

void ProcBuilder::syncPWD() const {
  // change working dir
  if (!this->cwd.empty()) {
    if (chdir(this->cwd.c_str()) < 0) {
      fatal_perror("chdir failed");
    }
  }

  // update PWD
  char *dir = realpath(".", nullptr);
  if (dir == nullptr) {
    fatal_perror("current working directory is broken!!");
  }
  setenv(arsh::ENV_PWD, dir, 1);
  free(dir);
}

void ProcBuilder::syncEnv() const {
  for (auto &pair : this->env) {
    setenv(pair.first.c_str(), pair.second.c_str(), 1);
  }
}

// ####################
// ##     Screen     ##
// ####################

void Screen::addChar(int ch) {
  switch (ch) {
  case '\0':
    break;
  case '\n':
    this->row++;
    this->updateMaxUsedRows();
    break;
  case '\r':
    this->col = 0;
    break;
  case '\b':
    this->setChar('\0');
    this->col--;
    break;
  case '\t':
    this->setChar('\t');
    break;
  case 127:
    this->setChar('\0');
    break;
  default:
    if (ch >= 0 && ch <= 31) {
      break;
    }
    this->setChar(ch);
    this->col++;
    break;
  }
}

void Screen::addCodePoint(const char *begin, const char *end) {
  int code = arsh::UnicodeUtil::utf8ToCodePoint(begin, end);
  if (isascii(code)) {
    this->addChar(code);
  } else {
    int width = arsh::UnicodeUtil::width(code, this->eaw);
    switch (width) {
    case 1:
      this->setChar(code);
      this->col++;
      break;
    case 2:
      this->setChar(code);
      this->col++;
      this->setChar(-1); // dummy
      this->col++;
      break;
    default:
      break;
    }
  }
}

void Screen::reportPos() {
  if (this->reporter) {
    auto pos = this->getCursor();
    std::string str = "\x1b[";
    str += std::to_string(pos.row);
    str += ';';
    str += std::to_string(pos.col);
    str += 'R';

    this->reporter(std::move(str));
  }
}

void Screen::clear() {
  for (auto &buf : this->bufs) {
    for (auto &ch : buf) {
      ch = '\0';
    }
  }
}

void Screen::clearLineFrom() {
  auto &buf = this->bufs[this->row];
  for (unsigned int i = this->col; i < buf.size(); i++) {
    buf[i] = '\0';
  }
}

void Screen::clearLine() {
  for (auto &ch : this->bufs[this->row]) {
    ch = '\0';
  }
}

static std::string toStringAtLine(const arsh::FlexBuffer<int> &buf) {
  std::string ret;
  for (int ch : buf) {
    if (ch == -1) {
      continue;
    }
    char data[8] = {};
    unsigned int r = arsh::UnicodeUtil::codePointToUtf8(ch, data);
    ret.append(data, r);
  }
  for (; !ret.empty() && ret.back() == '\0'; ret.pop_back())
    ;
  std::replace(ret.begin(), ret.end(), '\0', ' ');
  return ret;
}

std::string Screen::toString() const {
  std::string ret;
  for (unsigned int i = 0; i < this->maxUsedRows; i++) {
    auto line = toStringAtLine(this->bufs[i]);
    if (i > 0) {
      ret += '\n';
    }
    ret += line;
  }
  return ret;
}

} // namespace process