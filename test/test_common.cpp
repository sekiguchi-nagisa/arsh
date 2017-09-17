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

#include <cstdlib>

#include <symbol.h>
#include <misc/util.hpp>
#include "test_common.h"

// #############################
// ##     TempFileFactory     ##
// #############################

void TempFileFactory::createTemp() {
    const char *tmpdir = getenv("TMPDIR");
    if(tmpdir == nullptr) {
        tmpdir = "/tmp";
    }
    constexpr unsigned int size = 512;
    char name[size];
    snprintf(name, size, "%s/exec_test_tmpXXXXXX", tmpdir);

    int fd = mkstemp(name);
    close(fd);
    this->tmpFileName = name;
}

void TempFileFactory::deleteTemp() {
    remove(this->tmpFileName.c_str());
}


// ############################
// ##     CommandBuilder     ##
// ############################

CommandBuilder& CommandBuilder::addArgs(const std::vector<std::string> &values) {
    for(auto &e : values) {
        this->args.push_back(e);
    }
    return *this;
}

static std::string toString(const ydsh::ByteBuffer &buf, bool removeLastSpace) {
    std::string out(buf.get(), buf.size());

    if(removeLastSpace) {
        for(; !out.empty() && isSpace(out.back()); out.pop_back());
    }
    return out;
}

CmdResult CommandBuilder::execAndGetResult(bool removeLastSpace) const {
    Output output;
    int status = this->exec(output);
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

static constexpr unsigned int READ_PIPE = 0;
static constexpr unsigned int WRITE_PIPE = 1;

static void readPipes(Output &output, const pid_t (&outpipe)[2], const pid_t (&errpipe)[2]) {
    struct pollfd pollfds[2]{};
    pollfds[0].fd = outpipe[READ_PIPE];
    pollfds[0].events = POLLIN;
    pollfds[1].fd = errpipe[READ_PIPE];
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
}

int CommandBuilder::exec(Output *output) const {
    // flush standard stream due to prevent mixing io buffer
    fflush(stdout);
    fflush(stderr);
    fflush(stdin);

    // create pipe
    pid_t outpipe[2];
    pid_t errpipe[2];

    if(pipe(outpipe) < 0) {
        int e = errno;
        perror("pipe creation failed\n");
        return -e;
    }

    if(pipe(errpipe) < 0) {
        int e = errno;
        perror("pipe creation failed\n");
        close(outpipe[0]);
        close(outpipe[1]);
        return -e;
    }

    // fork-and-exec
    pid_t pid = fork();
    if(pid > 0) {   // parent
        close(outpipe[WRITE_PIPE]);
        close(errpipe[WRITE_PIPE]);

        if(output != nullptr) {
            readPipes(*output, outpipe, errpipe);
        }

        close(outpipe[READ_PIPE]);
        close(errpipe[READ_PIPE]);

        // wait for exit
        int status = 0;
        waitpid(pid, &status, 0);
        return status;
    } else if(pid == 0) {   // child
        if(output != nullptr) {
            dup2(outpipe[WRITE_PIPE], STDOUT_FILENO);
            dup2(errpipe[WRITE_PIPE], STDERR_FILENO);
        }
        close(outpipe[READ_PIPE]);
        close(outpipe[WRITE_PIPE]);
        close(errpipe[READ_PIPE]);
        close(errpipe[WRITE_PIPE]);

        char *argv[this->args.size() + 1];
        for(unsigned int i = 0; i < this->args.size(); i++) {
            argv[i] = const_cast<char *>(this->args[i].c_str());
        }
        argv[this->args.size()] = nullptr;

        this->syncPWD();
        execvp(argv[0], argv);
        exit(-errno);
    } else {
        int e = errno;
        perror("fork failed\n");
        return -e;
    }
}

void CommandBuilder::syncPWD() const {
    size_t size = PATH_MAX;
    char buf[size];
    const char *cwd = getcwd(buf, size);
    if(cwd == nullptr) {
        fatal("current working directory is broken!!\n");
    }
    setenv(ydsh::ENV_PWD, cwd, 1);
}

bool operator==(const ydsh::ByteBuffer &x, const ydsh::ByteBuffer &y) {
    if(x.size() != y.size()) {
        return false;
    }
    unsigned int size = x.size();
    for(unsigned int i = 0; i < size; i++) {
        if(x[i] != y[i]) {
            return false;
        }
    }
    return true;
}

std::ostream &operator<<(std::ostream &stream, const ydsh::ByteBuffer &buffer) {
    for(auto &b : buffer) {
        if(b >= 32 && b <= 126) {
            stream << (char) b;
        } else {
            stream << std::showbase << std::hex << b;
        }
    }
    return stream;
}