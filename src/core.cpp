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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <fcntl.h>
#include <dirent.h>

#include <cassert>

#include <config.h>
#include "state.h"
#include "symbol.h"
#include "parser.h"
#include "logger.h"
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

const char *FilePathCache::searchPath(const char *cmdName, unsigned char option) {
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
        pathPrefix = VAR_DEFAULT_PATH;
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

        if(resolvedPath[0] == '~') {
            resolvedPath = expandTilde(resolvedPath.c_str());
        }

        struct stat st;
        if(stat(resolvedPath.c_str(), &st) == 0 && (st.st_mode & S_IXUSR) == S_IXUSR) {
            if(hasFlag(option, DIRECT_SEARCH)) {
                this->prevPath = std::move(resolvedPath);
                return this->prevPath.c_str();
            } else {
                // set to cache
                if(this->map.size() == MAX_CACHE_SIZE) {
                    free(const_cast<char *>(this->map.begin()->first));
                    this->map.erase(this->map.begin());
                }
                auto pair = this->map.insert(std::make_pair(strdup(cmdName), std::move(resolvedPath)));
                assert(pair.second);
                return pair.first->second.c_str();
            }
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

// core api definition

TypePool &getPool(DSState &st) {
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

const char *getIFS(DSState &st) {
    if(st.IFS_index == 0) {
        auto handle = st.symbolTable.lookupHandle("IFS");
        st.IFS_index = handle->getFieldIndex();
        assert(handle != nullptr);
    }
    return typeAs<String_Object>(st.getGlobal(st.IFS_index))->getValue();
}

void setLocal(DSState &st, unsigned int index, const DSValue &obj) {
    st.setLocal(index, obj);
}

void setLocal(DSState &st, unsigned int index, DSValue &&obj) {
    st.setLocal(index, std::move(obj));
}

const DSValue &getLocal(const DSState &st, unsigned int index) {
    return st.getLocal(index);
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

void checkedPush(DSState &st, const DSValue &v) {
    st.push(v);
}

void checkedPush(DSState &st, DSValue &&v) {
    st.push(std::move(v));
}

void throwError(DSState &st, DSType &errorType, const char *message) {
    throwError(st, errorType, std::string(message));
}

void throwError(DSState &st, DSType &errorType, std::string &&message) {
    st.throwException(st.newError(errorType, std::move(message)));
}

void fillInStackTrace(const DSState &st, std::vector<StackTraceElement> &stackTrace) {
    unsigned int callableDepth = st.codeStack.size();

    unsigned int curPC = st.pc_;
    unsigned int curBottomIndex = st.stackBottomIndex;

    while(callableDepth) {
        auto &callable = st.codeStack[--callableDepth];
        if(!callable->is(CodeKind::NATIVE)) {
            const CompiledCode *cc = static_cast<const CompiledCode *>(callable);

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

            stackTrace.push_back(StackTraceElement(
                    sourceName, cc->getSrcInfo()->getLineNum(pos), std::move(callableName)));
        }

        // unwind state
        if(callableDepth) {
            const unsigned int offset = curBottomIndex;
            curPC = static_cast<unsigned int>(st.callStack[offset - 3].value());
            curBottomIndex = static_cast<unsigned int>(st.callStack[offset - 1].value());
        }
    }
}

std::string getConfigRootDir() {
#ifdef X_CONFIG_DIR
    return std::string(X_CONFIG_DIR);
#else
    std::string path(getenv(ENV_HOME));
    path += "/.ydsh";
    return path;
#endif
}

std::string getIfaceDir() {
    std::string root(getConfigRootDir());
    root += "/dbus/iface";
    return root;
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
    st.thrownObject = st.newError(st.pool.getShellExit(), std::move(str));

    // invoke termination hook
    if(st.terminationHook != nullptr) {
        const unsigned int lineNum =
                getOccuredLineNum(typeAs<Error_Object>(st.getThrownObject())->getStackTrace());
        st.terminationHook(DS_ERROR_KIND_EXIT, lineNum);
    }

    // print stack trace
    if(hasFlag(st.option, DS_OPTION_TRACE_EXIT)) {
        st.loadThrownObject();
        typeAs<Error_Object>(st.pop())->printStackTrace(st);
    }

    exit(status);
}

pid_t xfork(DSState &st) {
    pid_t pid = fork();
    if(pid == 0) {  // child process
        struct sigaction act;
        act.sa_handler = SIG_DFL;
        act.sa_flags = 0;
        sigemptyset(&act.sa_mask);

        /**
         * reset signal behavior
         */
        sigaction(SIGINT, &act, NULL);
        sigaction(SIGQUIT, &act, NULL);
        sigaction(SIGTSTP, &act, NULL);

        // update PID, PPID
        st.setGlobal(toIndex(BuiltinVarOffset::PID), DSValue::create<Int_Object>(st.pool.getUint32Type(), getpid()));
        st.setGlobal(toIndex(BuiltinVarOffset::PPID), DSValue::create<Int_Object>(st.pool.getUint32Type(), getppid()));
    }
    return pid;
}

/**
 * waitpid wrapper.
 */
pid_t xwaitpid(DSState &, pid_t pid, int &status, int options) {
    pid_t ret = waitpid(pid, &status, options);
    if(WIFSIGNALED(status)) {
        fputc('\n', stdout);
    }
    return ret;
}

static void format2digit(int num, std::string &out) {
    if(num < 10) {
        out += "0";
    }
    out += std::to_string(num);
}

static const char *safeBasename(const char *str) {
    const char *ptr = strrchr(str, '/');
    return ptr == nullptr ? str : ptr + 1;
}

void interpretPromptString(const DSState &st, const char *ps, std::string &output) {
    output.clear();

    struct tm *local = getLocalTime();

    static const char *wdays[] = {
            "Sun", "Mon", "Tue", "Wed", "Thurs", "Fri", "Sat"
    };

    const unsigned int hostNameSize = 128;    // in linux environment, HOST_NAME_MAX is 64
    static char hostName[hostNameSize];
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
                output += wdays[local->tm_wday];
                output += " ";
                output += std::to_string(local->tm_mon + 1);
                output += " ";
                output += std::to_string(local->tm_mday);
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
                format2digit(local->tm_hour, output);
                output += ":";
                format2digit(local->tm_min, output);
                output += ":";
                format2digit(local->tm_sec, output);
                continue;
            }
            case 'T': {
                format2digit(local->tm_hour < 12 ? local->tm_hour : local->tm_hour - 12, output);
                output += ":";
                format2digit(local->tm_min, output);
                output += ":";
                format2digit(local->tm_sec, output);
                continue;
            }
            case '@': {
                format2digit(local->tm_hour < 12 ? local->tm_hour : local->tm_hour - 12, output);
                output += ":";
                format2digit(local->tm_min, output);
                output += " ";
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
}

FuncObject *lookupUserDefinedCommand(const DSState &st, const char *commandName) {
    auto handle = st.symbolTable.lookupUdc(commandName);
    return handle == nullptr ? nullptr : typeAs<FuncObject>(st.getGlobal(handle->getFieldIndex()));
}

/**
 * path must be full path
 */
static std::vector<std::string> createPathStack(const char *path) {
    std::vector<std::string> stack;
    if(*path == '/') {
        stack.push_back("/");
        path++;
    }

    for(const char *ptr = nullptr; (ptr = strchr(path, '/')) != nullptr;) {
        const unsigned int size = ptr - path;
        if(size == 0) {
            path++;
            continue;
        }
        stack.push_back(std::string(path, size));
        path += size;
    }
    if(*path != '\0') {
        stack.push_back(path);
    }
    return stack;
}

/**
 * path is not null
 */
std::string expandDots(const char *basePath, const char *path) {
    assert(path != nullptr);
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
    std::string str;
    const unsigned int size = resolvedPathStack.size();
    for(unsigned int i = 1; i < size; i++) {
        str += '/';
        str += std::move(resolvedPathStack[i]);
    }
    return str;
}

std::string expandTilde(const char *path) {
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
    return expanded;
}


// for input completion

static void append(CStrBuffer &buf, const char *str) {
    // find inserting position
    for(auto iter = buf.begin(); iter != buf.end(); ++iter) {
        int r = strcmp(str, *iter);
        if(r <= 0) {
            if(r < 0) {
                buf.insert(iter, strdup(str));
            }
            return;
        }
    }

    // not found, append to last
    buf += strdup(str);
}

static void append(CStrBuffer &buf, const std::string &str) {
    append(buf, str.c_str());
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
        if(s[0] == '~') {
            std::string expanded = expandTilde(s.c_str());
            std::swap(s, expanded);
        }
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
    for(auto iter = ctx.symbolTable.cbeginGlobal(); iter != ctx.symbolTable.cendGlobal(); ++iter) {
        const char *name = iter->first.c_str();
        if(startsWith(name, SymbolTable::cmdSymbolPrefix)) {
            name += strlen(SymbolTable::cmdSymbolPrefix);
            if(startsWith(name, token.c_str())) {
                append(results, name);
            }
        }
    }

    // search builtin command
    const unsigned int bsize = getBuiltinCommandSize();
    for(unsigned int i = 0; i < bsize; i++) {
        const char *name = getBuiltinCommandName(i);
        if(startsWith(name, token.c_str())) {
            append(results, name);
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

        dirent *entry;
        while(true) {
            entry = readdir(dir);
            if(entry == nullptr) {
                break;
            }
            const char *name = entry->d_name;
            if(startsWith(name, token.c_str())) {
                std::string fullpath(p);
                fullpath += '/';
                fullpath += name;
                if(S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) == 0) {
                    append(results, name);
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
        for(struct passwd *entry = getpwent(); entry != nullptr; entry = getpwent()) {
            if(startsWith(entry->pw_name, token.c_str() + 1)) {
                std::string name("~");
                name += entry->pw_name;
                name += '/';
                append(results, name);
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
        if(targetDir[0] == '~') {
            targetDir = expandTilde(targetDir.c_str());
        }
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

    dirent *entry;
    while(true) {
        entry = readdir(dir);
        if(entry == nullptr) {
            break;
        }

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
            append(results, fileName);
        }
    }
    closedir(dir);
}

static void completeGlobalVarName(const DSState &ctx, const std::string &token, CStrBuffer &results) {
    for(auto iter = ctx.symbolTable.cbeginGlobal(); iter != ctx.symbolTable.cendGlobal(); ++iter) {
        const char *varName = iter->first.c_str();
        if(!token.empty() && !startsWith(varName, SymbolTable::cmdSymbolPrefix)
           && startsWith(varName, token.c_str() + 1)) {
            append(results, iter->first);
        }
    }
}

static void completeExpectedToken(const std::string &token, CStrBuffer &results) {
    if(!token.empty()) {
        append(results, token);
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
    try {
        {
            Parser parser;
            parser.setTracker(&tracker);
            RootNode rootNode;
            parser.parse(lexer, rootNode);
        }

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
    } catch(const ParseError &e) {
        LOG_L(DUMP_CONSOLE, [&](std::ostream &stream) {
            stream << "error kind: " << e.getErrorKind() << std::endl;
            stream << "kind: " << toString(e.getTokenKind())
                   << ", token: " << e.getErrorToken()
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
                if(!tokenPairs.empty()) {
                    kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
                    if(kind != CompletorKind::NONE) {
                        goto END;
                    }

                    if(!isNewline(lexer, tokenPairs.back()) && findKind(e.getExpectedTokens(), COMMAND)) {
                        kind = CompletorKind::CMD;
                        goto END;
                    }
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    kind = CompletorKind::FILE;
                    goto END;
                }
            } else if(strcmp(e.getErrorKind(), "TokenMismatched") == 0) {
                assert(e.getExpectedTokens().size() > 0);
                TokenKind expected = e.getExpectedTokens().back();
                LOG(DUMP_CONSOLE, "expected: " << toString(expected));

                if(!tokenPairs.empty()) {
                    kind = selectWithCmd(lexer, tokenPairs, cursor, tokenPairs.size() - 1, tokenStr, true);
                    if(kind != CompletorKind::NONE) {
                        goto END;
                    }
                }

                std::string expectedStr = toString(expected);
                if(expectedStr.size() < 2 || (expectedStr.front() != '<' && expectedStr.back() != '>')) {
                    tokenStr = std::move(expectedStr);
                    kind = CompletorKind::EXPECT;
                    goto END;
                }
                if(expected == COMMAND) {
                    kind = CompletorKind::CMD;
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
                   << ", token: " << t.second
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

