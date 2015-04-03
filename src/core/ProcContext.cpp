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

#include <core/ProcContext.h>
#include <misc/debug.h>

#define READ_PIPE  0
#define WRITE_PIPE 1

namespace ydsh {
namespace core {

// #########################
// ##     ProcContext     ##
// #########################

ProcContext::ProcContext(const std::string &cmdName) :
        DSObject(0), cmdName(cmdName), params(), argv(), pid(0),
        exitKind(NORMAL), exitStatus(0) {
}

ProcContext::~ProcContext() {
    delete[] this->argv;    // not delete element
}

std::string ProcContext::toString() {
    return "<process>"; //FIXME
}

void ProcContext::addParam(const std::shared_ptr<DSObject> &value) {
    String_Object *strObj = TYPE_AS(String_Object, value);
    if(strObj != 0) {
        this->params.push_back(std::dynamic_pointer_cast<String_Object>(value));
        return;
    }

    Array_Object *arrayObj = TYPE_AS(Array_Object, value);
    if(arrayObj != 0) {
        for(const std::shared_ptr<DSObject> &element : arrayObj->values) {
            this->params.push_back(std::dynamic_pointer_cast<String_Object>(element));
        }
    } else {
        fatal("illegal command parameter type: %d\n", value->getType()->getTypeId());
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

int ProcGroup::execProcs() {
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
            return -1;
        }
    }

    // fork
    unsigned int procIndex;
    for(procIndex = 0; procIndex < this->procSize && (pid[procIndex] = fork()) > 0; procIndex++) {
        this->procs[procIndex]->pid = pid[procIndex];
    }

    if(procIndex == this->procSize) {   // parent process
        //TODO: asynchronous invocation
        closeAllPipe(this->procSize - 1, pipefds);
        close(pipefds[this->procSize - 1][WRITE_PIPE]);
        //TODO: stdout capture.
        close(pipefds[this->procSize - 1][READ_PIPE]);

        // wait for exit
        for(unsigned int i = 0; i < this->procSize; i++) {
            int status;
            ProcContext *ctx = this->procs[i].get();
            waitpid(pid[i], &status, 0);
            if(WIFEXITED(status)) {
                ctx->exitKind = ProcContext::NORMAL;
                ctx->exitStatus = WEXITSTATUS(status);
            }
            if(WIFSIGNALED(status)) {
                ctx->exitKind = ProcContext::INTR;
                ctx->exitStatus = WTERMSIG(status);
            }
        }
        return 0;
    } else if(pid[procIndex] == 0) { // child process
        ProcContext *ctx = this->procs[procIndex].get();
        if(procIndex == 0) {    // first process
            //TODO: redirect
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
            //TODO: stdout capture
        }
        closeAllPipe(this->procSize, pipefds);
        execvp(ctx->argv[0], ctx->argv);
        perror("execution error");
        fprintf(stderr, "executed cmd: %s\n", ctx->argv[0]);
        exit(1);
    } else {
        perror("child process error");
        return -1;
    }
}

} // namespace core
} // namespace ydsh
