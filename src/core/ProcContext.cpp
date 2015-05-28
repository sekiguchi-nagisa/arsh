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
#include <stdlib.h>

#include "RuntimeContext.h"
#include "../misc/files.h"
#include "../misc/num.h"

namespace ydsh {
namespace core {

// #########################
// ##     ProcContext     ##
// #########################

ProcContext::ProcContext(RuntimeContext &ctx, const std::string &cmdName) :
        DSObject(0), ctx(&ctx), cmdName(cmdName), params(), redirs(), argv(), pid(0),
        exitKind(NORMAL), exitStatus(0) {
}

ProcContext::~ProcContext() {
    delete[] this->argv;    // not delete element
}

void ProcContext::addParam(std::shared_ptr<DSObject> &&value, bool skipEmptyString) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->pool.getStringType()) {
        std::shared_ptr<String_Object> obj = std::dynamic_pointer_cast<String_Object>(value);
        if(skipEmptyString && obj->value.empty()) {
            return;
        }
        this->params.push_back(std::move(obj));
        return;
    }

    if(*valueType == *this->ctx->pool.getStringArrayType()) {
        Array_Object *arrayObj = TYPE_AS(Array_Object, value);
        for(const std::shared_ptr<DSObject> &element : arrayObj->values) {
            this->params.push_back(std::dynamic_pointer_cast<String_Object>(element));
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->pool.getTypeName(*valueType).c_str());
    }
}

void ProcContext::addRedirOption(RedirectOP op, std::shared_ptr<DSObject> &&value) {
    DSType *valueType = value->getType();
    if(*valueType == *this->ctx->pool.getStringType()) {
        this->redirs.push_back(std::make_pair(op, std::dynamic_pointer_cast<String_Object>(value)));
        return;
    }

    if(*valueType == *this->ctx->pool.getStringArrayType()) {
        Array_Object *arrayObj = TYPE_AS(Array_Object, value);
        unsigned int count = 0;
        for(auto &element : arrayObj->values) {
            if(count++ == 0) {
                this->redirs.push_back(std::make_pair(op, std::dynamic_pointer_cast<String_Object>(element)));
            } else {
                this->params.push_back(std::dynamic_pointer_cast<String_Object>(element));
            }
        }
    } else {
        fatal("illegal command parameter type: %s\n", this->ctx->pool.getTypeName(*valueType).c_str());
    }
}

void ProcContext::prepare() {
    unsigned int argc = this->params.size() + 2;
    this->argv = new char *[argc];
    this->argv[0] = (char *) this->cmdName.c_str();
    for(unsigned int i = 1; i < argc - 1; i++) {
        this->argv[i] = (char *) this->params[i - 1]->value.c_str();
    }
    this->argv[argc - 1] = NULL;
}


// #######################
// ##     ProcGroup     ##
// #######################

ProcGroup::ProcGroup(unsigned int procSize) :
        procSize(procSize), procs(new std::shared_ptr<ProcContext>[procSize]) {
}

ProcGroup::~ProcGroup() {
    delete[] this->procs;
}

void ProcGroup::addProc(unsigned int index, const std::shared_ptr<ProcContext> &ctx) {
    this->procs[index] = ctx;
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
static void redirect(ProcContext *ctx) {  //FIXME: error reporting
    for(auto &pair : ctx->redirs) {
        switch(pair.first) {
        case IN_2_FILE: {
            redirectToFile(pair.second->value.c_str(), "rb", STDIN_FILENO);
            break;
        };
        case OUT_2_FILE: {
            redirectToFile(pair.second->value.c_str(), "wb", STDOUT_FILENO);
            break;
        };
        case OUT_2_FILE_APPEND: {
            redirectToFile(pair.second->value.c_str(), "ab", STDOUT_FILENO);
            break;
        };
        case ERR_2_FILE: {
            redirectToFile(pair.second->value.c_str(), "wb", STDERR_FILENO);
            break;
        };
        case ERR_2_FILE_APPEND: {
            redirectToFile(pair.second->value.c_str(), "ab", STDERR_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            redirectToFile(pair.second->value.c_str(), "wb", STDOUT_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT_2_FILE_APPEND: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            redirectToFile(pair.second->value.c_str(), "ab", STDOUT_FILENO);
            break;
        };
        case MERGE_ERR_2_OUT: {
            dup2(STDERR_FILENO, STDOUT_FILENO);
            break;
        };
        }
    }
}

static EvalStatus execBuiltin(ProcContext *procCtx) {   //FIXME: error report
    unsigned int paramSize = procCtx->params.size();
    auto ctx = procCtx->ctx;
    if(procCtx->cmdName == "cd") {
        const char *targetDir = getenv("HOME");
        if(paramSize > 0) {
            targetDir = procCtx->params[0]->value.c_str();
        }
        if(chdir(targetDir) != 0) {
            perror("-ydsh: cd");
            return EvalStatus::SUCCESS;
        }
        ctx->workingDir = getCurrentWorkingDir();
        return EvalStatus::SUCCESS;
    } else if(procCtx->cmdName == "exit") {
        if(paramSize == 0) {
            ctx->updateExitStatus(0);
        } else if(paramSize > 0) {
            const char *num = procCtx->params[0]->value.c_str();
            int status;
            long value = convertToInt64(num, status, false);
            if(status == 0) {
                ctx->updateExitStatus(value);
            } else {
                ctx->updateExitStatus(0);
            }
        }
        return EvalStatus::EXIT;
    } else {
        fatal("unsupported builtin command %s\n", procCtx->cmdName.c_str());
        return EvalStatus::SUCCESS;
    }
}

EvalStatus ProcGroup::execProcs() {    //FIXME: builtin
    // check builtin(cd, exit)
    if(this->procSize == 1 && (this->procs[0]->cmdName == "cd" || this->procs[0]->cmdName == "exit")) {
        return execBuiltin(this->procs[0].get());
    }

    RuntimeContext *ctx = this->procs[0]->ctx;

    // prepare each proc
    for(unsigned int i = 0; i < this->procSize; i++) {
        this->procs[i]->prepare();
    }

    pid_t pid[this->procSize];
    int pipefds[this->procSize][2];

    // create pipe
    for(unsigned int i = 0; i < this->procSize; i++) {
        if(pipe(pipefds[i]) < 0) {
            perror("pipe creation error");
            exit(1);
        }
    }

    // fork
    unsigned int procIndex;
    for(procIndex = 0; procIndex < this->procSize && (pid[procIndex] = fork()) > 0; procIndex++) {
        this->procs[procIndex]->pid = pid[procIndex];
    }

    if(procIndex == this->procSize) {   // parent process
        closeAllPipe(this->procSize - 1, pipefds);
        close(pipefds[this->procSize - 1][WRITE_PIPE]);
        close(pipefds[this->procSize - 1][READ_PIPE]);

        // wait for exit
        for(unsigned int i = 0; i < this->procSize; i++) {
            int status;
            ProcContext *procCtx = this->procs[i].get();
            waitpid(pid[i], &status, 0);
            if(WIFEXITED(status)) {
                procCtx->exitKind = ProcContext::NORMAL;
                procCtx->exitStatus = WEXITSTATUS(status);
            }
            if(WIFSIGNALED(status)) {
                procCtx->exitKind = ProcContext::INTR;
                procCtx->exitStatus = WTERMSIG(status);
            }
        }
        ctx->updateExitStatus(this->procs[this->procSize - 1]->exitStatus);
        return EvalStatus::SUCCESS ;
    } else if(pid[procIndex] == 0) { // child process
        ProcContext *procCtx = this->procs[procIndex].get();
        if(procIndex == 0) {    // first process
            if(this->procSize > 1) {
                dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
            }
        }
        if(procIndex > 0 && procIndex < this->procSize - 1) {   // other process.
            dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            dup2(pipefds[procIndex][WRITE_PIPE], STDOUT_FILENO);
        }
        if(procIndex == this->procSize - 1) { // last proc
            if(this->procSize > 1) {
                dup2(pipefds[procIndex - 1][READ_PIPE], STDIN_FILENO);
            }
        }

        redirect(procCtx);

        closeAllPipe(this->procSize, pipefds);
        execvp(procCtx->argv[0], procCtx->argv);
        perror("execution error");
        fprintf(stderr, "executed cmd: %s\n", procCtx->argv[0]);
        exit(1);
    } else {
        perror("child process error");
        exit(1);
    }
}

} // namespace core
} // namespace ydsh
