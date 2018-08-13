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
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <fcntl.h>

#include <cstdlib>
#include <cassert>

#include <constant.h>
#include <misc/util.hpp>
#include <misc/files.h>
#include <misc/fatal.h>

#define TTYDEFCHARS
#include "test_common.h"

#undef TTYDEFCHARS

#define error_at fatal_perror

// #############################
// ##     TempFileFactory     ##
// #############################

TempFileFactory::~TempFileFactory() {
    this->freeName();
}

static char *getTempRoot() {
    const char *tmpdir = getenv("TMPDIR");
    if(tmpdir == nullptr) {
        tmpdir = "/tmp";
    }
    return realpath(tmpdir, nullptr);
}

static char *makeTempDir() {
    char *tmpdir = getTempRoot();
    char *name = nullptr;
    if(asprintf(&name, "%s/test_tmp_dirXXXXXX", tmpdir) < 0) {
        error_at("");
    }
    free(tmpdir);
    char *dirName = mkdtemp(name);
    assert(dirName != nullptr);
    assert(dirName == name);
    return dirName;
}

void TempFileFactory::createTemp() {
    this->tmpDirName = makeTempDir();

    char *name;
    if(asprintf(&name, "%s/test_tmpXXXXXX", this->tmpDirName) < 0) {
        error_at("");
    }
    int fd = mkstemp(name);
    if(fd < 0) {
        error_at("");
    }
    close(fd);
    this->tmpFileName = name;
}

static void removeRecursive(const char *currentDir) {
    DIR *dir = opendir(currentDir);
    if(dir == nullptr) {
        error_at("cannot open dir: %s", currentDir);
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string fullpath = currentDir;
        fullpath += '/';
        fullpath += entry->d_name;
        const char *name = fullpath.c_str();
        if(S_ISDIR(ydsh::getStMode(name))) {
            removeRecursive(name);
        } else if(remove(name) < 0) {
            error_at("cannot remove: %s", name);
        }
    }
    closedir(dir);

    if(remove(currentDir) < 0) {
        error_at("cannot remove: %s", currentDir);
    }
}

void TempFileFactory::deleteTemp() {
    removeRecursive(this->tmpDirName);
    this->freeName();
}

void TempFileFactory::freeName() {
    free(this->tmpFileName);
    this->tmpFileName = nullptr;
    free(this->tmpDirName);
    this->tmpDirName = nullptr;
}

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

static bool recvData(int fd, std::string &str) {
    char buf[64];
    unsigned int bufSize = ydsh::arraySize(buf);
    int readSize = read(fd, buf, bufSize);
    if(readSize <= 0) {
        if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
            return true;
        }
        return false;
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
        if(ret <= 0) {
            if(ret == -1 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            }
            break;
        }

        unsigned int breakCount = 0;
        for(unsigned int i = 0; i < pollfdSize; i++) {
            if(pollfds[i].revents) {
                if(!recvData(pollfds[i].fd, (i == 0 ? output.first : output.second))) {
                    breakCount++;
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

static bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void trimLastSpace(std::string &str) {
    for(; !str.empty() && isSpace(str.back()); str.pop_back());
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

ProcBuilder& ProcBuilder::addArgs(const std::vector<std::string> &values) {
    for(auto &e : values) {
        this->args.push_back(e);
    }
    return *this;
}

ProcHandle ProcBuilder::operator()() {
    return spawn(this->config, [&] {
        char *argv[this->args.size() + 1];
        for(unsigned int i = 0; i < this->args.size(); i++) {
            argv[i] = const_cast<char *>(this->args[i].c_str());
        }
        argv[this->args.size()] = nullptr;

        this->syncEnv();
        this->syncPWD();
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
        termios term;
        cfmakeraw(&term);
        if(tcsetattr(fd, TCSAFLUSH, &term) == -1) {
            error_at("failed");
        }
        winsize ws;
        ws.ws_row = 24;
        ws.ws_col = 80;
        if(ioctl(fd, TIOCSWINSZ, &ws) == -1) {
            error_at("failed");
        }
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
    StreamBuilder(IOConfig config) : config(config),
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
    size_t size = PATH_MAX;
    char buf[size];
    const char *cwd = getcwd(buf, size);
    if(cwd == nullptr) {
        error_at("current working directory is broken!!");
    }
    setenv(ydsh::ENV_PWD, cwd, 1);
}

void ProcBuilder::syncEnv() const {
    for(auto &pair : this->env) {
        setenv(pair.first.c_str(), pair.second.c_str(), 1);
    }
}

std::string format(const char *fmt, ...) {
    va_list arg;

    va_start(arg, fmt);
    char *str = nullptr;
    if(vasprintf(&str, fmt, arg) == -1) {
        fatal_perror("");
    }
    va_end(arg);

    std::string v = str;
    free(str);
    return v;
}

// #######################
// ##     Extractor     ##
// #######################

void Extractor::consumeSpace() {
    for(; *this->str != '\0'; this->str++) {
        if(!isSpace(*this->str)) {
            return;
        }
    }
}

int Extractor::extract(unsigned int &value) {
    std::string buf;
    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(!isdigit(ch)) {
            break;
        }
        buf += ch;
    }
    long v = std::stol(buf);
    if(v < 0 || v > UINT32_MAX) {
        return 1;
    }
    value = static_cast<unsigned int>(v);
    return 0;
}

int Extractor::extract(int &value) {
    std::string buf;
    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(!isdigit(ch)) {
            break;
        }
        buf += ch;
    }
    long v = std::stol(buf);
    if(v < INT32_MIN || v > INT32_MAX) {
        return 1;
    }
    value = static_cast<int>(v);
    return 0;
}

int Extractor::extract(std::string &value) {
    value.clear();

    if(*this->str != '"') {
        return 1;
    }
    this->str++;

    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(ch == '"') {
            this->str++;
            return 0;
        }
        if(ch == '\\') {
            char next = *(this->str + 1);
            if(next == '\\' || next == '"') {
                ch = next;
                this->str++;
            }
        }
        value += ch;
    }
    return 1;
}

int Extractor::extract(const char *value) {
    this->consumeSpace();

    auto size = strlen(value);
    if(strncmp(this->str, value, size) != 0) {
        return 1;
    }
    this->str += size;
    return 0;
}