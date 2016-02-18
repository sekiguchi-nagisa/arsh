/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_CORE_PROC_H
#define YDSH_CORE_PROC_H

#include <cstring>
#include <cassert>

#include "object.h"
#include "../misc/hash.hpp"
#include "../misc/noncopyable.h"

namespace ydsh {
namespace ast {

class UserDefinedCmdNode;

}
}

namespace ydsh {
namespace core {

class RuntimeContext;
enum class EvalStatus : unsigned int;

constexpr unsigned int READ_PIPE = 0;
constexpr unsigned int WRITE_PIPE = 1;

#define EACH_RedirectOP(OP) \
    OP(IN_2_FILE, "<") \
    OP(OUT_2_FILE, "1>") \
    OP(OUT_2_FILE_APPEND, "1>>") \
    OP(ERR_2_FILE, "2>") \
    OP(ERR_2_FILE_APPEND, "2>>") \
    OP(MERGE_ERR_2_OUT_2_FILE, "&>") \
    OP(MERGE_ERR_2_OUT_2_FILE_APPEND, "&>>") \
    OP(MERGE_ERR_2_OUT, "2>&1") \
    OP(MERGE_OUT_2_ERR, "1>&2")

enum RedirectOP : unsigned int {
#define GEN_ENUM(ENUM, STR) ENUM,
    EACH_RedirectOP(GEN_ENUM)
#undef GEN_ENUM
    DUMMY,
};

/**
 * for builtin command argument.
 * following variables are read-only.
 */
struct BuiltinContext {
    /**
     * number of argv, exclude last element
     */
    int argc;

    /**
     * first element of argv is command name.
     * last element of argv is null.
     */
    char *const *argv;

    // not close theme
    FILE *fp_stdin;
    FILE *fp_stdout;
    FILE *fp_stderr;

    BuiltinContext(int argc, char **argv, int stdin_fd, int stdout_fd, int stderr_fd);
    explicit BuiltinContext(char *const *argv, int stdin_fd, int stdout_fd, int stderr_fd);

    /**
     * offset must be under bctx.argc.
     * copy file pointer
     */
    BuiltinContext(int offset, const BuiltinContext &bctx);

    NON_COPYABLE(BuiltinContext);
};

/**
 * return exit status.
 */
typedef int (*builtin_command_t)(RuntimeContext *ctx, const BuiltinContext &bctx);

unsigned int getBuiltinCommandSize();

/**
 * if index is out of range, return null
 */
const char *getBultinCommandName(unsigned int index);

// for error reporting
struct ChildError {
    /**
     * index of redirect option having some error.
     * if 0, has no error in redirection.
     */
    unsigned int redirIndex;

    /**
     * error number of occurred error.
     */
    int errorNum;

    ChildError() : redirIndex(0), errorNum(0) { }
    ~ChildError() = default;

    operator bool() const {
        return errorNum == 0 && redirIndex == 0;
    }
};

class ProcState {
public:
    enum ProcKind : unsigned int {
        EXTERNAL,
        BUILTIN,
        USER_DEFINED,
    };

    enum ExitKind : unsigned int {
        NORMAL,
        INTR,
    };

private:
    unsigned int __argOffset;

    /**
     * if not have redirect option, offset is 0.
     */
    unsigned int __redirOffset;

    ProcKind __procKind;

    union {
        void *__dummy;
        ast::UserDefinedCmdNode *__udcNode;
        builtin_command_t __builtinCmd;
        const char *__filePath;   // may be null if not found file
    };


    /**
     * following fields are valid, if parent process.
     */

    ExitKind __kind;
    pid_t __pid;
    int __exitStatus;

public:
    ProcState() = default;

    ProcState(unsigned int argOffset, unsigned int redirOffset, ProcKind procKind, void *ptr) :
            __argOffset(argOffset), __redirOffset(redirOffset),
            __procKind(procKind), __dummy(ptr),
            __kind(NORMAL), __pid(0), __exitStatus(0) { }

    ~ProcState() = default;

    /**
     * only called, if parent process.
     */
    void set(ExitKind kind, int exitStatus) {
        this->__kind = kind;
        this->__exitStatus = exitStatus;
    }

    /**
     * only called, if parent process.
     */
    void setPid(pid_t pid) {
        this->__pid = pid;
    }

    unsigned int argOffset() const {
        return this->__argOffset;
    }

    unsigned int redirOffset() const {
        return this->__redirOffset;
    }

    ProcKind procKind() const {
        return this->__procKind;
    }

    ast::UserDefinedCmdNode *udcNode() const {
        return this->__udcNode;
    }

    builtin_command_t builtinCmd() const {
        return this->__builtinCmd;
    }

    const char *filePath() const {
        return this->__filePath;
    }

    ExitKind kind() const {
        return this->__kind;
    }

    pid_t pid() const {
        return this->__pid;
    }

    int exitStatus() const {
        return this->__exitStatus;
    }
};

class PipelineEvaluator {
private:
    /**
     * stack top index before pipeline evaluation.
     */
    unsigned int stacktopIndex;

    /**
     * commonly stored object is String_Object.
     */
    std::vector<DSValue> argArray;

    /**
     * pair's second must be String_Object
     */
    std::vector<std::pair<RedirectOP, DSValue>> redirOptions;

    std::vector<ProcState> procStates;

public:
    NON_COPYABLE(PipelineEvaluator);

    PipelineEvaluator() = default;

    ~PipelineEvaluator() = default;

    void setStackTopIndex(unsigned int stackTopIndex) {
        this->stacktopIndex = stackTopIndex;
    }

    unsigned int getStackTopIndex() const {
        return this->stacktopIndex;
    }

    std::vector<DSValue> &getArgArray() {
        return this->argArray;
    }

    std::vector<std::pair<RedirectOP, DSValue>> &getRedirOptions() {
        return this->redirOptions;
    }

    std::vector<ProcState> &getProcStates() {
        return this->procStates;
    }

    void clear() {
        this->stacktopIndex = 0;
        this->argArray.clear();
        this->redirOptions.clear();
        this->procStates.clear();
    }

    EvalStatus evalPipeline(RuntimeContext &ctx);

private:
    bool redirect(RuntimeContext &ctx, unsigned int procIndex,
                  int errorPipe, int stdin_fd, int stdout_fd, int stderr_fd);

    DSValue *getARGV(unsigned int procIndex);
    const char *getCommandName(unsigned int procIndex);

    bool checkChildError(RuntimeContext &ctx, const std::pair<unsigned int, ChildError> &errorPair);
};


} // namespace core
} // namespace ydsh


#endif //YDSH_CORE_PROC_H
