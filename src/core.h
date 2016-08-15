/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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

#include <string>
#include <vector>

#include "opcode.h"
#include "misc/hash.hpp"
#include "misc/buffer.hpp"

struct DSState;

namespace ydsh {

class TypePool;
class DSType;
class DSValue;
class StackTraceElement;
class FuncObject;

using CStrBuffer = FlexBuffer<char *>;

/**
 * enum order is corresponding to builtin variable declaration order.
 */
enum class BuiltinVarOffset : unsigned int {
    DBUS,           // DBus
    OSTYPE,         // OSTYPE (utsname.sysname)
    VERSION,        // YDSH_VERSION (equivalent to ps_intrp '\V')
    REPLY,          // REPLY (for read command)
    REPLY_VAR,      // reply (fo read command)
    PID,            // PID (current process)
    PPID,           // PPID
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

    static constexpr unsigned char USE_DEFAULT_PATH = 1 << 0;
    static constexpr unsigned char DIRECT_SEARCH    = 1 << 1;

    /**
     * search file path by using PATH
     * if cannot resolve path (file not found), return null.
     */
    const char *searchPath(const char *cmdName, unsigned char option = 0);

    void removePath(const char *cmdName);

    bool isCached(const char *cmdName) const;

    /**
     * clear all cache
     */
    void clear();

    /**
     * get begin iterator of map
     */
    CStringHashMap<std::string>::const_iterator cbegin() const {
        return this->map.cbegin();
    }

    /**
     * get end iterator of map
     */
    CStringHashMap<std::string>::const_iterator cend() const {
        return this->map.cend();
    }
};

struct DebugHook {
    virtual void vmFetchHook(DSState &st, OpCode op) = 0;
};

// core api

// getter api

TypePool &getPool(DSState &st);

FilePathCache &getPathCache(DSState &st);

const DSValue &getTrueObj(const DSState &st);
const DSValue &getFalseObj(const DSState &st);
const DSValue &getEmptyStrObj(const DSState &st);

const char *getIFS(DSState &st);

void setLocal(DSState &st, unsigned int index, const DSValue &obj);

void setLocal(DSState &st, unsigned int index, DSValue &&obj);

const DSValue &getLocal(const DSState &st, unsigned int index);

void setGlobal(DSState &st, unsigned int index, const DSValue &obj);

void setGlobal(DSState &st, unsigned int index, DSValue &&obj);

const DSValue &getGlobal(const DSState &st, unsigned int index);

inline const DSValue &getDBus(const DSState &st) {
    return getGlobal(st, toIndex(BuiltinVarOffset::DBUS));
}

void checkedPush(DSState &st, const DSValue &v);    //TODO: future may be removed
void checkedPush(DSState &st, DSValue &&v);         //TODO: future may be removed

void throwError(DSState &st, DSType &errorType, const char *message);

void throwError(DSState &st, DSType &errorType, std::string &&message);

void fillInStackTrace(const DSState &st, std::vector<StackTraceElement> &stackTrace);

std::string getConfigRootDir();

std::string getIfaceDir();

const char *getLogicalWorkingDir(const DSState &st);

/**
 * change current working directory and update OLDPWD, PWD.
 * if dest is null, do nothing and return true.
 */
bool changeWorkingDir(DSState &st, const char *dest, const bool useLogical);

void exitShell(DSState &st, unsigned int status);

/**
 * after fork, reset signal setting in child process.
 */
pid_t xfork(DSState &st);

/**
 * waitpid wrapper.
 */
pid_t xwaitpid(DSState &st, pid_t pid, int &status, int options);

/**
 * n is 1 or 2
 */
void interpretPromptString(const DSState &st, const char *ps, std::string &output);

/**
 * if not found, return null
 */
FuncObject *lookupUserDefinedCommand(const DSState &st, const char *commandName);

/**
 * obj must indicate user-defined command.
 */
void callUserDefinedCommand(DSState &st, const FuncObject *obj, DSValue *argv);

std::string expandDots(const char *basePath, const char *path);

/**
 * path is starts with tilde.
 */
std::string expandTilde(const char *path);

/**
 * return completion candidates.
 * line must be terminate newline.
 */
CStrBuffer completeLine(const DSState &st, const std::string &line);

} // namespace ydsh

#endif //YDSH_CORE_H
