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

#ifndef YDSH_CORE_H
#define YDSH_CORE_H

#include <unistd.h>

#include <csignal>
#include <string>
#include <vector>
#include <array>

#include "opcode.h"
#include "object.h"
#include "misc/hash.hpp"
#include "misc/buffer.hpp"
#include "misc/flag_util.hpp"
#include "misc/opt.hpp"
#include "misc/resource.hpp"

struct DSState;

namespace ydsh {

/**
 * enum order is corresponding to builtin variable declaration order.
 */
enum class BuiltinVarOffset : unsigned int {
    SCRIPT_NAME,    // SCRIPT_NAME
    SCRIPT_DIR,     // SCRIPT_DIR
    REPLY,          // REPLY (for read command)
    REPLY_VAR,      // reply (fo read command)
    PID,            // PID (current process)
    PPID,           // PPID
    SECONDS,        // SECONDS
    IFS,            // IFS
    COMPREPLY,      // COMPREPLY
    PIPESTATUS,     // PIPESTATUS
    EXIT_STATUS,    // ?
    SHELL_PID,      // $
    ARGS,           // @
    ARGS_SIZE,      // #
    POS_0,          // 0 (for script name)
    POS_1,          // 1 (for argument)
    /*POS_2, POS_3, POS_4, POS_5, POS_6, POS_7, POS_8, POS_9, */
};

inline unsigned int toIndex(BuiltinVarOffset offset) {
    return static_cast<unsigned int>(offset);
}

class FilePathCache {
private:
    /**
     * contains previously resolved path (for direct search)
     */
    std::string prevPath;

    CStringHashMap<std::string> map;

    static constexpr unsigned int MAX_CACHE_SIZE = 100;

public:
    NON_COPYABLE(FilePathCache);

    FilePathCache() = default;

    ~FilePathCache();

    enum SearchOp {
        NON              = 0u,
        USE_DEFAULT_PATH = 1u << 0u,
        DIRECT_SEARCH    = 1u << 1u,
    };

    /**
     * search file path by using PATH
     * if cannot resolve path (file not found), return null.
     */
    const char *searchPath(const char *cmdName, SearchOp op = NON);

    void removePath(const char *cmdName);

    bool isCached(const char *cmdName) const;

    /**
     * clear all cache
     */
    void clear();

    /**
     * get begin iterator of map
     */
    auto begin() const {
        return this->map.cbegin();
    }

    /**
     * get end iterator of map
     */
    auto end() const {
        return this->map.cend();
    }
};

template <> struct allow_enum_bitop<FilePathCache::SearchOp> : std::true_type {};

struct GetOptState : public opt::GetOptState {
    /**
     * index of next processing argument
     */
    unsigned int index;

    explicit GetOptState(unsigned int index= 1) : index(index) {}

    int operator()(const ArrayObject &obj, const char *optStr);
};

struct VMHook {
    /**
     * hook for vm fetch event
     * @param st
     * @param op
     * fetched opcode
     */
    virtual void vmFetchHook(DSState &st, OpCode op) = 0;

    /**
     * hook for exception handle event
     * @param st
     */
    virtual void vmThrowHook(DSState &st) = 0;
};

struct DumpTarget {
    FilePtr files[3];
};

const DSValue &getBuiltinGlobal(const DSState &st, const char *varName);

/**
 * raise Error Object and update exit status
 * @param st
 * @param type
 * @param message
 * @param status
 */
void raiseError(DSState &st, TYPE type, std::string &&message, int status = 1);

void raiseSystemError(DSState &st, int errorNum, std::string &&message);

/**
 *
 * @param st
 * @param useLogical
 * @return
 * if has error, return null and set errno.
 */
CStrPtr getWorkingDir(const DSState &st, bool useLogical);

/**
 * change current working directory and update OLDPWD, PWD.
 * if dest is null, do nothing and return true.
 */
bool changeWorkingDir(DSState &st, StringRef dest, bool useLogical);

/**
 *
 * @param st
 * @param sigNum
 * @param handler
 * must be FuncObject
 */
void installSignalHandler(DSState &st, int sigNum, const DSValue &handler);

/**
 *
 * @param st
 * @param sigNum
 * @return
 * if sigNum is invalid (ex. pseudo-signal), return SIG_DFL object
 */
DSValue getSignalHandler(const DSState &st, int sigNum);

/**
 * if set is true, ignore some signals.
 * if set is false, reset some signal setting.
 * @param set
 */
void setJobControlSignalSetting(DSState &st, bool set);

/**
 * expand dot '.' '..'
 * @param basePath
 * may be null. must be full path.
 * @param path
 * may be null
 * @return
 * if expansion failed, return empty string
 */
std::string expandDots(const char *basePath, const char *path);

/**
 *
 * @param str
 * @param useHOME
 * if true, use `HOME' environmental variable for `~' expansion
 */
void expandTilde(std::string &str, bool useHOME = false);

/**
 * get full path of LOCAL_MOD_DIR
 * @return
 */
const char *getFullLocalModDir();

class ModuleLoader;

/**
 *
 * @param pool
 * @param loader
 * @param code
 * may be null
 * @return
 * if cannot resolve underlying module type (if underlying module is not sealed (root module)),
 * return null
 */
const ModType *getUnderlyingModType(const TypePool &pool,
                                    const ModuleLoader &loader, const CompiledCode *code);

const ModType *getRuntimeModuleByLevel(const DSState &state, unsigned int callLevel);

inline const ModType *getCurRuntimeModule(const DSState &state) {
    return getRuntimeModuleByLevel(state, 0);
}

class SignalGuard {
private:
    sigset_t maskset;

public:
    SignalGuard() {
        sigfillset(&this->maskset);
        sigprocmask(SIG_BLOCK, &this->maskset, nullptr);
    }

    ~SignalGuard() {
        int e = errno;
        sigprocmask(SIG_UNBLOCK, &this->maskset, nullptr);
        errno = e;
    }
};

class SigSet {
private:
    static_assert(NSIG - 1 <= sizeof(uint64_t) * 8, "huge signal number");

    uint64_t value{0};

    int pendingIndex{1};

public:
    void add(int sigNum) {
        uint64_t f = static_cast<uint64_t>(1) << static_cast<unsigned int>(sigNum - 1);
        setFlag(this->value, f);
    }

    void del(int sigNum) {
        uint64_t f = static_cast<uint64_t>(1) << static_cast<unsigned int>(sigNum - 1);
        unsetFlag(this->value, f);
    }

    bool has(int sigNum) const {
        uint64_t f = static_cast<uint64_t>(1) << static_cast<unsigned int>(sigNum - 1);
        return hasFlag(this->value, f);
    }

    bool empty() const {
        return this->value == 0;
    }

    void clear() {
        this->value = 0;
        this->pendingIndex = 1;
    }

    int popPendingSig();
};

class SignalVector {
private:
    /**
     * pair.second must be FuncObject
     */
    std::vector<std::pair<int, DSValue>> data;

public:
    SignalVector() = default;
    ~SignalVector() = default;

    /**
     * if func is null, delete handler.
     * @param sigNum
     * @param value
     * must be FuncObject
     * may be null
     */
    void insertOrUpdate(int sigNum, const DSValue &value);

    /**
     *
     * @param sigNum
     * @return
     * if not found, return null obj.
     */
    DSValue lookup(int sigNum) const;

    const std::vector<std::pair<int, DSValue>> &getData() const {
        return this->data;
    };


    enum class UnsafeSigOp {
        DFL,
        IGN,
        SET,
    };

    /**
     * unsafe op.
     * @param sigNum
     * @param op
     * @param handler
     * may be nullptr
     */
    void install(int sigNum, UnsafeSigOp op, const DSValue &handler);

    /**
     * clear all handler and set to SIG_DFL.
     */
    void clear();
};

template <typename ...T>
inline std::pair<unsigned int, std::array<DSValue, 3>> makeArgs(T&& ... arg) {
    static_assert(sizeof...(arg) <= 3, "too long");
    return std::make_pair(sizeof...(arg), std::array<DSValue, 3>{{ std::forward<T>(arg)...}});
}

/**
 *
 * @param filePath
 * if null, not execute and set ENOENT.
 * @param argv
 * not null
 * @param envp
 * may be null
 * @return
 * if success, not return.
 */
int xexecve(const char *filePath, char *const *argv, char *const *envp);

/**
 * wrap '_exit'
 * when coverage build, call '__gcov_flush()' before _exit
 * @param exitStatus
 */
[[noreturn]] void terminate(int exitStatus);

} // namespace ydsh

#endif //YDSH_CORE_H
