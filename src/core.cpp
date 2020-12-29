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

#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>

#include <algorithm>
#include <cassert>

#include "vm.h"
#include "logger.h"
#include "misc/num_util.hpp"
#include "misc/files.h"

extern char **environ;  //NOLINT

namespace ydsh {

// ###########################
// ##     FilePathCache     ##
// ###########################

FilePathCache::~FilePathCache() {
    for(auto &pair : this->map) {
        free(const_cast<char *>(pair.first));
    }
}

const char *FilePathCache::searchPath(const char *cmdName, FilePathCache::SearchOp op) {
    // if found '/', return fileName
    if(strchr(cmdName, '/') != nullptr) {
        return cmdName;
    }

    // search cache
    if(!hasFlag(op, DIRECT_SEARCH)) {
        auto iter = this->map.find(cmdName);
        if(iter != this->map.end()) {
            return iter->second.c_str();
        }
    }

    // get PATH
    const char *pathPrefix = getenv(ENV_PATH);
    if(pathPrefix == nullptr || hasFlag(op, USE_DEFAULT_PATH)) {
        pathPrefix = VAL_DEFAULT_PATH;
    }

    // resolve path
    for(StringRef pathValue = pathPrefix; !pathValue.empty();) {
        StringRef remain;
        auto pos = pathValue.find(":");
        if(pos != StringRef::npos) {
            remain = pathValue.substr(pos + 1);
            pathValue = pathValue.slice(0, pos);
        }

        if(!pathValue.empty()) {
            auto resolvedPath = pathValue.toString();
            if(resolvedPath.back() != '/') {
                resolvedPath += '/';
            }
            resolvedPath += cmdName;
            expandTilde(resolvedPath);

            if((getStMode(resolvedPath.c_str()) & S_IXUSR) == S_IXUSR) {
                if(hasFlag(op, DIRECT_SEARCH)) {
                    this->prevPath = std::move(resolvedPath);
                    return this->prevPath.c_str();
                }
                // set to cache
                if(this->map.size() == MAX_CACHE_SIZE) {
                    free(const_cast<char *>(this->map.begin()->first));
                    this->map.erase(this->map.begin());
                }
                auto pair = this->map.emplace(strdup(cmdName), std::move(resolvedPath));
                assert(pair.second);
                return pair.first->second.c_str();
            }
        }
        pathValue = remain;
    }

    // not found
    return nullptr;
}

void FilePathCache::removePath(const char *cmdName) {
    if(cmdName != nullptr) {
        auto iter = this->map.find(cmdName);
        if(iter != this->map.end()) {
            free(const_cast<char *>(iter->first));
            this->map.erase(iter);
        }
    }
}

bool FilePathCache::isCached(const char *cmdName) const {
    return this->map.find(cmdName) != this->map.end();
}

void FilePathCache::clear() {
    for(auto &pair : this->map) {
        free(const_cast<char *>(pair.first));
    }
    this->map.clear();
}

struct StrArrayIter {
    ArrayObject::IterType actual;

    explicit StrArrayIter(ArrayObject::IterType actual) : actual(actual) {}

    auto operator*() const {
        return this->actual->asStrRef();
    }

    bool operator==(const StrArrayIter &o) const {
        return this->actual == o.actual;
    }

    bool operator!=(const StrArrayIter &o) const {
        return !(*this == o);
    }

    StrArrayIter &operator++() {
        ++this->actual;
        return *this;
    }
};

int GetOptState::operator()(const ArrayObject &obj, const char *optStr) {
    auto iter = StrArrayIter(obj.getValues().begin() + this->index);
    auto end = StrArrayIter(obj.getValues().end());
    int ret = opt::GetOptState::operator()(iter, end, optStr);
    this->index = iter.actual - obj.getValues().begin();
    return ret;
}

// core api definition
const DSValue &getGlobal(const DSState &st, const char *varName) {
    auto *handle = st.rootModScope->lookup(varName);
    assert(handle != nullptr);
    return st.getGlobal(handle->getIndex());
}

void raiseError(DSState &st, TYPE type, std::string &&message, int status) {
    auto except = ErrorObject::newError(st, st.typePool.get(type), DSValue::createStr(std::move(message)));
    st.throwObject(std::move(except), status);
}

void raiseSystemError(DSState &st, int errorNum, std::string &&message) {
    assert(errorNum != 0);
    if(errorNum == EINTR) {
        /**
         * if EINTR, already raised SIGINT. and SIGINT handler also raises SystemError.
         * due to eliminate redundant SystemError, force clear SIGINT
         */
        SignalGuard guard;
        DSState::pendingSigSet.del(SIGINT);
        if(DSState::pendingSigSet.empty()) {
            DSState::clearPendingSignal();
        }
    }
    std::string str(std::move(message));
    str += ": ";
    str += strerror(errorNum);
    raiseError(st, TYPE::SystemError, std::move(str));
}

CStrPtr getWorkingDir(const DSState &st, bool useLogical) {
    if(useLogical) {
        if(!S_ISDIR(getStMode(st.logicalWorkingDir.c_str()))) {
            return nullptr;
        }
        return CStrPtr(strdup(st.logicalWorkingDir.c_str()));
    }
    return getCWD();
}

bool changeWorkingDir(DSState &st, StringRef dest, const bool useLogical) {
    if(dest.hasNullChar()) {
        errno = EINVAL;
        return false;
    }

    const bool tryChdir = !dest.empty();
    const char *ptr = dest.data();
    std::string actualDest;
    if(tryChdir) {
        if(useLogical) {
            actualDest = expandDots(st.logicalWorkingDir.c_str(), ptr);
            ptr = actualDest.c_str();
        }
        if(chdir(ptr) != 0) {
            return false;
        }
    }

    // update OLDPWD
    const char *oldpwd = getenv(ENV_PWD);
    if(oldpwd == nullptr) {
        oldpwd = "";
    }
    setenv(ENV_OLDPWD, oldpwd, 1);

    // update PWD
    if(tryChdir) {
        if(useLogical) {
            setenv(ENV_PWD, actualDest.c_str(), 1);
            st.logicalWorkingDir = std::move(actualDest);
        } else {
            auto cwd = getCWD();
            if(cwd != nullptr) {
                setenv(ENV_PWD, cwd.get(), 1);
                st.logicalWorkingDir = cwd.get();
            }
        }
    }
    return true;
}

void installSignalHandler(DSState &st, int sigNum, const DSValue &handler) {
    SignalGuard guard;

    auto &DFL_handler = getGlobal(st, VAR_SIG_DFL);
    auto &IGN_handler = getGlobal(st, VAR_SIG_IGN);

    DSValue actualHandler;
    auto op = SignalVector::UnsafeSigOp::SET;
    if(sigNum == SIGBUS || sigNum == SIGSEGV || sigNum == SIGILL || sigNum == SIGFPE) {
        /**
         * not handle or ignore these signals due to prevent undefined behavior.
         * see. https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
         *      http://man7.org/linux/man-pages/man2/sigaction.2.html
         */
        return;
    } else if(handler == DFL_handler) {
        if(sigNum == SIGHUP) {
            actualHandler = handler;
        } else {
            op = SignalVector::UnsafeSigOp::DFL;
        }
    } else if(handler == IGN_handler) {
        op = SignalVector::UnsafeSigOp::IGN;
    } else {
        actualHandler = handler;
    }

    st.sigVector.install(sigNum, op, actualHandler);
}

DSValue getSignalHandler(const DSState &st, int sigNum) {
    auto &DFL_handler = getGlobal(st, VAR_SIG_DFL);
    auto &IGN_handler = getGlobal(st, VAR_SIG_IGN);

    auto handler = st.sigVector.lookup(sigNum);

    if(handler == nullptr) {
        struct sigaction action{};
        if(sigaction(sigNum, nullptr, &action) == 0) {
            if(action.sa_handler == SIG_IGN) {
                return IGN_handler;
            }
        }
        return DFL_handler;
    }
    return handler;
}

void setJobControlSignalSetting(DSState &st, bool set) {
    SignalGuard guard;

    auto op = set ? SignalVector::UnsafeSigOp::IGN : SignalVector::UnsafeSigOp::DFL;
    DSValue handler;

    if(set) {
        st.sigVector.install(SIGINT, SignalVector::UnsafeSigOp::SET, getGlobal(st, VAR_DEF_SIGINT));
    } else {
        st.sigVector.install(SIGINT, op, handler);
    }
    st.sigVector.install(SIGQUIT, op, handler);
    st.sigVector.install(SIGTSTP, op, handler);
    st.sigVector.install(SIGTTIN, op, handler);
    st.sigVector.install(SIGTTOU, op, handler);

    // due to prevent waitpid error (always wait child process termination)
    st.sigVector.install(SIGCHLD, SignalVector::UnsafeSigOp::DFL, handler, true);
}

/**
 * path must be full path
 */
static std::vector<std::string> createPathStack(const char *path) {
    std::vector<std::string> stack;
    if(*path == '/') {
        stack.emplace_back("/");
        path++;
    }

    for(const char *ptr; (ptr = strchr(path, '/')) != nullptr;) {
        const unsigned int size = ptr - path;
        if(size == 0) {
            path++;
            continue;
        }
        stack.emplace_back(path, size);
        path += size;
    }
    if(*path != '\0') {
        stack.emplace_back(path);
    }
    return stack;
}

std::string expandDots(const char *basePath, const char *path) {
    std::string str;

    if(path == nullptr || *path == '\0') {
        return str;
    }

    std::vector<std::string> resolvedPathStack;
    auto pathStack(createPathStack(path));

    // fill resolvedPathStack
    if(!pathStack.empty() && pathStack.front() != "/") {
        if(basePath != nullptr && *basePath == '/') {
            resolvedPathStack = createPathStack(basePath);
        } else {
            auto ptr = getCWD();
            if(!ptr) {
                return str;
            }
            resolvedPathStack = createPathStack(ptr.get());
        }
    }

    for(auto &e : pathStack) {
        if(e == "..") {
            if(!resolvedPathStack.empty()) {
                resolvedPathStack.pop_back();
            }
        } else if(e != ".") {
            resolvedPathStack.push_back(std::move(e));
        }
    }

    // create path
    const unsigned int size = resolvedPathStack.size();
    if(size == 1) {
        str += '/';
    }
    for(unsigned int i = 1; i < size; i++) {
        str += '/';
        str += resolvedPathStack[i];
    }
    return str;
}

void expandTilde(std::string &str, bool useHOME) {
    if(str.empty() || str.front() != '~') {
        return;
    }

    const char *path = str.c_str();
    std::string expanded;
    for(; *path != '/' && *path != '\0'; path++) {
        expanded += *path;
    }

    // expand tilde
    if(expanded.size() == 1) {
        const char *value = useHOME ? getenv(ENV_HOME) : nullptr;
        if(!value) {    // use HOME, but HOME is not set, fallback to getpwuid(getuid())
            struct passwd *pw = getpwuid(getuid());
            if(pw != nullptr) {
                value = pw->pw_dir;
            }
        }
        if(value) {
            expanded = value;
        }
    } else if(expanded == "~+") {
        /**
         * if PWD indicates valid dir, use PWD.
         * if PWD is invalid, use cwd
         * if cwd is removed, not expand
         */
        auto cwd = getCWD();
        if(cwd) {
            const char *pwd = getenv(ENV_PWD);
            if(pwd && *pwd == '/' && isSameFile(pwd, cwd.get())) {
                expanded = pwd;
            } else {
                expanded = cwd.get();
            }
        }
    } else if(expanded == "~-") {
        /**
         * if OLDPWD indicates valid dir, use OLDPWD
         * if OLDPWD is invalid, not expand
         */
         const char *oldpwd = getenv(ENV_OLDPWD);
         if(oldpwd && *oldpwd == '/' && S_ISDIR(getStMode(oldpwd))) {
             expanded = oldpwd;
         }
    } else {
        struct passwd *pw = getpwnam(expanded.c_str() + 1);
        if(pw != nullptr) {
            expanded = pw->pw_dir;
        }
    }

    // append rest
    if(*path != '\0') {
        expanded += path;
    }
    str = std::move(expanded);
}

static std::string toFullLocalModDirPath() {
    std::string dir = LOCAL_MOD_DIR;
    expandTilde(dir);
    return dir;
}

const char *getFullLocalModDir() {
    static auto path = toFullLocalModDirPath();
    return path.c_str();
}

// ####################
// ##     SigSet     ##
// ####################

int SigSet::popPendingSig() {
    assert(!this->empty());
    int sigNum;
    do {
        sigNum = this->pendingIndex++;
        if(this->pendingIndex == NSIG) {
            this->pendingIndex = 1;
        }
    } while(!this->has(sigNum));
    this->del(sigNum);
    return sigNum;
}


// ##########################
// ##     SignalVector     ##
// ##########################

struct SigEntryComp {
    using Entry = std::pair<int, DSValue>;

    bool operator()(const Entry &x, int y) const {
        return x.first < y;
    }

    bool operator()(int x, const Entry &y) const {
        return x < y.first;
    }
};

void SignalVector::insertOrUpdate(int sigNum, const DSValue &func) {
    auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
    if(iter != this->data.end() && iter->first == sigNum) {
        if(func) {
            iter->second = func;    // update
        } else {
            this->data.erase(iter); // remove
        }
    } else if(func) {
        this->data.insert(iter, std::make_pair(sigNum, func));  // insert
    }
}

DSValue SignalVector::lookup(int sigNum) const {
    auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
    if(iter != this->data.end() && iter->first == sigNum) {
        return iter->second;
    }
    return nullptr;
}

static void signalHandler(int sigNum) { // when called this handler, all signals are blocked due to signal mask
    DSState::pendingSigSet.add(sigNum);
    setFlag(DSState::eventDesc, VMEvent::SIGNAL);
}

void SignalVector::install(int sigNum, UnsafeSigOp op, const DSValue &handler, bool setSIGCHLD) {
    if(sigNum == SIGCHLD && !setSIGCHLD) {
        return;
    }

    // set posix signal handler
    struct sigaction action{};
    if(sigNum != SIGINT) {  // always restart system call except for SIGINT
        action.sa_flags = SA_RESTART;
    }
    sigfillset(&action.sa_mask);

    switch(op) {
    case UnsafeSigOp::DFL:
        action.sa_handler = SIG_DFL;
        break;
    case UnsafeSigOp::IGN:
        action.sa_handler = SIG_IGN;
        break;
    case UnsafeSigOp::SET:
        action.sa_handler = signalHandler;
        break;
    }
    sigaction(sigNum, &action, nullptr);

    // register handler
    if(sigNum != SIGCHLD) {
        this->insertOrUpdate(sigNum, handler);
    }
}

void SignalVector::clear() {
    for(auto &e : this->data) {
        struct sigaction action{};
        action.sa_handler = SIG_DFL;
        sigaction(e.first, &action, nullptr);
    }
    this->data.clear();
}

int xexecve(const char *filePath, char *const *argv, char *const *envp) {
    if(filePath == nullptr) {
        errno = ENOENT;
        return -1;
    }

    // set env
    setenv("_", filePath, 1);
    if(envp == nullptr) {
        envp = environ;
    }

    LOG_EXPR(DUMP_EXEC, [&]{
        std::string str = filePath;
        str += ", [";
        for(unsigned int i = 0; argv[i] != nullptr; i++) {
            if(i > 0) {
                str += ", ";
            }
            str += argv[i];
        }
        str += "]";
        return str;
    });

    // execute external command
    int ret = execve(filePath, argv, envp);
    if(errno == ENOEXEC) {  // fallback to /bin/sh
        unsigned int size = 0;
        for(; argv[size]; size++);
        size++;
        char *newArgv[size + 1];
        newArgv[0] = const_cast<char *>("/bin/sh");
        memcpy(newArgv + 1, argv, sizeof(char *) * size);
        return execve(newArgv[0], newArgv, envp);
    }
    return ret;
}

} // namespace ydsh

#ifdef CODE_COVERAGE
extern "C" void __gcov_flush(); // for coverage reporting
#endif

namespace ydsh {

void terminate(int exitStatus) {
#ifdef CODE_COVERAGE
    /*
         * after call _exit(), not write coverage information due to skip atexit handler.
         * in order to write coverage information, manually call __gcove_flush()
         */
        __gcov_flush();
#endif
    _exit(exitStatus);
}

} // namespace ydsh