/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#include <symbol.h>
#include <misc/util.hpp>
#include <misc/files.h>
#include "test_common.h"

#define error_at(fmt, ...) fatal(fmt ": %s\n", ## __VA_ARGS__, strerror(errno))

// #############################
// ##     TempFileFactory     ##
// #############################

TempFileFactory::~TempFileFactory() {
    this->freeName();
}

static char *makeTempDir() {
    const char *tmpdir = getenv("TMPDIR");
    if(tmpdir == nullptr) {
        tmpdir = "/tmp";
    }
    char *name = nullptr;
    if(asprintf(&name, "%s/test_tmp_dirXXXXXX", tmpdir) < 0) {
        error_at("");
    }
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

void TempFileFactory::deleteTemp() {
    DIR *dir = opendir(this->tmpDirName);
    if(dir == nullptr) {
        error_at("");
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string fullpath = this->tmpDirName;
        fullpath += '/';
        fullpath += entry->d_name;
        const char *name = fullpath.c_str();
        if(S_ISREG(ydsh::getStMode(name))) {
            if(remove(name) < 0) {
                error_at("%s", name);
            }
        } else {
            fatal("not a regular file: %s\n", name);    //FIXME: symbolic link, etc...
        }
    }
    closedir(dir);

    if(rmdir(this->tmpDirName) < 0) {
        error_at("");
    }
    this->freeName();
}

void TempFileFactory::freeName() {
    free(this->tmpFileName);
    this->tmpFileName = nullptr;
    free(this->tmpDirName);
    this->tmpDirName = nullptr;
}


// ########################
// ##     ProcHandle     ##
// ########################

int ProcHandle::wait() {
    if(this->pid() > -1) {
        close(this->out());
        close(this->err());

        // wait for exit
        int status = 0;
        waitpid(this->pid(), &status, 0);

        this->pid_ = -1;
        this->out_ = -1;
        this->err_ = -1;
        return status;
    }
    return 0;
}

Output ProcHandle::readAll() {
    Output output;

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
                    (i == 0 ? output.out : output.err).append(buf, readSize);
                }
                if(readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
                    continue;
                }
                if(readSize <= 0) {
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

static std::string toString(const ydsh::ByteBuffer &buf, bool removeLastSpace) {
    std::string out(buf.get(), buf.size());

    if(removeLastSpace) {
        for(; !out.empty() && isSpace(out.back()); out.pop_back());
    }
    return out;
}

ProcResult ProcHandle::waitAndGetResult(bool removeLastSpace) {
    auto output = this->readAll();
    int status = this->wait();

    if(WIFEXITED(status)) {
        status = WEXITSTATUS(status);
    } else if(WIFSIGNALED(status)) {
        status = WTERMSIG(status) + 128;
    } else {
        fatal("invalid exit status\n");
    }

    return {.status = status,
            .out = toString(output.out, removeLastSpace),
            .err = toString(output.err, removeLastSpace)};
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

ProcHandle ProcBuilder::operator()(bool usePipe) const {
    return spawn(usePipe, [&] {
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

int ProcBuilder::exec(Output *output) const {
    auto proc = (*this)(output != nullptr);
    if(output != nullptr) {
        *output = proc.readAll();
    }
    return proc.wait();
}

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

ProcHandle ProcBuilder::spawnImpl(bool usePipe) {
    // flush standard stream due to prevent mixing io buffer
    fflush(stdout);
    fflush(stderr);
    fflush(stdin);

    // create pipe
    pid_t outpipe[2];
    pid_t errpipe[2];

    if(pipe(outpipe) < 0) {
        error_at("pipe creation failed");
    }

    if(pipe(errpipe) < 0) {
        error_at("pipe creation failed");
    }

    pid_t pid = ::fork();
    if(pid > 0) {
        close(outpipe[WRITE_PIPE]);
        close(errpipe[WRITE_PIPE]);
        if(!usePipe) {
            close(outpipe[READ_PIPE]);
            close(errpipe[READ_PIPE]);
        }

        return ProcHandle(pid, usePipe ? outpipe[READ_PIPE] : -1, usePipe ? errpipe[READ_PIPE] : -1);
    } else if(pid == 0) {
        if(usePipe) {
            dup2(outpipe[WRITE_PIPE], STDOUT_FILENO);
            dup2(errpipe[WRITE_PIPE], STDERR_FILENO);
        }
        close(outpipe[READ_PIPE]);
        close(outpipe[WRITE_PIPE]);
        close(errpipe[READ_PIPE]);
        close(errpipe[WRITE_PIPE]);

        return ProcHandle();
    } else {
        error_at("fork failed");
    }
}

void ProcBuilder::syncPWD() const {
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