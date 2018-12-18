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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <cstdlib>
#include <cassert>
#include <climits>
#include <csignal>
#include <cctype>

#include <constant.h>
#include <misc/util.hpp>
#include <misc/fatal.h>
#include <misc/logger_base.hpp>

#include "process.h"

#ifdef __CYGWIN__
static cc_t ttydefchars[NCCS] = {
            _POSIX_VDISABLE, CDISCARD, CEOL, CEOL2, CEOF, CERASE,
            CINTR, CKILL, CLNEXT, CMIN, CQUIT, CREPRINT, CSTART,
            CSTOP, CSUSP, CSWTCH, CTIME, CWERASE
    };
#else
#define TTYDEFCHARS
#include <sys/ttydefaults.h>
#endif


#define error_at fatal_perror

namespace process {

static WaitStatus inspectStatus(int status) {
    int s;
    WaitStatus::Kind type;
    if(WIFEXITED(status)) {
        s = WEXITSTATUS(status);
        type = WaitStatus::EXITED;
    } else if(WIFSIGNALED(status)) {
        s = WTERMSIG(status);
        type = WaitStatus::SIGNALED;
    } else if(WIFSTOPPED(status)) {
        s = WSTOPSIG(status);
        type = WaitStatus::STOPPED;
    } else {
        fatal("unsupported status\n");
    }
    return {.kind = type, .value = s};
}

// ########################
// ##     ProcHandle     ##
// ########################

ProcHandle::~ProcHandle() {
    if(*this) {
        kill(this->pid(), SIGKILL);
    }
    this->wait();
}

WaitStatus ProcHandle::wait() {
    if(this->pid() > -1) {
        // wait for exit
        int s;
        if(waitpid(this->pid(), &s, 0) < 0) {
            this->status_ = {.kind = WaitStatus::ERROR, .value = errno};
        } else {
            this->status_ = inspectStatus(s);
        }

        if(this->status_.isTerminated()) {
            close(this->in());
            close(this->out());
            close(this->err());

            this->detach();
        }
    }
    return this->status_;
}

struct ProcLogger : public ydsh::SingletonLogger<ProcLogger> {
    ProcLogger() : ydsh::SingletonLogger<ProcLogger>("PROC_LOG") {}
};

static bool recvData(int fd, std::string &str) {
    char buf[64];
    unsigned int bufSize = ydsh::arraySize(buf);
    int readSize = read(fd, buf, bufSize);
    if(ProcLogger::Enabled(ydsh::LogLevel::INFO)) {
        int old = errno;
        ProcLogger::Info("recv size: %d, errno=%s", readSize, readSize < 0 ? strerror(errno) : "");
        errno = old;
    }
    if(readSize <= 0) {
        return readSize == -1 && (errno == EAGAIN || errno == EINTR);
    }
    str.append(buf, readSize);
    return true;
}

std::pair<std::string, std::string> ProcHandle::readAll(int timeout) const {
    std::pair<std::string, std::string> output;
    if(this->out() < 0 && this->err() < 0) {
        return output;
    }

    struct pollfd pollfds[2]{};
    pollfds[0].fd = this->out();
    pollfds[0].events = POLLIN;
    pollfds[1].fd = this->err();
    pollfds[1].events = POLLIN;

    while(true) {
        constexpr unsigned int pollfdSize = ydsh::arraySize(pollfds);
        int ret = poll(pollfds, pollfdSize, timeout);

        if(ProcLogger::Enabled(ydsh::LogLevel::INFO)) {
            int old = errno;
            ProcLogger::Info("poll: %d, out: %04x, err: %04x, errno=%s",
                    ret, pollfds[0].revents, pollfds[1].revents, ret < 0 ? strerror(errno) : "");
            errno = old;
        }

        if(ret <= 0) {
            if(ret == -1 && (errno == EINTR || errno == EAGAIN)) {
                ProcLogger::Info("retry poll by errno=%s", strerror(errno));
                continue;
            }
            break;
        }

        unsigned int breakCount = 0;
        for(unsigned int i = 0; i < pollfdSize; i++) {
            if(pollfds[i].revents & POLLIN) {
                if(!recvData(pollfds[i].fd, (i == 0 ? output.first : output.second))) {
                    breakCount++;
                    continue;
                }
            } else {
                breakCount++;
            }
        }
        if(breakCount == 2) {
            break;
        }
    }
    return output;
}

static void trimLastSpace(std::string &str) {
    for(; !str.empty() && isspace(str.back()); str.pop_back());
}

Output ProcHandle::waitAndGetResult(bool removeLastSpace) {
    auto output = this->readAll();
    auto status = this->wait();

    if(removeLastSpace) {
        trimLastSpace(output.first);
        trimLastSpace(output.second);
    }

    return {.status = status,
            .out = std::move(output.first),
            .err = std::move(output.second)};
}

// #########################
// ##     ProcBuilder     ##
// #########################

static void setPTYSetting(int fd, const termios &term, unsigned short row, unsigned short col) {
    if(fd < 0) {
        return;
    }

    if(tcsetattr(fd, TCSAFLUSH, &term) == -1) {
        error_at("failed");
    }

    winsize ws{};
    ws.ws_row = row;
    ws.ws_col = col;
    if(ioctl(fd, TIOCSWINSZ, &ws) == -1) {
        error_at("failed");
    }
}

ProcBuilder& ProcBuilder::addArgs(const std::vector<std::string> &values) {
    for(auto &e : values) {
        this->args.push_back(e);
    }
    return *this;
}

ProcHandle ProcBuilder::operator()() const {
    return spawn(this->config, [&] {
        char *argv[this->args.size() + 1];
        for(unsigned int i = 0; i < this->args.size(); i++) {
            argv[i] = const_cast<char *>(this->args[i].c_str());
        }
        argv[this->args.size()] = nullptr;

        this->syncEnv();
        this->syncPWD();
        setPTYSetting(this->findPTY(), this->term, this->row, this->col);
        if(this->beforeExec) {
            this->beforeExec();
        }
        execvp(argv[0], argv);
        return -errno;
    });
}

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

static void loginPTY(int fd) {
    if(fd < 0) {
        return;
    }

    if(setsid() == -1) {
        error_at("failed");
    }

    if(ioctl(fd, TIOCSCTTY, 0) == -1) {
        error_at("failed");
    }
}

void xcfmakesane(termios &term) {
    term.c_iflag = TTYDEF_IFLAG;
    term.c_oflag = TTYDEF_OFLAG;
    term.c_lflag = TTYDEF_LFLAG;
    term.c_cflag = TTYDEF_CFLAG;
    cfsetispeed(&term, TTYDEF_SPEED);
    cfsetospeed(&term, TTYDEF_SPEED);

    memcpy(term.c_cc, ttydefchars, sizeof(ttydefchars));
}

static void openPTY(IOConfig config, int &masterFD, int &slaveFD) {
    if(config.in.is(IOConfig::PTY) ||
       config.out.is(IOConfig::PTY) ||
       config.err.is(IOConfig::PTY)) {
        int fd = posix_openpt(O_RDWR | O_NOCTTY);
        if(fd == -1) {
            error_at("open pty master failed");
        }
        if(grantpt(fd) != 0 || unlockpt(fd) != 0) {
            error_at("failed");
        }
        masterFD = fd;
        fd = open(ptsname(masterFD), O_RDWR | O_NOCTTY);
        if(fd == -1) {
            error_at("open pty slave failed");
        }
        termios term{};
        cfmakeraw(&term);
        setPTYSetting(fd, term, 24, 80);
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
    explicit StreamBuilder(IOConfig config) : config(config),
                                              inpipe{dup(this->config.in.fd), -1},
                                              outpipe{-1, dup(this->config.out.fd)},
                                              errpipe{-1, dup(this->config.err.fd)} {
    }

    void initPipe() {
        if(this->config.in.is(IOConfig::PIPE) && pipe(this->inpipe) < 0) {
            error_at("pipe creation failed");
        }
        if(this->config.out.is(IOConfig::PIPE) && pipe(this->outpipe) < 0) {
            error_at("pipe creation failed");
        }
        if(this->config.err.is(IOConfig::PIPE) && pipe(this->errpipe) < 0) {
            error_at("pipe creation failed");
        }
    }

    void init() {
        openPTY(this->config, this->masterFD, this->slaveFD);
        this->initPipe();
    }

    void setInParent() {
        close(this->slaveFD);
        this->closeInParent();
        if(this->masterFD > -1) {
            if(this->config.in.is(IOConfig::PTY)) {
                this->inpipe[WRITE_PIPE] = dup(this->masterFD);
            }
            if(this->config.out.is(IOConfig::PTY)) {
                this->outpipe[READ_PIPE] = dup(this->masterFD);
            }
            if(this->config.err.is(IOConfig::PTY)) {
                this->errpipe[READ_PIPE] = dup(this->masterFD);
            }
            close(this->masterFD);
        }
    }

    void initPTYSlave() {
        int fd = this->slaveFD;
        loginPTY(fd);

        if(this->config.in.is(IOConfig::PTY)) {
            this->inpipe[READ_PIPE] = dup(fd);
        }
        if(this->config.out.is(IOConfig::PTY)) {
            this->outpipe[WRITE_PIPE] = dup(fd);
        }
        if(this->config.err.is(IOConfig::PTY)) {
            this->errpipe[WRITE_PIPE] = dup(fd);
        }
        close(fd);
    }

    void closeInParent() {
        close(this->inpipe[READ_PIPE]);
        close(this->outpipe[WRITE_PIPE]);
        close(this->errpipe[WRITE_PIPE]);
    }

    void setInChild() {
        dup2(this->inpipe[READ_PIPE], STDIN_FILENO);
        dup2(this->outpipe[WRITE_PIPE], STDOUT_FILENO);
        dup2(this->errpipe[WRITE_PIPE], STDERR_FILENO);

        for(unsigned int i = 0; i < 2; i++) {
            close(this->inpipe[i]);
            close(this->outpipe[i]);
            close(this->errpipe[i]);
        }
    }

    void setChildStream() {
        close(this->masterFD);
        this->initPTYSlave();
        this->setInChild();
    }

    int inputWriter() const {
        return this->inpipe[WRITE_PIPE];
    }

    int outputReader() const {
        return this->outpipe[READ_PIPE];
    }

    int errorReader() const {
        return this->errpipe[READ_PIPE];
    }
};

ProcHandle ProcBuilder::spawnImpl(IOConfig config) {
    // flush standard stream due to prevent mixing io buffer
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);

    StreamBuilder builder(config);
    builder.init();
    pid_t pid = fork();
    if(pid > 0) {
        builder.setInParent();
        return ProcHandle(pid, builder.inputWriter(), builder.outputReader(), builder.errorReader());
    } else if(pid == 0) {
        builder.setChildStream();
        return ProcHandle();
    } else {
        error_at("fork failed");
    }
}

void ProcBuilder::syncPWD() const {
    // change working dir
    if(!this->cwd.empty()) {
        if(chdir(this->cwd.c_str()) < 0) {
            error_at("chdir failed");
        }
    }

    // update PWD
    char *dir = realpath(".", nullptr);
    if(dir == nullptr) {
        error_at("current working directory is broken!!");
    }
    setenv(ydsh::ENV_PWD, dir, 1);
    free(dir);
}

void ProcBuilder::syncEnv() const {
    for(auto &pair : this->env) {
        setenv(pair.first.c_str(), pair.second.c_str(), 1);
    }
}

int ProcBuilder::findPTY() const {
    if(this->config.in.is(IOConfig::PTY)) {
        return STDIN_FILENO;
    }
    if(this->config.out.is(IOConfig::PTY)) {
        return STDOUT_FILENO;
    }
    if(this->config.err.is(IOConfig::PTY)) {
        return STDERR_FILENO;
    }
    return -1;
}


} // namespace process