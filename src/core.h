/*
 * Copyright (C) 2016-2017 Nagisa Sekiguchi
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

#include "opcode.h"
#include "misc/hash.hpp"
#include "misc/buffer.hpp"
#include "misc/flag_util.hpp"

struct DSState;

namespace ydsh {

class DSType;
class DSValue;
class Array_Object;
class StackTraceElement;
class FuncObject;
class SymbolTable;
class DSCode;
class JobTable;

using CStrBuffer = FlexBuffer<char *>;

/**
 * enum order is corresponding to builtin variable declaration order.
 */
enum class BuiltinVarOffset : unsigned int {
    DBUS,           // DBus
    VERSION,        // YDSH_VERSION (equivalent to ps_intrp '\V')
    REPLY,          // REPLY (for read command)
    REPLY_VAR,      // reply (fo read command)
    PID,            // PID (current process)
    PPID,           // PPID
    SECONDS,        // SECONDS
    IFS,            // IFS
    HIST_CMD,       // HISTCMD
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
     * contains previously resolved path (for directive search)
     */
    std::string prevPath;

    CStringHashMap<std::string> map;

    static constexpr unsigned int MAX_CACHE_SIZE = 100;

public:
    FilePathCache() = default;

    ~FilePathCache();

    static constexpr flag8_t USE_DEFAULT_PATH = 1 << 0;
    static constexpr flag8_t DIRECT_SEARCH    = 1 << 1;

    /**
     * search file path by using PATH
     * if cannot resolve path (file not found), return null.
     */
    const char *searchPath(const char *cmdName, flag8_set_t option = 0);

    void removePath(const char *cmdName);

    bool isCached(const char *cmdName) const;

    /**
     * clear all cache
     */
    void clear();

    /**
     * get begin iterator of map
     */
    CStringHashMap<std::string>::const_iterator begin() const {
        return this->map.cbegin();
    }

    /**
     * get end iterator of map
     */
    CStringHashMap<std::string>::const_iterator end() const {
        return this->map.cend();
    }
};

struct GetOptState {
    /**
     * index of next processing argument
     */
    unsigned int index{1};

    /**
     * currently processed argument.
     */
    const char *optCursor{nullptr};

    /**
     * may be null, if has no optional argument.
     */
    const char *optArg{nullptr};

    /**
     * unrecognized option.
     */
    int optOpt{0};

    int operator()(const Array_Object &obj, const char *optStr);
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
    FILE *fps[3]{nullptr};

    ~DumpTarget() {
        for(auto &fp : this->fps) {
            if(fp != nullptr) {
                fclose(fp);
            }
        }
    }
};

// core api

// getter api

SymbolTable &getPool(DSState &st);
const SymbolTable &getPool(const DSState &st);

JobTable &getJobTable(DSState &st);

FilePathCache &getPathCache(DSState &st);

const DSValue &getTrueObj(const DSState &st);
const DSValue &getFalseObj(const DSState &st);
const DSValue &getEmptyStrObj(const DSState &st);

void setLocal(DSState &st, unsigned char index, const DSValue &obj);

void setLocal(DSState &st, unsigned char index, DSValue &&obj);

const DSValue &getLocal(const DSState &st, unsigned char index);

DSValue extractLocal(DSState &st, unsigned char index);

void setGlobal(DSState &st, unsigned int index, const DSValue &obj);

void setGlobal(DSState &st, unsigned int index, DSValue &&obj);

const DSValue &getGlobal(const DSState &st, unsigned int index);

const DSValue &getGlobal(const DSState &st, const char *varName);

/**
 *
 * @param st
 * @return
 * offset + 0 EXIT_HOOK
 * offset + 1 ERR_HOOK
 * offset + 2 ASSERT_HOOK
 */
unsigned int getTermHookIndex(DSState &st);

bool hasError(const DSState &st);

void raiseError(DSState &st, DSType &errorType, std::string &&message);

void raiseSystemError(DSState &st, int errorNum, std::string &&message);

[[noreturn]]
void throwError(DSState &st, DSType &errorType, std::string &&message);

void fillInStackTrace(const DSState &st, std::vector<StackTraceElement> &stackTrace);

const char *getConfigRootDir();

const char *getIfaceDir();

/**
 *
 * @param st
 * @param useLogical
 * @param buf
 * @return
 * if has error, return null and set errno.
 */
const char *getWorkingDir(const DSState &st, bool useLogical, std::string &buf);

/**
 * change current working directory and update OLDPWD, PWD.
 * if dest is null, do nothing and return true.
 */
bool changeWorkingDir(DSState &st, const char *dest, bool useLogical);

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
 * n is 1 or 2
 */
std::string interpretPromptString(const DSState &st, const char *ps);

std::string expandDots(const char *basePath, const char *path);

void expandTilde(std::string &str);

/**
 * return completion candidates.
 * line must be terminate newline.
 */
CStrBuffer completeLine(const DSState &st, const std::string &line);

class SignalGuard {
private:
    sigset_t maskset;

public:
    SignalGuard() {
        sigfillset(&this->maskset);
        sigprocmask(SIG_BLOCK, &this->maskset, nullptr);
    }

    ~SignalGuard() {
        sigprocmask(SIG_UNBLOCK, &this->maskset, nullptr);
    }
};

} // namespace ydsh

#endif //YDSH_CORE_H
