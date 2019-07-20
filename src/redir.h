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

#include <unistd.h>
#include <fcntl.h>

#include <cstdio>

#include "object.h"
#include "job.h"
#include "constant.h"

namespace ydsh {

constexpr unsigned int FD_BIT_0 = 1u << 0u;
constexpr unsigned int FD_BIT_1 = 1u << 1u;
constexpr unsigned int FD_BIT_2 = 1u << 2u;

#define EACH_RedirOP(OP) \
    OP(IN_2_FILE                    , FD_BIT_0) \
    OP(OUT_2_FILE                   , FD_BIT_1) \
    OP(OUT_2_FILE_APPEND            , FD_BIT_1) \
    OP(ERR_2_FILE                   , FD_BIT_2) \
    OP(ERR_2_FILE_APPEND            , FD_BIT_2) \
    OP(MERGE_ERR_2_OUT_2_FILE       , (FD_BIT_2 | FD_BIT_1)) \
    OP(MERGE_ERR_2_OUT_2_FILE_APPEND, (FD_BIT_2 | FD_BIT_1)) \
    OP(MERGE_ERR_2_OUT              , FD_BIT_2) \
    OP(MERGE_OUT_2_ERR              , FD_BIT_1) \
    OP(HERE_STR                     , FD_BIT_0)

enum class RedirOP : unsigned char {
#define GEN_ENUM(ENUM, BITS) ENUM,
    EACH_RedirOP(GEN_ENUM)
#undef GEN_ENUM
    NOP,
};

inline unsigned int getChangedFD(RedirOP op) {
    switch(op) {
#define GEN_CASE(ENUM, BITS) case RedirOP::ENUM: return BITS;
    EACH_RedirOP(GEN_CASE)
#undef GEN_CASE
    case RedirOP::NOP:
        break;
    }
    return 0;
}

inline void tryToDup(int srcFd, int targetFd) {
    if(srcFd > -1) {
        dup2(srcFd, targetFd);
    }
}

inline void tryToClose(int fd) {
    if(fd > -1) {
        close(fd);
    }
}

using pipe_t = int[2];

inline void tryToClose(pipe_t &pipefds) {
    tryToClose(pipefds[0]);
    tryToClose(pipefds[1]);
}

inline void tryToPipe(pipe_t &pipefds, bool openPipe) {
    if(openPipe) {
        if(pipe(pipefds) < 0) {
            perror("pipe creation failed\n");
            exit(1);    //FIXME: throw exception
        }
    } else {
        pipefds[0] = -1;
        pipefds[1] = -1;
    }
}

inline void initAllPipe(unsigned int size, pipe_t *pipes) {
    for(unsigned int i = 0; i < size; i++) {
        tryToPipe(pipes[i], true);
    }
}

inline void closeAllPipe(int size, pipe_t *pipefds) {
    for(int i = 0; i < size; i++) {
        tryToClose(pipefds[i]);
    }
}

struct PipeSet {
    pipe_t in;
    pipe_t out;
};

// FIXME: error reporting
inline PipeSet initPipeSet(ForkKind kind) {
    bool useInPipe = false;
    bool useOutPipe = false;

    switch(kind) {
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
        break;
    }

    PipeSet set;    //NOLINT
    tryToPipe(set.in, useInPipe);
    tryToPipe(set.out, useOutPipe);
    return set;
}

inline void redirInToNull() {
    int fd = open("/dev/null", O_WRONLY);
    dup2(fd, STDIN_FILENO);
    close(fd);
}

inline bool needForeground(ForkKind kind) {
    return kind == ForkKind::ARRAY || kind == ForkKind::STR;
}

constexpr unsigned int READ_PIPE = 0;
constexpr unsigned int WRITE_PIPE = 1;

inline void flushStdFD() {
    fflush(stdin);
    fflush(stdout);
    fflush(stderr);
}

/**
 * for pipeline
 */
class PipelineState : public DSObject {
private:
    DSState &state;
    Job entry;

public:
    NON_COPYABLE(PipelineState);

    PipelineState(DSState &state, Job &&entry) :
            DSObject(nullptr), state(state), entry(std::move(entry)) {}

    ~PipelineState() override;
};

/**
 * for io redirection
 */
class RedirConfig : public DSObject {
private:
    unsigned int backupFDset{0};   // if corresponding bit is set, backup old fd

    std::vector<std::pair<RedirOP, DSValue>> ops;

    int oldFds[3];

public:
    NON_COPYABLE(RedirConfig);

    RedirConfig() : DSObject(nullptr), oldFds{-1, -1, -1} {}

    ~RedirConfig() override;

    void addRedirOp(RedirOP op, DSValue &&arg) {
        this->ops.emplace_back(op, std::move(arg));
        this->backupFDset |= getChangedFD(op);
    }

    void ignoreBackup() {
        this->backupFDset = 0;
    }

    bool redirect(DSState &st);

private:
    void backupFDs() {
        for(unsigned int i = 0; i < 3; i++) {
            if(this->backupFDset & (1u << i)) {
                this->oldFds[i] = fcntl(i, F_DUPFD_CLOEXEC, 0);
            }
        }
    }

    void restoreFDs() {
        for(unsigned int i = 0; i < 3; i++) {
            if(this->backupFDset & (1u << i)) {
                dup2(this->oldFds[i], i);
            }
        }
    }
};

} // namespace ydsh

#endif //YDSH_REDIR_H
