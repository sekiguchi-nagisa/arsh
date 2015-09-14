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

#ifndef YDSH_PROCINVOKER_H
#define YDSH_PROCINVOKER_H

#include <cstring>
#include <cassert>

#include "DSObject.h"
#include "misc/hash.hpp"

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
};

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


class ProcInvoker {
public:
    /**
     * if some error happened(ex. exit), raised will be true.
     * return exit status.
     */
    typedef int (*builtin_command_t)(RuntimeContext *ctx, const BuiltinContext &bctx, bool &raised);

private:
    enum ExitKind : unsigned int {
        NORMAL,
        INTR,
    };

    struct ProcContext{
        ExitKind kind;
        int pid;
        int exitStatus;

        ProcContext() = default;
        explicit ProcContext(int pid) : kind(ExitKind::NORMAL), pid(pid), exitStatus(0) { }
        ~ProcContext() = default;

        void set(ExitKind kind, int exitStatus) {
            this->kind = kind;
            this->exitStatus = exitStatus;
        }
    };

    /**
     * not delete it
     */
    RuntimeContext *ctx;

    /**
     * builtin command name and pointer.
     */
    CStringHashMap<builtin_command_t> builtinMap;

    /**
     * commonly stored object is String_Object.
     */
    std::vector<DSValue> argArray;

    /**
     * pair's second must be String_Object
     */
    std::vector<std::pair<RedirectOP, DSValue>> redirOptions;

    /**
     * first is argument offset
     * second is redirect option offset.
     * if not have redirect option, second is 0.
     */
    std::vector<std::pair<unsigned int, unsigned int>> procOffsets;

    std::vector<ProcContext> procCtxs;

public:
    ProcInvoker(RuntimeContext *ctx);
    ~ProcInvoker() = default;

    void clear() {
        this->argArray.clear();
        this->redirOptions.clear();
        this->procOffsets.clear();
        this->procCtxs.clear();
    }

    void openProc();
    void closeProc();
    void addCommandName(DSValue &&value);
    void addArg(DSValue &&value, bool skipEmptyString);
    void addRedirOption(RedirectOP op, DSValue &&value);

    EvalStatus invoke();

    /**
     * first element of argv is command name.
     * last element of argv is null.
     */
    EvalStatus execBuiltinCommand(char *const argv[]);

    /**
     * return null, if not found builtin command.
     */
    builtin_command_t lookupBuiltinCommand(const char *commandName);

    /**
     * write status to status (same of wait's status).
     */
    void forkAndExec(const BuiltinContext &bctx, int &status, bool useDefaultPath = false);

private:
    bool redirect(unsigned int procIndex, int errorPipe, int stdin_fd, int stdout_fd, int stderr_fd);

    DSValue *getARGV(unsigned int procIndex);

    const char *getCommandName(unsigned int procIndex);
    bool checkChildError(const std::pair<unsigned int, ChildError> &errorPair);
};


} // namespace core
} // namespace ydsh


#endif //YDSH_PROCINVOKER_H
