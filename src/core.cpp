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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <dirent.h>

#include <algorithm>
#include <cassert>

#include <config.h>
#include "vm.h"
#include "symbol.h"
#include "parser.h"
#include "logger.h"
#include "cmd.h"
#include "misc/num.h"
#include "misc/time.h"
#include "misc/files.h"

namespace ydsh {

// ###########################
// ##     FilePathCache     ##
// ###########################

FilePathCache::~FilePathCache() {
    for(auto &pair : this->map) {
        free(const_cast<char *>(pair.first));
    }
}

const char *FilePathCache::searchPath(const char *cmdName, flag8_t option) {
    // if found '/', return fileName
    if(strchr(cmdName, '/') != nullptr) {
        return cmdName;
    }

    // search cache
    if(!hasFlag(option, DIRECT_SEARCH)) {
        auto iter = this->map.find(cmdName);
        if(iter != this->map.end()) {
            return iter->second.c_str();
        }
    }

    // get PATH
    const char *pathPrefix = getenv(ENV_PATH);
    if(pathPrefix == nullptr || hasFlag(option, USE_DEFAULT_PATH)) {
        pathPrefix = VAL_DEFAULT_PATH;
    }

    // resolve path
    std::string resolvedPath;
    for(unsigned int i = 0; !resolvedPath.empty() || pathPrefix[i] != '\0'; i++) {
        char ch = pathPrefix[i];
        bool stop = false;

        if(ch == '\0') {
            stop = true;
        } else if(ch != ':') {
            resolvedPath += ch;
            continue;
        }
        if(resolvedPath.empty()) {
            continue;
        }

        if(resolvedPath.back() != '/') {
            resolvedPath += '/';
        }
        resolvedPath += cmdName;
        expandTilde(resolvedPath);

        struct stat st;
        if(stat(resolvedPath.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) == S_IXUSR) {
            if(hasFlag(option, DIRECT_SEARCH)) {
                this->prevPath = std::move(resolvedPath);
                return this->prevPath.c_str();
            }
            // set to cache
            if(this->map.size() == MAX_CACHE_SIZE) {
                free(const_cast<char *>(this->map.begin()->first));
                this->map.erase(this->map.begin());
            }
            auto pair = this->map.insert(std::make_pair(strdup(cmdName), std::move(resolvedPath)));
            assert(pair.second);
            return pair.first->second.c_str();
        }
        resolvedPath.clear();

        if(stop) {
            break;
        }
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

/**
 *
 * @param obj
 * first element is command name.
 * @param optStr
 * @return
 */
int GetOptState::operator()(const Array_Object &obj, const char *optStr) {
    unsigned int argc = obj.getValues().size();
    if(this->index < argc) {
        const char *arg = str(obj.getValues()[this->index]);
        if(*arg != '-' || strcmp(arg, "-") == 0) {
            return -1;
        }

        if(strcmp(arg, "--") == 0) {
            this->index++;
            return -1;
        }

        if(this->optCursor == nullptr || *this->optCursor == '\0') {
            this->optCursor = arg + 1;
        }

        const char *ptr = strchr(optStr, *this->optCursor);
        if(ptr != nullptr) {
            if(*(ptr + 1) == ':') {
                if(++this->index == argc) {
                    this->optOpt = *ptr;
                    return ':';
                }
                this->optArg = str(obj.getValues()[this->index]);
                this->optCursor = nullptr;
            }

            if(this->optCursor == nullptr || *(++this->optCursor) == '\0') {
                this->index++;
            }
            return *ptr;
        }
        this->optOpt = *this->optCursor;
        return '?';
    }
    return -1;
}

// core api definition

TypePool &getPool(DSState &st) {
    return st.pool;
}

const TypePool &getPool(const DSState &st) {
    return st.pool;
}

FilePathCache &getPathCache(DSState &st) {
    return st.pathCache;
}

const DSValue &getTrueObj(const DSState &st) {
    return st.trueObj;
}

const DSValue &getFalseObj(const DSState &st) {
    return st.falseObj;
}

const DSValue &getEmptyStrObj(const DSState &st) {
    return st.emptyStrObj;
}

void setLocal(DSState &st, unsigned char index, const DSValue &obj) {
    st.setLocal(index, obj);
}

void setLocal(DSState &st, unsigned char index, DSValue &&obj) {
    st.setLocal(index, std::move(obj));
}

const DSValue &getLocal(const DSState &st, unsigned char index) {
    return st.getLocal(index);
}

DSValue extractLocal(DSState &st, unsigned char index) {
    return st.moveLocal(index);
}

void setGlobal(DSState &st, unsigned int index, const DSValue &obj) {
    st.setGlobal(index, obj);
}

void setGlobal(DSState &st, unsigned int index, DSValue &&obj) {
    st.setGlobal(index, std::move(obj));
}

const DSValue &getGlobal(const DSState &st, unsigned int index) {
    return st.getGlobal(index);
}

void throwError(DSState &st, DSType &errorType, std::string &&message) {
    st.throwException(st.newError(errorType, std::move(message)));
}

/**
 * convert errno to SystemError.
 * errorNum must not be 0.
 * format message '%s: %s', message, strerror(errorNum)
 */
void throwSystemError(DSState &st, int errorNum, std::string &&message) {
    if(errorNum == 0) {
        fatal("errno is not 0\n");
    }

    std::string str(std::move(message));
    str += ": ";
    str += strerror(errorNum);
    throwError(st, st.pool.getSystemErrorType(), std::move(str));
}

void fillInStackTrace(const DSState &st, std::vector<StackTraceElement> &stackTrace) {
    unsigned int callableDepth = st.codeStack.size();

    unsigned int curPC = st.pc_;
    unsigned int curBottomIndex = st.stackBottomIndex;

    while(callableDepth != 0u) {
        auto &callable = st.codeStack[--callableDepth];
        if(!callable->is(CodeKind::NATIVE)) {
            const auto *cc = static_cast<const CompiledCode *>(callable);

            // create stack trace element
            const char *sourceName = cc->getSrcInfo()->getSourceName().c_str();
            unsigned int pos = getSourcePos(cc->getSourcePosEntries(), curPC);

            std::string callableName;
            switch(callable->getKind()) {
            case CodeKind::TOPLEVEL:
                callableName += "<toplevel>";
                break;
            case CodeKind::FUNCTION:
                callableName += "function ";
                callableName += cc->getName();
                break;
            case CodeKind::USER_DEFINED_CMD:
                callableName += "command ";
                callableName += cc->getName();
                break;
            default:
                break;
            }

            stackTrace.emplace_back(sourceName, cc->getSrcInfo()->getLineNum(pos), std::move(callableName));
        }

        // unwind state
        if(callableDepth != 0u) {
            const unsigned int offset = curBottomIndex;
            curPC = static_cast<unsigned int>(st.callStack[offset - 3].value());
            curBottomIndex = static_cast<unsigned int>(st.callStack[offset - 1].value());
        }
    }
}

static std::string initConfigRootDir() {
#ifdef X_CONFIG_DIR
    return std::string(X_CONFIG_DIR);
#else
    std::string path(getenv(ENV_HOME));
    path += "/.ydsh";
    return path;
#endif
}

const char *getConfigRootDir() {
    static std::string dir(initConfigRootDir());
    return dir.c_str();
}

static std::string initIfaceDir() {
    std::string root(getConfigRootDir());
    root += "/dbus/iface";
    return root;
}

const char *getIfaceDir() {
    static std::string dir(initIfaceDir());
    return dir.c_str();
}

const char *getLogicalWorkingDir(const DSState &st) {
    return st.logicalWorkingDir.c_str();
}

bool changeWorkingDir(DSState &st, const char *dest, const bool useLogical) {
    if(dest == nullptr) {
        return true;
    }

    const bool tryChdir = strlen(dest) != 0;
    std::string actualDest;
    if(tryChdir) {
        if(useLogical) {
            actualDest = expandDots(st.logicalWorkingDir.c_str(), dest);
            dest = actualDest.c_str();
        }
        if(chdir(dest) != 0) {
            return false;
        }
    }

    // update OLDPWD
    const char *oldpwd = getenv(ENV_PWD);
    assert(oldpwd != nullptr);
    setenv(ENV_OLDPWD, oldpwd, 1);

    // update PWD
    if(tryChdir) {
        if(useLogical) {
            setenv(ENV_PWD, actualDest.c_str(), 1);
            st.logicalWorkingDir = std::move(actualDest);
        } else {
            size_t size = PATH_MAX;
            char buf[size];
            const char *cwd = getcwd(buf, size);
            if(cwd != nullptr) {
                setenv(ENV_PWD, cwd, 1);
            }
            st.logicalWorkingDir = cwd;
        }
    }
    return true;
}

void exitShell(DSState &st, unsigned int status) {
    std::string str("terminated by exit ");
    str += std::to_string(status);
    auto except = st.newError(st.pool.getShellExit(), std::move(str));

    // invoke termination hook
    if(st.terminationHook != nullptr) {
        const unsigned int lineNum = getOccurredLineNum(typeAs<Error_Object>(except)->getStackTrace());
        st.terminationHook(DS_ERROR_KIND_EXIT, lineNum);
    }

    // print stack trace
    if(hasFlag(st.option, DS_OPTION_TRACE_EXIT)) {
        typeAs<Error_Object>(except)->printStackTrace(st);
    }
    exit(status);
}

pid_t xfork(DSState &st, pid_t pgid, bool foreground) {
    pid_t pid = fork();
    if(pid == 0) {  // child process
        if(st.isInteractive()) {
            setpgid(0, pgid);
            if(foreground) {
                tcsetpgrp(STDIN_FILENO, getpgid(0));
            }

            struct sigaction act;
            act.sa_handler = SIG_DFL;
            act.sa_flags = 0;
            sigemptyset(&act.sa_mask);

            /**
             * reset signal behavior
             */
            sigaction(SIGINT, &act, nullptr);
            sigaction(SIGQUIT, &act, nullptr);
            sigaction(SIGTSTP, &act, nullptr);
            sigaction(SIGTTIN, &act, nullptr);
            sigaction(SIGTTOU, &act, nullptr);
            sigaction(SIGCHLD, &act, nullptr);
        }

        // update PID, PPID
        st.setGlobal(toIndex(BuiltinVarOffset::PID), DSValue::create<Int_Object>(st.pool.getUint32Type(), getpid()));
        st.setGlobal(toIndex(BuiltinVarOffset::PPID), DSValue::create<Int_Object>(st.pool.getUint32Type(), getppid()));
    } else if(pid > 0) {
        if(st.isInteractive()) {
            setpgid(pid, pgid);
            if(foreground) {
                tcsetpgrp(STDIN_FILENO, getpgid(pid));
            }
        }
    }
    return pid;
}

/**
 * waitpid wrapper.
 */
pid_t xwaitpid(DSState &, pid_t pid, int &status, int options) {
    pid_t ret = waitpid(pid, &status, options);
    if(WIFSIGNALED(status)) {
//        fputc('\n', stdout);
    }
    return ret;
}

static const char *safeBasename(const char *str) {
    const char *ptr = strrchr(str, '/');
    return ptr == nullptr ? str : ptr + 1;
}

std::string interpretPromptString(const DSState &st, const char *ps) {
    std::string output;

    struct tm *local = getLocalTime();

    constexpr unsigned int hostNameSize = 128;    // in linux environment, HOST_NAME_MAX is 64
    char hostName[hostNameSize];
    if(gethostname(hostName, hostNameSize) !=  0) {
        hostName[0] = '\0';
    }

    for(unsigned int i = 0; ps[i] != '\0'; i++) {
        char ch = ps[i];
        if(ch == '\\' && ps[i + 1] != '\0') {
            switch(ps[++i]) {
            case 'a':
                ch = '\a';
                break;
            case 'd': {
                const char *wdays[] = {
                        "Sun", "Mon", "Tue", "Wed", "Thurs", "Fri", "Sat"
                };

                char str[16];
                strftime(str, arraySize(str), " %m %d", local);
                output += wdays[local->tm_wday];
                output += str;
                continue;
            }
            case 'e':
                ch = '\033';
                break;
            case 'h': {
                for(unsigned int j = 0; hostName[j] != '\0' && hostName[j] != '.'; j++) {
                    output += hostName[j];
                }
                continue;
            }
            case 'H':
                output += hostName;
                continue;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 's': {
                auto &v = st.getGlobal(toIndex(BuiltinVarOffset::POS_0));    // script name
                output += safeBasename(typeAs<String_Object>(v)->getValue());
                continue;
            }
            case 't': {
                char str[16];
                strftime(str, arraySize(str), "%T", local);
                output += str;
                continue;
            }
            case 'T': {
                char str[16];
                strftime(str, arraySize(str), "%I:%M:%S", local);
                output += str;
                continue;
            }
            case '@': {
                char str[16];
                strftime(str, arraySize(str), "%I:%M ", local);
                output += str;
                output += local->tm_hour < 12 ? "AM" : "PM";
                continue;
            }
            case 'u': {
                struct passwd *pw = getpwuid(geteuid());
                if(pw != nullptr) {
                    output += pw->pw_name;
                }
                continue;
            }
            case 'v': {
#define XSTR(v) #v
#define STR(v) XSTR(v)
                output += STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION);
                continue;
#undef XSTR
#undef STR
            }
            case 'V': {
                const unsigned int index = toIndex(BuiltinVarOffset::VERSION);
                const char *str = typeAs<String_Object>(st.getGlobal(index))->getValue();
                output += str;
                continue;
            }
            case 'w': {
                const char *c = getenv(ENV_PWD);
                if(c != nullptr) {
                    const char *home = getenv(ENV_HOME);
                    if(home != nullptr && strstr(c, home) == c) {
                        output += '~';
                        c += strlen(home);
                    }
                    output += c;
                }
                continue;
            }
            case 'W':  {
                const char *cwd = getenv(ENV_PWD);
                if(cwd == nullptr) {
                    continue;
                }
                if(strcmp(cwd, "/") == 0) {
                    output += cwd;
                } else {
                    const char *home = getenv(ENV_HOME);
                    char *realHOME = home != nullptr ? realpath(home, nullptr) : nullptr;
                    char *realPWD = realpath(cwd, nullptr);

                    if(realHOME != nullptr && strcmp(realHOME, realPWD) == 0) {
                        output += "~";
                    } else {
                        output += safeBasename(cwd);
                    }

                    free(realHOME);
                    free(realPWD);
                }
                continue;
            }
            case '$':
                ch = (getuid() == 0 ? '#' : '$');
                break;
            case '\\':
                ch = '\\';
                break;
            case '[':
            case ']':
                continue;
            case '0': {
                int v = 0;
                for(unsigned int c = 0; c < 3; c++) {
                    if(isOctal(ps[i + 1])) {
                        v *= 8;
                        v += ps[++i] - '0';
                    } else {
                        break;
                    }
                }
                ch = (char) v;
                break;
            }
            case 'x': {
                if(isHex(ps[i + 1])) {
                    int v = toHex(ps[++i]);
                    if(isHex(ps[i + 1])) {
                        v *= 16;
                        v += toHex(ps[++i]);
                    }
                    ch = (char) v;
                    break;
                }
                i--;
                break;
            }
            default:
                i--;
                break;
            }
        }
        output += ch;
    }
    return output;
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

    for(const char *ptr = nullptr; (ptr = strchr(path, '/')) != nullptr;) {
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
        if(basePath != nullptr) {
            resolvedPathStack = createPathStack(basePath);
        } else {
            size_t size = PATH_MAX;
            char buf[size];
            const char *cwd = getcwd(buf, size);
            if(cwd != nullptr) {
                resolvedPathStack = createPathStack(cwd);
            }
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

void expandTilde(std::string &str) {
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
        struct passwd *pw = getpwuid(getuid());
        if(pw != nullptr) {
            expanded = pw->pw_dir;
        }
    } else if(expanded == "~+") {
        expanded = getenv(ENV_PWD);
    } else if(expanded == "~-") {
        expanded = getenv(ENV_OLDPWD);
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


// for input completion
enum class EscapeOp {
    NOP,
    COMMAND_NAME,
    COMMAND_NAME_PART,
    COMMAND_ARG,
};

static std::string escape(const char *str, EscapeOp op) {
    std::string buf;
    if(op == EscapeOp::NOP) {
        buf += str;
        return buf;
    }

    if(op == EscapeOp::COMMAND_NAME) {
        char ch = *str;
        if(isDecimal(ch) || ch == '+' || ch == '-' || ch == '[' || ch == ']') {
            buf += '\\';
            buf += ch;
            str++;
        }
    }

    while(*str != '\0') {
        char ch = *(str++);
        bool found = false;
        switch(ch) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        case '\\':
        case ';':
        case '\'':
        case '"':
        case '`':
        case '|':
        case '&':
        case '<':
        case '>':
        case '(':
        case ')':
        case '$':
        case '#':
            found = true;
            break;
        case '{':
        case '}':
            if(op == EscapeOp::COMMAND_NAME || op == EscapeOp::COMMAND_NAME_PART) {
                found = true;
            }
            break;
        default:
            break;
        }

        if(found) {
            buf += '\\';
        }
        buf += ch;
    }

    buf += '\0';
    return buf;
}


static void append(CStrBuffer &buf, const char *str, EscapeOp op) {
    std::string estr = escape(str, op);

    // find inserting position
    for(auto iter = buf.begin(); iter != buf.end(); ++iter) {
        int r = strcmp(estr.c_str(), *iter);
        if(r <= 0) {
            if(r < 0) {
                buf.insert(iter, strdup(estr.c_str()));
            }
            return;
        }
    }

    // not found, append to last
    buf += strdup(estr.c_str());
}

static void append(CStrBuffer &buf, const std::string &str, EscapeOp op) {
    append(buf, str.c_str(), op);
}

static std::vector<std::string> computePathList(const char *pathVal) {
    std::vector<std::string> result;
    std::string buf;
    assert(pathVal != nullptr);

    for(unsigned int i = 0; pathVal[i] != '\0'; i++) {
        char ch = pathVal[i];
        if(ch == ':') {
            result.push_back(std::move(buf));
            buf = "";
        } else {
            buf += ch;
        }
    }
    if(!buf.empty()) {
        result.push_back(std::move(buf));
    }

    // expand tilde
    for(auto &s : result) {
        expandTilde(s);
    }

    return result;
}

static bool startsWith(const char *s1, const char *s2) {
    return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

/**
 * append candidates to results.
 * token may be empty string.
 */
static void completeCommandName(const DSState &ctx, const std::string &token, CStrBuffer &results) {
    // search user defined command
    for(const auto &iter : ctx.symbolTable.curScope()) {
        const char *name = iter.first.c_str();
        if(startsWith(name, SymbolTable::cmdSymbolPrefix)) {
            name += strlen(SymbolTable::cmdSymbolPrefix);
            if(startsWith(name, token.c_str())) {
                append(results, name, EscapeOp::COMMAND_NAME);
            }
        }
    }

    // search builtin command
    const unsigned int bsize = getBuiltinCommandSize();
    for(unsigned int i = 0; i < bsize; i++) {
        const char *name = getBuiltinCommandName(i);
        if(startsWith(name, token.c_str())) {
            append(results, name, EscapeOp::COMMAND_NAME);
        }
    }


    // search external command
    const char *path = getenv(ENV_PATH);
    if(path == nullptr) {
        return;
    }

    auto pathList(computePathList(path));
    for(const auto &p : pathList) {
        DIR *dir = opendir(p.c_str());
        if(dir == nullptr) {
            continue;
        }

        for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
            const char *name = entry->d_name;
            if(startsWith(name, token.c_str())) {
                std::string fullpath(p);
                fullpath += '/';
                fullpath += name;
                if(S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) == 0) {
                    append(results, name, EscapeOp::COMMAND_NAME);
                }
            }
        }
        closedir(dir);
    }
}

static void completeFileName(const DSState &st, const std::string &token,
                             CStrBuffer &results, bool onlyExec = true) {
    const auto s = token.find_last_of('/');

    // complete tilde
    if(token[0] == '~' && s == std::string::npos) {
        setpwent();
        for(struct passwd *entry = getpwent(); entry != nullptr; entry = getpwent()) {
            if(startsWith(entry->pw_name, token.c_str() + 1)) {
                std::string name("~");
                name += entry->pw_name;
                name += '/';
                append(results, name, EscapeOp::NOP);
            }
        }
        endpwent();
        return;
    }

    // complete file name

    /**
     * resolve directory path
     */
    std::string targetDir;
    if(s == 0) {
        targetDir = "/";
    } else if(s != std::string::npos) {
        targetDir = token.substr(0, s);
        expandTilde(targetDir);
        targetDir = expandDots(st.logicalWorkingDir.c_str(), targetDir.c_str());
    } else {
        targetDir = expandDots(st.logicalWorkingDir.c_str(), ".");
    }
    LOG(DUMP_CONSOLE, "targetDir = " << targetDir);

    /**
     * resolve name
     */
    std::string name;
    if(s != std::string::npos) {
        name = token.substr(s + 1);
    } else {
        name = token;
    }

    DIR *dir = opendir(targetDir.c_str());
    if(dir == nullptr) {
        return;
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if(startsWith(entry->d_name, name.c_str())) {
            if(name.empty() && (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)) {
                continue;
            }

            std::string fullpath(targetDir);
            fullpath += '/';
            fullpath += entry->d_name;

            if(onlyExec && S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) != 0) {
                continue;
            }

            std::string fileName = entry->d_name;
            if(S_ISDIR(getStMode(fullpath.c_str()))) {
                fileName += '/';
            }
            append(results, fileName, onlyExec ? EscapeOp::COMMAND_NAME_PART : EscapeOp::COMMAND_ARG);
        }
    }
    closedir(dir);
}

static void completeGlobalVarName(const DSState &ctx, const std::string &token, CStrBuffer &results) {
    for(const auto &iter : ctx.symbolTable.curScope()) {
        const char *varName = iter.first.c_str();
        if(!token.empty() && !startsWith(varName, SymbolTable::cmdSymbolPrefix)
           && startsWith(varName, token.c_str() + 1)) {
            append(results, iter.first, EscapeOp::NOP);
        }
    }
}

static void completeExpectedToken(const std::string &token, CStrBuffer &results) {
    if(!token.empty()) {
        append(results, token, EscapeOp::NOP);
    }
}

enum class CompletorKind {
    NONE,
    CMD,    // command name without '/'
    QCMD,   // command name with '/'
    FILE,
    VAR,    // complete global variable name
    EXPECT,
};

static bool isFileName(const std::string &str) {
    return !str.empty() && (str[0] == '~' || strchr(str.c_str(), '/') != nullptr);
}

static bool isRedirOp(TokenKind kind) {
    switch(kind) {
    case REDIR_IN_2_FILE:
    case REDIR_OUT_2_FILE:
    case REDIR_OUT_2_FILE_APPEND:
    case REDIR_ERR_2_FILE:
    case REDIR_ERR_2_FILE_APPEND:
    case REDIR_MERGE_ERR_2_OUT_2_FILE:
    case REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND:
    case REDIR_MERGE_ERR_2_OUT:
    case REDIR_MERGE_OUT_2_ERR:
        return true;
    default:
        return false;
    }
}

static bool isNewline(const Lexer &lexer, const std::pair<TokenKind, Token> &pair) {
    return pair.first == LINE_END && lexer.toTokenText(pair.second) != ";";
}

static CompletorKind selectWithCmd(const Lexer &lexer, const std::vector<std::pair<TokenKind, Token>> &tokenPairs,
                                   unsigned int cursor, unsigned int lastIndex,
                                   std::string &tokenStr, bool exactly = false) {
    TokenKind kind = tokenPairs[lastIndex].first;
    Token token = tokenPairs[lastIndex].second;

    if(exactly && isNewline(lexer, tokenPairs[lastIndex]) && lastIndex > 0) {
        lastIndex--;
        kind = tokenPairs[lastIndex].first;
        token = tokenPairs[lastIndex].second;
    }

    switch(kind) {
    case COMMAND:
        if(token.pos + token.size == cursor) {
            tokenStr = lexer.toTokenText(token);
            return isFileName(tokenStr) ? CompletorKind::QCMD : CompletorKind::CMD;
        }
        return CompletorKind::FILE;
    case CMD_ARG_PART:
        if(token.pos + token.size == cursor && lastIndex > 0) {
            auto prevKind = tokenPairs[lastIndex - 1].first;
            auto prevToken = tokenPairs[lastIndex - 1].second;

            /**
             * if previous token is redir op,
             * or if spaces exist between current and previous
             */
            if(isRedirOp(prevKind) || prevToken.pos + prevToken.size < token.pos) {
                tokenStr = lexer.toTokenText(token);
                return CompletorKind::FILE;
            }
            return CompletorKind::NONE;
        }
        return CompletorKind::FILE;
    default:
        if(token.pos + token.size == cursor && (kind == APPLIED_NAME || kind == SPECIAL_NAME)) {
            tokenStr = lexer.toTokenText(token);
            return CompletorKind::VAR;
        }

        if(!exactly && token.pos + token.size < cursor) {
            return CompletorKind::FILE;
        }
        return CompletorKind::NONE;
    }
}

static bool findKind(const std::vector<TokenKind> &values, TokenKind kind) {
    for(auto &e : values) {
        if(e == kind) {
            return true;
        }
    }
    return false;
}

static CompletorKind selectCompletor(const std::string &line, std::string &tokenStr) {
    CompletorKind kind = CompletorKind::NONE;

    const unsigned int cursor = line.size() - 1; //   ignore last newline

    // parse input line
    Lexer lexer("<line>", line.c_str());
    TokenTracker tracker;
    Parser parser(lexer);
    parser.setTracker(&tracker);
    auto rootNode = parser();

    if(!parser.hasError()) {
        const auto &tokenPairs = tracker.getTokenPairs();
        const unsigned int tokenSize = tokenPairs.size();

        assert(tokenSize > 0);

        unsigned int lastIndex = tokenSize - 1;

        if(lastIndex == 0) {
            goto END;
        }

        lastIndex--; // skip EOS

        switch(tokenPairs[lastIndex].first) {
        case RBC:
            kind = CompletorKind::CMD;
            goto END;
        case APPLIED_NAME:
        case SPECIAL_NAME: {
            Token token = tokenPairs[lastIndex].second;
            if(token.pos + token.size == cursor) {
                tokenStr = lexer.toTokenText(token);
                kind = CompletorKind::VAR;
                goto END;
            }
            break;
        }
        case LINE_END: {
            if(!isNewline(lexer, tokenPairs[lastIndex])) {  // terminate with ';'
                kind = CompletorKind::CMD;
                goto END;
            }

            lastIndex--; // skip LINE_END
            kind = selectWithCmd(lexer, tokenPairs, cursor, lastIndex, tokenStr);
            goto END;
        }
        default:
            break;
        }
    } else {
        const auto &e = *parser.getError();
        LOG_L(DUMP_CONSOLE, [&](std::ostream &stream) {
            stream << "error kind: " << e.getErrorKind() << std::endl;
            stream << "kind: " << toString(e.getTokenKind())
                   << ", token: " << toString(e.getErrorToken())
                   << ", text: " << lexer.toTokenText(e.getErrorToken()) << std::endl;
        });

        Token token = e.getErrorToken();
        auto &tokenPairs = tracker.getTokenPairs();
        if(token.pos + token.size < cursor) {
            goto END;
        }

        switch(e.getTokenKind()) {
        case EOS:
        case LINE_END: {
            if(lexer.toTokenText(token) == ";") {
                goto END;
            }

            if(strcmp(e.getErrorKind(), "NoViableAlter") == 0) {
                kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
                if(kind != CompletorKind::NONE) {
                    goto END;
                }

                if(!isNewline(lexer, tokenPairs.back()) && findKind(e.getExpectedTokens(), COMMAND)) {
                    kind = CompletorKind::CMD;
                    goto END;
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    kind = CompletorKind::FILE;
                    goto END;
                }
            } else if(strcmp(e.getErrorKind(), "TokenMismatched") == 0) {
                assert(e.getExpectedTokens().size() > 0);
                TokenKind expected = e.getExpectedTokens().back();
                LOG(DUMP_CONSOLE, "expected: " << toString(expected));

                kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
                if(kind != CompletorKind::NONE) {
                    goto END;
                }

                std::string expectedStr = toString(expected);
                if(expectedStr.size() < 2 || (expectedStr.front() != '<' && expectedStr.back() != '>')) {
                    tokenStr = std::move(expectedStr);
                    kind = CompletorKind::EXPECT;
                    goto END;
                }
            }
            break;
        }
        case INVALID: {
            std::string str = lexer.toTokenText(token);
            if(str == "$" && token.pos + token.size == cursor &&
               findKind(e.getExpectedTokens(), APPLIED_NAME) &&
               findKind(e.getExpectedTokens(), SPECIAL_NAME)) {
                tokenStr = std::move(str);
                kind = CompletorKind::VAR;
                goto END;
            }
            break;
        }
        default:
            break;
        }
    }

    END:
    LOG_L(DUMP_CONSOLE, [&](std::ostream &stream) {
        stream << "token size: " << tracker.getTokenPairs().size() << std::endl;
        for(auto &t : tracker.getTokenPairs()) {
            stream << "kind: " << toString(t.first)
                   << ", token: " << toString(t.second)
                   << ", text: " << lexer.toTokenText(t.second) << std::endl;
        }

        switch(kind) {
        case CompletorKind::NONE:
            stream << "ckind: NONE" << std::endl;
            break;
        case CompletorKind::CMD:
            stream << "ckind: CMD" << std::endl;
            break;
        case CompletorKind::QCMD:
            stream << "ckind: QCMD" << std::endl;
            break;
        case CompletorKind::FILE:
            stream << "ckind: FILE" << std::endl;
            break;
        case CompletorKind::VAR:
            stream << "ckind: VAR" << std::endl;
            break;
        case CompletorKind::EXPECT:
            stream << "ckind: EXPECT" << std::endl;
            break;
        }
    });

    return kind;
}

CStrBuffer completeLine(const DSState &st, const std::string &line) {
    assert(!line.empty() && line.back() == '\n');

    CStrBuffer sbuf;
    std::string tokenStr;
    switch(selectCompletor(line, tokenStr)) {
    case CompletorKind::NONE:
        break;  // do nothing
    case CompletorKind::CMD:
        completeCommandName(st, tokenStr, sbuf);
        break;
    case CompletorKind::QCMD:
        completeFileName(st, tokenStr, sbuf);
        break;
    case CompletorKind::FILE:
        completeFileName(st, tokenStr, sbuf, false);
        break;
    case CompletorKind::VAR:
        completeGlobalVarName(st, tokenStr, sbuf);
        break;
    case CompletorKind::EXPECT:
        completeExpectedToken(tokenStr, sbuf);
        break;
    }
    return sbuf;
}

} // namespace ydsh

