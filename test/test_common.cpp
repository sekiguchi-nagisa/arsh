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
#include <stdarg.h>

#include <cstdlib>
#include <cassert>

#include <constant.h>
#include <misc/util.hpp>
#include <misc/files.h>
#include <misc/fatal.h>
#include "test_common.h"

#define error_at(fmt, ...) fatal(fmt ": %s\n", ## __VA_ARGS__, strerror(errno))

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

std::pair<std::string, std::string> ProcHandle::readAll() const {
    std::pair<std::string, std::string> output;

    unsigned int validFDCount = 0;
    if(this->out() > -1) {
        validFDCount++;
    }
    if(this->err() > -1) {
        validFDCount++;
    }

    if(validFDCount == 0) {
        return output;
    }

    struct pollfd pollfds[2]{};
    pollfds[0].fd = this->out();
    pollfds[0].events = POLLIN;
    pollfds[1].fd = this->err();
    pollfds[1].events = POLLIN;

    while(true) {
        if(poll(pollfds, ydsh::arraySize(pollfds), -1) == -1) {
            if(errno != EINTR) {
                break;
            }
        }

        unsigned int breakCount = 0;
        for(unsigned int i = 0; i < ydsh::arraySize(pollfds); i++) {
            if(pollfds[i].revents & POLLIN) {
                char buf[64];
                int readSize = read(pollfds[i].fd, buf, ydsh::arraySize(buf));
                if(readSize > 0) {
                    (i == 0 ? output.first : output.second).append(buf, readSize);
                }
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
                    breakCount++;
                }
            } else if(pollfds[i].fd >= 0) {
                breakCount++;
            }
        }
        if(breakCount == validFDCount) {
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

static int tryToDup(int srcFD, int targetFD) {
    if(srcFD > -1) {
        return dup2(srcFD, targetFD);
    }
    return 0;
}

static ProcHandle spawnImpl(int (&inpipe)[2], int (&outpipe)[2], int (&errpipe)[2]) {
    pid_t pid = fork();
    if(pid > 0) {
        close(inpipe[READ_PIPE]);
        close(outpipe[WRITE_PIPE]);
        close(errpipe[WRITE_PIPE]);

        return ProcHandle(pid, inpipe[WRITE_PIPE], outpipe[READ_PIPE], errpipe[READ_PIPE]);
    } else if(pid == 0) {
        tryToDup(inpipe[READ_PIPE], STDIN_FILENO);
        tryToDup(outpipe[WRITE_PIPE], STDOUT_FILENO);
        tryToDup(errpipe[WRITE_PIPE], STDERR_FILENO);

        close(inpipe[WRITE_PIPE]);
        close(outpipe[READ_PIPE]);
        close(errpipe[READ_PIPE]);

        return ProcHandle();
    } else {
        error_at("fork failed");
    }
}

ProcHandle ProcBuilder::spawnImpl(IOConfig config) {
    // flush standard stream due to prevent mixing io buffer
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);

    pid_t inpipe[] = {dup(config.in.fd), -1};
    pid_t outpipe[] = {-1, dup(config.out.fd)};
    pid_t errpipe[] = {-1, dup(config.err.fd)};

    // create pipe
    if(config.in.fd == IOConfig::PIPE && pipe(inpipe) < 0) {
        error_at("pipe creation failed");
    }
    if(config.out.fd == IOConfig::PIPE && pipe(outpipe) < 0) {
        error_at("pipe creation failed");
    }
    if(config.err.fd == IOConfig::PIPE && pipe(errpipe) < 0) {
        error_at("pipe creation failed");
    }

    return ::spawnImpl(inpipe, outpipe, errpipe);
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
        fatal("%s\n", strerror(errno));
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
    this->consumeSpace();

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
    this->consumeSpace();

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

    this->consumeSpace();

    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(isSpace(ch)) {
           break;
        }
        value += ch;
    }
    return 0;
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