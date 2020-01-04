/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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

#ifndef YDSH_VM_H
#define YDSH_VM_H

#include <chrono>
#include <cstdio>

#include <ydsh/ydsh.h>

#include "cmd.h"
#include "symbol_table.h"
#include "signals.h"
#include "core.h"
#include "job.h"
#include "misc/noncopyable.h"
#include "state.h"

namespace ydsh {

enum class VMEvent : unsigned int {
    HOOK   = 1u << 0u,
    SIGNAL = 1u << 1u,
    MASK   = 1u << 2u,
};

enum class EvalOP : unsigned int {
    PROPAGATE  = 1u << 0u,    // propagate uncaught exception to caller (except for subshell).
    SKIP_TERM  = 1u << 1u,    // not call termination handler (except for subshell).
    HAS_RETURN = 1u << 2u,    // may have return value.
    COMMIT     = 1u << 3u,    // after evaluation, commit/abort symbol table
};

template <> struct allow_enum_bitop<VMEvent> : std::true_type {};

template <> struct allow_enum_bitop<EvalOP> : std::true_type {};

class VM;

}

using namespace ydsh;

struct DSState {
public:
    SymbolTable symbolTable;

    /**
     * must be Boolean_Object
     */
    const DSValue trueObj;

    /**
     * must be Boolean_Object
     */
    const DSValue falseObj;

    /**
     * must be String_Object
     */
    const DSValue emptyStrObj;

    const DSValue emptyFDObj;

    /**
     * maintains latest result of EDIT_HOOK
     */
    DSValue editOpReply;

    /**
     * maintain latest rendered prompt/
     */
    DSValue prompt;

    unsigned short option{DS_OPTION_ASSERT};

    DSExecMode execMode{DS_EXEC_MODE_NORMAL};

    DumpTarget dumpTarget;

    /**
     * cache searched result.
     */
    FilePathCache pathCache;

    unsigned int lineNum{1};

    /**
     * if 0, current shell is not sub-shell.
     * otherwise current shell is sub-shell.
     */
    unsigned int subshellLevel{0};

    std::string logicalWorkingDir;

    SignalVector sigVector;

    JobTable jobTable;

    /**
     * for OP_STR.
     */
    std::string toStrBuf;

private:
    friend class ydsh::VM;

    VMHook *hook{nullptr};

    std::vector<DSValue> globals{64};

    VMState stack;

    decltype(std::chrono::system_clock::now()) baseTime;

public:
    static VMEvent eventDesc;

    static SigSet pendingSigSet;

    NON_COPYABLE(DSState);

    DSState();

    ~DSState() = default;

    int getExitStatus() const {
        return typeAs<Int_Object>(this->getGlobal(BuiltinVarOffset::EXIT_STATUS))->getValue();
    }

    const char *getScriptDir() const {
        return typeAs<String_Object>(this->getGlobal(BuiltinVarOffset::SCRIPT_DIR))->getValue();
    }

    bool hasError() const {
        return this->stack.hasError();
    }

    /**
     * set thrownObject and update exit status
     * @param except
     * @param afterStatus
     * set exit status to it
     */
    void throwObject(DSValue &&except, int afterStatus) {
        this->stack.setThrownObject(std::move(except));
        this->updateExitStatus(afterStatus);
    }

    // variable manipulation
    void setGlobal(unsigned int index, const DSValue &obj) {
        this->setGlobal(index, DSValue(obj));
    }

    void setGlobal(BuiltinVarOffset offset, DSValue &&obj) {
        this->setGlobal(toIndex(offset), std::move(obj));
    }

    void setGlobal(unsigned int index, DSValue &&obj) {
        this->globals[index] = std::move(obj);
    }

    const DSValue &getGlobal(unsigned int index) const {
        return this->globals[index];
    }

    const DSValue &getGlobal(BuiltinVarOffset offset) const {
        return this->getGlobal(toIndex(offset));
    }

    void setLocal(unsigned char index, DSValue &&obj) {
        this->stack.setLocal(index, std::move(obj));
    }

    const DSValue &getLocal(unsigned char index) const {
        return this->stack.getLocal(index);
    }

    DSValue moveLocal(unsigned char index) {
        return this->stack.moveLocal(index);
    }

    void updateExitStatus(unsigned int status) {
        this->setGlobal(BuiltinVarOffset::EXIT_STATUS,
                DSValue::create<Int_Object>(this->symbolTable.get(TYPE::Int32), status));
    }

    void updatePipeStatus(unsigned int size, const Proc *procs, bool mergeExitStatus);

    bool isJobControl() const {
        return hasFlag(this->option, DS_OPTION_JOB_CONTROL);
    }

    bool isRootShell() const {
        int shellpid = typeAs<Int_Object>(this->getGlobal(BuiltinVarOffset::SHELL_PID))->getValue();
        int pid = typeAs<Int_Object>(this->getGlobal(BuiltinVarOffset::PID))->getValue();
        return shellpid == pid;
    }

    bool isForeground() const {
        return this->isJobControl() && this->isRootShell();
    }

    void setVMHook(VMHook *hook) {
        this->hook = hook;
        if(hook != nullptr) {
            setFlag(eventDesc, VMEvent::HOOK);
        } else {
            unsetFlag(eventDesc, VMEvent::HOOK);
        }
    }

    const VMState &getCallStack() const {
        return this->stack;
    }
};

namespace ydsh {

struct Command {
    enum CmdKind {
        USER_DEFINED,
        BUILTIN_S,
        BUILTIN,
        EXTERNAL,
    } kind;

    union {
        const DSCode *udc;
        builtin_command_t builtinCmd;
        const char *filePath;   // may be null if not found file
    };
};

class CmdResolver {
public:
    enum ResolveOp {
        MASK_UDC      = 1u << 0u,
        MASK_EXTERNAL = 1u << 1u,
        MASK_FALLBACK = 1u << 2u,
    };

private:
    ResolveOp mask;
    FilePathCache::SearchOp searchOp;

public:
    CmdResolver(ResolveOp mask, FilePathCache::SearchOp op) : mask(mask), searchOp(op) {}

    CmdResolver() : CmdResolver({}, FilePathCache::NON) {}

    ~CmdResolver() = default;

    Command operator()(DSState &state, const char *cmdName) const;
};

template <> struct allow_enum_bitop<CmdResolver::ResolveOp> : std::true_type {};

class VM {
private:
    static void pushExitStatus(DSState &state, int status) {
        state.updateExitStatus(status);
        state.stack.push(status == 0 ? state.trueObj : state.falseObj);
    }

    static bool windStackFrame(DSState &state, unsigned int stackTopOffset,
                               unsigned int paramSize, const DSCode *code) {
        auto ret = state.stack.wind(stackTopOffset, paramSize, code);
        if(!ret) {
            raiseError(state, TYPE::StackOverflowError, "local stack size reaches limit");
        }
        return ret;
    }

    // runtime api
    static bool checkCast(DSState &state, DSType *targetType);

    static bool checkAssertion(DSState &state);

    static const char *loadEnv(DSState &state, bool hasDefault);

    /**
     * stack state in function apply    stack grow ===>
     *
     * +-----------+---------+--------+   +--------+
     * | stack top | funcObj | param1 | ~ | paramN |
     * +-----------+---------+--------+   +--------+
     *                       | offset |   |        |
     */
    static bool prepareFuncCall(DSState &state, unsigned int paramSize) {
        auto *func = typeAs<FuncObject>(state.stack.peekByOffset(paramSize));
        return windStackFrame(state, paramSize + 1, paramSize, &func->getCode());
    }

    /**
     * stack state in method call    stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             | offset           |   |        |
     */
    static bool prepareMethodCall(DSState &state, unsigned short index, unsigned short paramSize) {
        const unsigned int actualParamSize = paramSize + 1; // include receiver
        return windStackFrame(state, actualParamSize, actualParamSize,
                              state.stack.peekByOffset(paramSize)->getType()->getMethodRef(index));
    }

    /**
     * stack state in constructor call     stack grow ===>
     *
     * +-----------+------------------+   +--------+
     * | stack top | param1(receiver) | ~ | paramN |
     * +-----------+------------------+   +--------+
     *             |    new offset    |
     */
    static bool prepareConstructorCall(DSState &state, unsigned short paramSize) {
        return windStackFrame(state, paramSize, paramSize + 1,
                              state.stack.peekByOffset(paramSize)->getType()->getConstructor());
    }

    /**
     * stack state in function apply    stack grow ===>
     *
     * +-----------+---------------+--------------+
     * | stack top | param1(redir) | param2(argv) |
     * +-----------+---------------+--------------+
     *             |     offset    |
     */
    static bool prepareUserDefinedCommandCall(DSState &state, const DSCode *code, DSValue &&argvObj,
                                              DSValue &&restoreFD, flag8_set_t attr);

    static bool forkAndEval(DSState &state);

    static int forkAndExec(DSState &state, const char *fillPath, char *const *argv, DSValue &&redirConfig);

    static bool callCommand(DSState &state, CmdResolver resolver,
                            DSValue &&argvObj, DSValue &&redirConfig, flag8_set_t attr = 0);

    static bool callBuiltinCommand(DSState &state, DSValue &&argvObj, DSValue &&redir, flag8_set_t attr);

    static void callBuiltinExec(DSState &state, DSValue &&array, DSValue &&redir);

    /**
     *
     * @param lastPipe
     * if true, evaluate last pipe in parent shell
     * @return
     * if has error, return false.
     */
    static bool callPipeline(DSState &state, bool lastPipe);

    static void addCmdArg(DSState &state, bool skipEmptyStr);

    static bool kickSignalHandler(DSState &state, int sigNum, DSValue &&func);

    static bool checkVMEvent(DSState &state);

    /**
     *
     * @return
     * if has exception, return false.
     */
    static bool mainLoop(DSState &state);

    /**
     * if found exception handler, return true.
     * otherwise return false.
     */
    static bool handleException(DSState &state, bool forceUnwind);

    /**
     * actual entry point of interpreter.
     * @param op
     * @param dsError
     * if not null, set error info
     * @return
     * if has error or not value, return null
     * otherwise, return value
     */
    static DSValue startEval(DSState &state, EvalOP op, DSError *dsError);

    static unsigned int prepareArguments(VMState &state, DSValue &&recv,
                                         std::pair<unsigned int, std::array<DSValue, 3>> &&args);

    /**
     * print uncaught exception information.
     * @param except
     * uncaught exception
     * @param dsError
     * if not null, set error information
     * @return
     * if except is null, return always DS_ERROR_KIND_SUCCESS and not set error info
     */
    static DSErrorKind handleUncaughtException(DSState &state, const DSValue &except, DSError *dsError);

    /**
     * call user-defined termination handler specified by TERM_HOOK.
     * @param kind
     * @param except
     */
    static void callTermHook(DSState &state, DSErrorKind kind, DSValue &&except);

public:
    // entry point
    /**
     * entry point of toplevel code evaluation.
     * @param code
     * must be toplevel compiled code.
     * @param dsError
     * if not null, set error information
     * @return
     * exit status of latest executed command.
     */
    static int callToplevel(DSState &state, const CompiledCode &code, DSError *dsError);

    /**
     * execute command.
     * @param argv
     * DSValue must be String_Object
     * @param propagate
     * if true, not handle uncaught exception
     * @return
     * if exit status is 0, return true.
     * otherwise, return false
     */
    static DSValue execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate);

    /**
     * call method.
     * @param handle
     * must not be null
     * @param recv
     * @param args
     * @return
     * return value of method (if no return value, return null).
     */
    static DSValue callMethod(DSState &state, const MethodHandle *handle, DSValue &&recv,
                              std::pair<unsigned int, std::array<DSValue, 3>> &&args);

    /**
     *
     * @param funcObj
     * @param args
     * @return
     * return value of method (if no return value, return null).
     */
    static DSValue callFunction(DSState &state, DSValue &&funcObj, std::pair<unsigned int, std::array<DSValue, 3>> &&args);
};

// entry point of code evaluation
/**
 * entry point of toplevel code evaluation.
 * @param code
 * must be toplevel compiled code.
 * @param dsError
 * if not null, set error information
 * @return
 * exit status of latest executed command.
 */
inline int callToplevel(DSState &state, const CompiledCode &code, DSError *dsError) {
    return VM::callToplevel(state, code, dsError);
}

/**
 * execute command.
 * @param argv
 * DSValue must be String_Object
 * @param propagate
 * if true, not handle uncaught exception
 * @return
 * if exit status is 0, return true.
 * otherwise, return false
 */
inline DSValue execCommand(DSState &state, std::vector<DSValue> &&argv, bool propagate) {
    return VM::execCommand(state, std::move(argv), propagate);
}

/**
 * call method.
 * @param handle
 * must not be null
 * @param recv
 * @param args
 * @return
 * return value of method (if no return value, return null).
 */
inline DSValue callMethod(DSState &state, const MethodHandle *handle, DSValue &&recv,
                          std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    return VM::callMethod(state, handle, std::move(recv), std::move(args));
}

/**
 *
 * @param funcObj
 * @param args
 * @return
 * return value of method (if no return value, return null).
 */
inline DSValue callFunction(DSState &state, DSValue &&funcObj,
                            std::pair<unsigned int, std::array<DSValue, 3>> &&args) {
    return VM::callFunction(state, std::move(funcObj), std::move(args));
}

} // namespace ydsh

#endif //YDSH_VM_H
