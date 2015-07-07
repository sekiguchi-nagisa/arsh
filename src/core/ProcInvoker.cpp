/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <cstdlib>

#include "ProcInvoker.h"
#include "RuntimeContext.h"
#include "../misc/num.h"
#include "../misc/debug.h"

namespace ydsh {
namespace core {

// #########################
// ##     ProcInvoker     ##
// #########################

void ProcInvoker::openProc() {
    unsigned int offset = this->argArray.getUsedSize();
    this->procOffsets.push_back(std::make_pair(offset, 0));
}

void ProcInvoker::closeProc() {
    this->argArray.append(nullptr);
    this->redirOptions.push_back(std::make_pair(RedirectOP::DUMMY, nullptr));
}

void ProcInvoker::addCommandName(const std::string &name) {
    this->argArray.append(name.c_str());
}

void ProcInvoker::addParam(const std::shared_ptr<DSObject> &value, bool skipEmptyString) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->getPool().getStringType()) {
        std::shared_ptr<String_Object> obj = std::dynamic_pointer_cast<String_Object>(value);
        if(skipEmptyString && obj->getValue().empty()) {
            return;
        }
        this->argArray.append(obj->getValue().c_str());
        return;
    }

    if(*valueType == *this->ctx->getPool().getStringArrayType()) {
        Array_Object *arrayObj = TYPE_AS(Array_Object, value);
        for(const std::shared_ptr<DSObject> &element : arrayObj->getValues()) {
            this->argArray.append(std::dynamic_pointer_cast<String_Object>(element)->getValue().c_str());
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->getPool().getTypeName(*valueType).c_str());
    }
}

void ProcInvoker::addRedirOption(RedirectOP op, const std::shared_ptr<DSObject> &value) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->getPool().getStringType()) {
        if(this->procOffsets.back().second == 0) {
            this->procOffsets.back().second = this->redirOptions.size();
        }
        this->redirOptions.push_back(
                std::make_pair(op, std::dynamic_pointer_cast<String_Object>(value)->getValue().c_str()));
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->getPool().getTypeName(*valueType).c_str());
    }
}

static void closeAllPipe(int size, int pipefds[][2]) {
    for(int i = 0; i < size; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
}

static void redirectToFile(const char *fileName, const char *mode, int targetFD) {
    FILE *fp = fopen(fileName, mode);
    if(fp != NULL) {
        int fd = fileno(fp);
        dup2(fd, targetFD);
        fclose(fp);
    } else {
        fprintf(stderr, "-ydsh: %s: ", fileName);
        perror("");
        exit(1);
    }
}

/**
 * if redirection failed, exit 1
 */
void ProcInvoker::redirect(unsigned int procIndex) {  //FIXME: error reporting
    unsigned int startIndex = this->procOffsets[procIndex].second;
    for(; this->redirOptions[startIndex].first != RedirectOP:: DUMMY; startIndex++) {
        const std::pair<RedirectOP, const char *> &pair = this->redirOptions[startIndex];
        switch(pair.first) {
        case IN_2_FILE: {
            redirectToFile(pair.second, "rb", STDIN_FILENO);
            break;
        };
        case OUT_2_FILE: {
            redirectToFile(pair.second, "wb", STDOUT_FILENO);
            break;
        };
        case OUT_2_FILE_APPEND: {
            redirectToFile(pair.second, "ab", STDOUT_FILENO);
            break;
        };
        case ERR_2_FILE: {
            redirectToFile(pair.second, "wb", STDERR_FILENO);
            break;
        };
        case ERR_2_FILE_APPEND: {
            redirectToFile(pair.second, "ab", STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE: {
            redirectToFile(pair.second, "wb", STDOUT_FILENO);
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            redirectToFile(pair.second, "ab", STDOUT_FILENO);
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT: {
            dup2(STDOUT_FILENO, STDERR_FILENO);
            break;
        };
        case MERGE_OUT_2_ERR: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            break;
        }
        default:
            fatal("unsupported redir option: %d\n", pair.first);
        }
    }
}

static EvalStatus execBuiltin(RuntimeContext *ctx, char **argv) {   //FIXME: error report
    unsigned int argSize = 0;
    for(; argv[argSize + 1] != nullptr; argSize++);

    if(strcmp(argv[0], "cd") == 0) {
        const char *targetDir = getenv("HOME");
        if(argSize > 0) {
            targetDir = argv[1];
        }
        if(chdir(targetDir) != 0) {
            perror("-ydsh: cd");
            return EvalStatus::SUCCESS;
        }
        ctx->updateWorkingDir();
        return EvalStatus::SUCCESS;
    } else if(strcmp(argv[0], "exit") == 0) {
        if(argSize == 0) {
            ctx->exitShell(0);
        } else if(argSize > 0) {
            const char *num = argv[1];
            int status;
            long value = convertToInt64(num, status, false);
            if(status == 0) {
                ctx->exitShell(value);
            } else {
                ctx->exitShell(0);
            }
        }
        return EvalStatus::THROW;
    } else {
        fatal("unsupported builtin command %s\n",argv[0]);
        return EvalStatus::SUCCESS;
    }
}

EvalStatus ProcInvoker::invoke() {
    const unsigned int procSize = this->procOffsets.size();

    // check builtin(cd, exit)
    if(procSize == 1) {
        char **argv = this->argArray.getRawData(this->procOffsets[0].first);
        if(strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "exit") == 0) {
            return execBuiltin(this->ctx, argv);
        }
    }

    // create pipe
    pid_t pid[procSize];
    int pipefds[procSize][2];
    for(unsigned int i = 0; i < procSize; i++) {
        if(pipe(pipefds[i]) < 0) {
            perror("pipe creation error");
            exit(1);
        }
    }

    // fork
    unsigned int procIndex;
    for(procIndex = 0; procIndex < procSize && (pid[procIndex] = fork()) > 0; procIndex++) {
        this->procCtxs.push_back(ProcInvoker::ProcContext(pid[procIndex]));
    }

    if(procIndex == procSize) {   // parent process
        closeAllPipe(procSize - 1, pipefds);
        close(pipefds[procSize - 1][WRITE_PIPE]);
        close(pipefds[procSize - 1][READ_PIPE]);

        // wait for exit
        for(unsigned int i = 0; i < procSize; i++) {
            int status;
            waitpid(pid[i], &status, 0);
            if(WIFEXITED(status)) {
                this->procCtxs[i].set(ExitKind::NORMAL, WEXITSTATUS(status));
            }
            if(WIFSIGNALED(status)) {
                this->procCtxs[i].set(ExitKind::INTR, WTERMSIG(status));
            }
        }
        ctx->updateExitStatus(this->procCtxs[procSize - 1].exitStatus);
        return EvalStatus::SUCCESS;
    } else if(pid[procIndex] == 0) { // child process
        if(procIndex == 0) {    // first process
            if(procSize > 1) {
                dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
            }
        }
        if(procIndex > 0 && procIndex < procSize - 1) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == procSize - 1) { // last proc
            if(procSize > 1) {
                dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            }
        }

        this->redirect(procIndex);

        closeAllPipe(procSize, pipefds);

        char **argv = this->argArray.getRawData(this->procOffsets[procIndex].first);
        execvp(argv[0], argv);
        perror("execution error");
        fprintf(stderr, "executed cmd: %s\n", argv[0]);
        exit(1);
    } else {
        perror("child process error");
        exit(1);
    }
}


} // namespace core
} // namespace ydsh