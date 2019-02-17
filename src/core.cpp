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
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <dirent.h>

#include <algorithm>
#include <cassert>

#include <config.h>
#include "vm.h"
#include "constant.h"
#include "parser.h"
#include "logger.h"
#include "cmd.h"
#include "misc/num.h"
#include "time_util.h"
#include "misc/files.h"

extern char **environ;

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

        struct stat st{};
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

struct StrArrayIter {
    Array_Object::IterType actual;

    explicit StrArrayIter(Array_Object::IterType actual) : actual(actual) {}

    const char *operator*() const {
        return str(*actual);
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

int GetOptState::operator()(const Array_Object &obj, const char *optStr) {
    auto iter = StrArrayIter(obj.getValues().begin() + this->index);
    auto end = StrArrayIter(obj.getValues().end());
    int ret = opt::GetOptState::operator()(iter, end, optStr);
    this->index = iter.actual - obj.getValues().begin();
    return ret;
}

// core api definition

SymbolTable &getPool(DSState &st) {
    return st.symbolTable;
}

const SymbolTable &getPool(const DSState &st) {
    return st.symbolTable;
}

JobTable &getJobTable(DSState &st) {
    return st.jobTable;
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

const DSValue &getGlobal(const DSState &st, const char *varName) {
    auto *handle = st.symbolTable.lookupHandle(varName);
    assert(handle != nullptr);
    return st.getGlobal(handle->getIndex());
}

unsigned int getTermHookIndex(DSState &st) {
    if(st.termHookIndex == 0) {
        auto *handle = st.symbolTable.lookupHandle(VAR_TERM_HOOK);
        assert(handle != nullptr);
        st.termHookIndex = handle->getIndex();
    }
    return st.termHookIndex;
}

bool hasError(const DSState &st) {
    return static_cast<bool>(st.getThrownObject());
}

void raiseError(DSState &st, TYPE type, std::string &&message, int status) {
    auto except = Error_Object::newError(st, st.symbolTable.get(type), DSValue::create<String_Object>(
            st.symbolTable.get(TYPE::String), std::move(message)));
    st.throwObject(std::move(except), status);
}

void raiseSystemError(DSState &st, int errorNum, std::string &&message) {
    assert(errorNum != 0);
    std::string str(std::move(message));
    str += ": ";
    str += strerror(errorNum);
    raiseError(st, TYPE::SystemError, std::move(str));
}

void fillInStackTrace(const DSState &st, std::vector<StackTraceElement> &stackTrace) {
    assert(!st.controlStack.empty());
    auto frame = st.getFrame();
    for(unsigned int callDepth = st.controlStack.size(); callDepth > 0; frame = st.controlStack[--callDepth]) {
        auto &callable = frame.code;
        if(!callable->is(CodeKind::NATIVE)) {
            const auto *cc = static_cast<const CompiledCode *>(callable);

            // create stack trace element
            const char *sourceName = cc->getSourceName();
            unsigned int lineNum = getLineNum(cc->getLineNumEntries(), frame.pc);

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
            stackTrace.emplace_back(sourceName, lineNum, std::move(callableName));
        }
    }
}

const char *getWorkingDir(const DSState &st, bool useLogical, std::string &buf) {
    if(useLogical) {
        if(!S_ISDIR(getStMode(st.logicalWorkingDir.c_str()))) {
            return nullptr;
        }
        buf = st.logicalWorkingDir;
    } else {
        char *ptr = realpath(".", nullptr);
        if(ptr == nullptr) {
            return nullptr;
        }
        buf = ptr;
        free(ptr);
    }
    return buf.c_str();
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
            char *cwd = realpath(".", nullptr);
            if(cwd != nullptr) {
                setenv(ENV_PWD, cwd, 1);
                st.logicalWorkingDir = cwd;
                free(cwd);
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
    if(handler == DFL_handler) {
        if(sigNum == SIGHUP) {
            actualHandler = handler;
        } else {
            op = SignalVector::UnsafeSigOp::DFL;
        }
    } else if(handler == IGN_handler) {
        op = SignalVector::UnsafeSigOp::IGN;
    } else if(sigNum == SIGSEGV || sigNum == SIGILL || sigNum == SIGFPE) {
        /**
         * always set default due to prevent undefined behavior.
         * see. https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
         */
        op = SignalVector::UnsafeSigOp::DFL;
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

    st.sigVector.install(SIGINT, op, handler);
    st.sigVector.install(SIGQUIT, op, handler);
    st.sigVector.install(SIGTSTP, op, handler);
    st.sigVector.install(SIGTTIN, op, handler);
    st.sigVector.install(SIGTTOU, op, handler);

    // due to prevent waitpid error (always wait child process termination)
    st.sigVector.install(SIGCHLD, SignalVector::UnsafeSigOp::DFL, handler, true);
}


static const char *safeBasename(const char *str) {
    const char *ptr = strrchr(str, '/');
    return ptr == nullptr ? str : ptr + 1;
}

static std::string findPWD() {
    std::string str;
    const char *cwd = getenv(ENV_PWD);
    if(cwd) {
        str = cwd;
    } else {
        char *ptr = realpath(".", nullptr);
        if(ptr) {
            str = ptr;
            free(ptr);
        } else {
            str = ".";
        }
    }
    return str;
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
                std::string str = findPWD();
                const char *cwd = str.c_str();
                const char *home = getenv(ENV_HOME);
                if(home != nullptr && strstr(cwd, home) == cwd) {
                    output += '~';
                    cwd += strlen(home);
                }
                output += cwd;
                continue;
            }
            case 'W':  {
                std::string str = findPWD();
                const char *cwd = str.c_str();
                if(str == ".") {
                    output += str;
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
                    int v = hexToNum(ps[++i]);
                    if(isHex(ps[i + 1])) {
                        v *= 16;
                        v += hexToNum(ps[++i]);
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
        if(basePath != nullptr && *basePath != '\0') {
            resolvedPathStack = createPathStack(basePath);
        } else {
            char *ptr = realpath(".", nullptr);
            if(ptr) {
                resolvedPathStack = createPathStack(ptr);
                free(ptr);
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
        char *ptr = realpath(".", nullptr);
        if(ptr) {
            expanded = ptr;
            free(ptr);
        }
    } else if(expanded == "~-") {
        const char *env = getenv(ENV_OLDPWD);
        if(env != nullptr) {
            expanded = env;
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
    result.emplace_back();
    assert(pathVal != nullptr);

    for(unsigned int i = 0; pathVal[i] != '\0'; i++) {
        char ch = pathVal[i];
        if(ch == ':') {
            result.emplace_back();
        } else {
            result.back() += ch;
        }
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
static CStrBuffer completeCommandName(const DSState &ctx, const std::string &token) {
    CStrBuffer results;

    // search user defined command
    for(const auto &iter : ctx.symbolTable.globalScope()) {
        const char *name = iter.first.c_str();
        if(startsWith(name, CMD_SYMBOL_PREFIX)) {
            name += strlen(CMD_SYMBOL_PREFIX);
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
        return results;
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
    return results;
}

static CStrBuffer completeFileName(const DSState &st, const std::string &token, bool onlyExec = true) {
    CStrBuffer results;

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
        return results;
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
    LOG(DUMP_CONSOLE, "targetDir = %s", targetDir.c_str());

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
        return results;
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
    return results;
}

static CStrBuffer completeGlobalVarName(const DSState &ctx, const std::string &token) {
    CStrBuffer results;

    for(const auto &iter : ctx.symbolTable.globalScope()) {
        const char *varName = iter.first.c_str();
        if(!token.empty() && !startsWith(varName, CMD_SYMBOL_PREFIX)
           && !startsWith(varName, MOD_SYMBOL_PREFIX)
           && startsWith(varName, token.c_str() + 1)) {
            append(results, iter.first, EscapeOp::NOP);
        }
    }
    return results;
}

static CStrBuffer completeExpectedToken(const std::string &token) {
    CStrBuffer results;

    if(!token.empty()) {
        append(results, token, EscapeOp::NOP);
    }
    return results;
}

static CStrBuffer completeEnvName(const std::string &envName) {
    CStrBuffer results;
    for(unsigned int i = 0; environ[i] != nullptr; i++) {
        const char *env = environ[i];
        if(startsWith(env, envName.c_str())) {
            const char *ptr = strchr(env, '=');
            assert(ptr != nullptr);
            append(results, std::string(env, ptr - env), EscapeOp::NOP);
        }
    }
    return results;
}

#define EACH_COMPLETOR_KIND(OP) \
    OP(NONE) \
    OP(CMD) /* command name without '/' */\
    OP(QCMD) /* command name with '/' */\
    OP(FILE) \
    OP(VAR) /* complete global variable name */\
    OP(EXPECT) \
    OP(ENV) /* complete environmental vairable name */


enum class CompletorKind : unsigned int {
#define GEN_ENUM(OP) OP,
    EACH_COMPLETOR_KIND(GEN_ENUM)
#undef GEN_ENUM
};

static const char *toString(CompletorKind kind) {
    const char *table[] = {
#define GEN_STR(OP) #OP,
            EACH_COMPLETOR_KIND(GEN_STR)
#undef GEN_STR
    };
    return table[static_cast<unsigned int>(kind)];
}


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

static std::pair<CompletorKind, std::string> selectWithCmd(const Parser &parser, unsigned int cursor, bool exactly = false) {
    auto &tokenPairs = parser.getTracker()->getTokenPairs();
    assert(!tokenPairs.empty());
    TokenKind kind = tokenPairs.back().first;
    Token token = tokenPairs.back().second;

    switch(kind) {
    case COMMAND:
        if(token.pos + token.size == cursor) {
            auto tokenStr = parser.getLexer()->toTokenText(token);
            auto kind = isFileName(tokenStr) ? CompletorKind::QCMD : CompletorKind::CMD;
            return {kind, std::move(tokenStr)};
        }
        return {CompletorKind::FILE, ""};
    case CMD_ARG_PART:
        if(token.pos + token.size == cursor && tokenPairs.size() > 1) {
            unsigned int prevIndex = tokenPairs.size() - 2;
            auto prevKind = tokenPairs[prevIndex].first;
            auto prevToken = tokenPairs[prevIndex].second;

            /**
             * if previous token is redir op,
             * or if spaces exist between current and previous
             */
            if(isRedirOp(prevKind) || prevToken.pos + prevToken.size < token.pos) {
                return {CompletorKind::FILE, parser.getLexer()->toTokenText(token)};
            }
            return {CompletorKind::NONE, ""};
        }
        return {CompletorKind::FILE, ""};
    default:
        if(!exactly && token.pos + token.size < cursor) {
            return {CompletorKind::FILE, ""};
        }
        return {CompletorKind::NONE, ""};
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

static bool inCmdMode(const Node &node) {
    switch(node.getNodeKind()) {
    case NodeKind::UnaryOp:
        return inCmdMode(*static_cast<const UnaryOpNode &>(node).getExprNode());
    case NodeKind::BinaryOp:
        return inCmdMode(*static_cast<const BinaryOpNode &>(node).getRightNode());
    case NodeKind::Cmd:
        return true;
    case NodeKind::Pipeline:
        return inCmdMode(*static_cast<const PipelineNode &>(node).getNodes().back());
    case NodeKind::Fork: {
        auto &forkNode = static_cast<const ForkNode &>(node);
        return forkNode.getOpKind() == ForkKind::COPROC && inCmdMode(*forkNode.getExprNode());
    }
    case NodeKind::Assert: {
        auto &assertNode = static_cast<const AssertNode &>(node);
        return assertNode.getMessageNode()->getToken().size == 0 && inCmdMode(*assertNode.getCondNode());
    }
    case NodeKind::Jump: {
        auto &jumpNode = static_cast<const JumpNode &>(node);
        return (jumpNode.getOpKind() == JumpNode::THROW || jumpNode.getOpKind() == JumpNode::RETURN)
               && inCmdMode(*jumpNode.getExprNode());
    }
    case NodeKind::VarDecl: {
        auto &vardeclNode = static_cast<const VarDeclNode &>(node);
        return vardeclNode.getExprNode() != nullptr && inCmdMode(*vardeclNode.getExprNode());
    }
    case NodeKind::Assign:
        return inCmdMode(*static_cast<const AssignNode &>(node).getRightNode());
    default:
        break;
    }
    return false;
}

static bool requireSingleCmdArg(const Node &node, unsigned int cursor) {
    auto token = node.getToken();
    if(token.pos + token.size == cursor) {
        auto kind = node.getNodeKind();
        if(kind == NodeKind::With) {
            return true;
        } else if(kind == NodeKind::Source) {
            return static_cast<const SourceNode&>(node).getName().empty();
        }
    }
    return false;
}

static std::unique_ptr<Node> applyAndGetLatest(Parser &parser) {
    std::unique_ptr<Node> node;
    while(parser) {
        node = parser();
        if(parser.hasError()) {
            break;
        }
    }
    return node;
}

static std::pair<CompletorKind, std::string> selectCompletor(const Parser &parser, const std::unique_ptr<Node> &node,
                                                             const unsigned int cursor) {
    auto &lexer = *parser.getLexer();
    const auto &tokenPairs = parser.getTracker()->getTokenPairs();

    if(!parser.hasError()) {
        if(tokenPairs.empty()) {
            return {CompletorKind::NONE, ""};
        }

        auto token = tokenPairs.back().second;
        switch(tokenPairs.back().first) {
        case LINE_END:
        case BACKGROUND:
        case DISOWN_BG:
            return {CompletorKind::CMD, ""};
        case RP:
            return {CompletorKind::EXPECT, ";"};
        case APPLIED_NAME:
        case SPECIAL_NAME:
            if(token.pos + token.size == cursor) {
                return {CompletorKind::VAR, lexer.toTokenText(token)};
            }
            break;
        case IDENTIFIER:
            if(node->getNodeKind() == NodeKind::VarDecl
                && static_cast<const VarDeclNode&>(*node).getKind() == VarDeclNode::IMPORT_ENV) {
                if(token.pos + token.size == cursor) {
                    return {CompletorKind::ENV, lexer.toTokenText(token)};
                }
            }
            break;
        default:
            break;
        }
        if(inCmdMode(*node) || requireSingleCmdArg(*node, cursor)) {
            return selectWithCmd(parser, cursor);
        }
    } else {
        const auto &e = parser.getError();
        LOG(DUMP_CONSOLE, "error kind: %s\nkind: %s, token: %s, text: %s",
            e.getErrorKind(), toString(e.getTokenKind()),
            toString(e.getErrorToken()).c_str(),
            lexer.toTokenText(e.getErrorToken()).c_str());

        Token token = e.getErrorToken();
        if(token.pos + token.size < cursor) {
            return {CompletorKind::NONE, ""};
        }

        switch(e.getTokenKind()) {
        case EOS: {
            if(strcmp(e.getErrorKind(), NO_VIABLE_ALTER) == 0) {
                auto pair = selectWithCmd(parser, cursor, true);
                if(pair.first != CompletorKind::NONE) {
                    return pair;
                }

                if(findKind(e.getExpectedTokens(), COMMAND)) {
                    return {CompletorKind::CMD, ""};
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    return {CompletorKind::FILE, ""};
                }
            } else if(strcmp(e.getErrorKind(), TOKEN_MISMATCHED) == 0) {
                assert(!e.getExpectedTokens().empty());
                TokenKind expected = e.getExpectedTokens().back();
                LOG(DUMP_CONSOLE, "expected: %s", toString(expected));

                auto pair = selectWithCmd(parser, cursor, true);
                if(pair.first != CompletorKind::NONE) {
                    return pair;
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    return {CompletorKind::FILE, ""};
                }

                if(!tokenPairs.empty() && tokenPairs.back().first == IMPORT_ENV
                        && findKind(e.getExpectedTokens(), IDENTIFIER)) {
                    return {CompletorKind::ENV, ""};
                }

                std::string expectedStr = toString(expected);
                if(expectedStr.size() < 2 || (expectedStr.front() != '<' && expectedStr.back() != '>')) {
                    return {CompletorKind::EXPECT, std::move(expectedStr)};
                }
            }
            break;
        }
        case INVALID: {
            std::string str = lexer.toTokenText(token);
            if(str == "$" && token.pos + token.size == cursor &&
               findKind(e.getExpectedTokens(), APPLIED_NAME) &&
               findKind(e.getExpectedTokens(), SPECIAL_NAME)) {
                return {CompletorKind::VAR, std::move(str)};
            }
            break;
        }
        default:
            break;
        }
    }
    return {CompletorKind::NONE, ""};
}

static std::pair<CompletorKind, std::string> selectCompletor(const std::string &line) {
    const unsigned int cursor = line.size() - 1; //   ignore last newline

    // parse input line
    Lexer lexer("<line>", line.c_str());
    TokenTracker tracker;
    Parser parser(lexer);
    parser.setTracker(&tracker);
    auto node = applyAndGetLatest(parser);
    auto pair = selectCompletor(parser, node, cursor);

    LOG_IF(DUMP_CONSOLE, {
        std::string str = "token size: ";
        str += std::to_string(tracker.getTokenPairs().size());
        str += "\n";
        for(auto &t : tracker.getTokenPairs()) {
            str += "kind: ";
            str += toString(t.first);
            str += ", token: ";
            str += toString(t.second);
            str += ", text: ";
            str += lexer.toTokenText(t.second);
            str += "\n";
        }
        str += "ckind: ";
        str += toString(pair.first);
        LOG(DUMP_CONSOLE, "%s", str.c_str());
    });

    return pair;
}

CStrBuffer completeLine(const DSState &st, const std::string &line) {
    assert(!line.empty() && line.back() == '\n');

    CStrBuffer sbuf;
    auto pair = selectCompletor(line);
    switch(pair.first) {
    case CompletorKind::NONE:
        break;  // do nothing
    case CompletorKind::CMD:
        sbuf = completeCommandName(st, pair.second);
        break;
    case CompletorKind::QCMD:
        sbuf = completeFileName(st, pair.second);
        break;
    case CompletorKind::FILE:
        sbuf = completeFileName(st, pair.second, false);
        break;
    case CompletorKind::VAR:
        sbuf = completeGlobalVarName(st, pair.second);
        break;
    case CompletorKind::EXPECT:
        sbuf = completeExpectedToken(pair.second);  //FIXME: complete multiple tokens
        break;
    case CompletorKind::ENV:
        sbuf = completeEnvName(pair.second);
        break;
    }
    return sbuf;
}

bool readAll(FILE *fp, ByteBuffer &buf) {
    while(true) {
        char data[128];
        clearerr(fp);
        errno = 0;
        unsigned int size = fread(data, sizeof(char), arraySize(data), fp);
        if(size > 0) {
            buf.append(data, size);
        } else if(errno) {
            if(errno == EINTR) {
                continue;
            }
            return false;
        } else {
            break;
        }
    }
    return true;
}

} // namespace ydsh

