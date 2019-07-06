/*
 * Copyright (C) 2017-2018 Nagisa Sekiguchi
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

#include <fcntl.h>

#include <cstdarg>
#include <climits>

#include <misc/files.h>
#include <misc/fatal.h>

#include "test_common.h"

#define error_at fatal_perror

// #############################
// ##     TempFileFactory     ##
// #############################

static char *getTempRoot() {
    const char *tmpdir = getenv("TMPDIR");
    if(tmpdir == nullptr) {
        tmpdir = "/tmp";
    }
    return realpath(tmpdir, nullptr);
}

static std::string makeTempDir() {
    char *tmpdir = getTempRoot();
    std::string name = tmpdir;
    free(tmpdir);
    name += "/test_tmp_dirXXXXXX";
    char *dirName = mkdtemp(&name[0]);
    assert(dirName != nullptr);
    assert(dirName == name);
    (void) dirName;
    return name;
}

TempFileFactory::TempFileFactory() :
        tmpDirName(makeTempDir()), tmpFileName(this->createTempFile("", "")) {}

static void removeRecursive(const char *currentDir) {
    DIR *dir = opendir(currentDir);
    if(dir == nullptr) {
        error_at("cannot open dir: %s", currentDir);
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string fullpath = currentDir;
        fullpath += '/';
        fullpath += entry->d_name;
        const char *name = fullpath.c_str();
        if(S_ISDIR(ydsh::getStMode(name))) {
            removeRecursive(name);
        } else if(remove(name) < 0) {
            error_at("cannot remove: %s", name);
        }
    }
    closedir(dir);

    if(remove(currentDir) < 0) {
        error_at("cannot remove: %s", currentDir);
    }
}

TempFileFactory::~TempFileFactory() {
    removeRecursive(this->tmpDirName.c_str());
}

ydsh::FilePtr TempFileFactory::createTempFilePtr(std::string &name, const std::string &content) const {
    using namespace ydsh;

    FilePtr filePtr;
    std::string fileName = this->getTempDirName();
    fileName += '/';

    if(!name.empty()) {
        fileName += name;
        filePtr = createFilePtr(fopen, fileName.c_str(), "w+be");
    } else {
        fileName += "temp_XXXXXX";
        int fd = mkostemp(&fileName[0], O_CLOEXEC);
        if(fd < 0) {
            error_at("");
        }
        filePtr = createFilePtr(fdopen, fd, "w+b");
        if(!filePtr) {
            close(fd);
        }
    }

    if(filePtr) {
        name = std::move(fileName);
        fwrite(content.c_str(), sizeof(char), content.size(), filePtr.get());
        fflush(filePtr.get());
    }
    return filePtr;
}

std::string format(const char *fmt, ...) {
    va_list arg;

    va_start(arg, fmt);
    char *str = nullptr;
    if(vasprintf(&str, fmt, arg) == -1) {
        fatal_perror("");
    }
    va_end(arg);

    std::string v = str;
    free(str);
    return v;
}

// #######################
// ##     Extractor     ##
// #######################

void Extractor::consumeSpace() {
    for(; *this->str != '\0'; this->str++) {
        if(!isspace(*this->str)) {
            return;
        }
    }
}

int Extractor::extract(unsigned int &value) {
    std::string buf;
    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(!isdigit(ch)) {
            break;
        }
        buf += ch;
    }
    long v = std::stol(buf);
    if(v < 0 || v > UINT32_MAX) {
        return 1;
    }
    value = static_cast<unsigned int>(v);
    return 0;
}

int Extractor::extract(int &value) {
    std::string buf;
    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(!isdigit(ch)) {
            break;
        }
        buf += ch;
    }
    long v = std::stol(buf);
    if(v < INT32_MIN || v > INT32_MAX) {
        return 1;
    }
    value = static_cast<int>(v);
    return 0;
}

int Extractor::extract(std::string &value) {
    value.clear();

    if(*this->str != '"') {
        return 1;
    }
    this->str++;

    for(; *this->str != '\0'; this->str++) {
        char ch = *this->str;
        if(ch == '"') {
            this->str++;
            return 0;
        }
        if(ch == '\\') {
            char next = *(this->str + 1);
            if(next == '\\' || next == '"') {
                ch = next;
                this->str++;
            }
        }
        value += ch;
    }
    return 1;
}

int Extractor::extract(const char *value) {
    this->consumeSpace();

    auto size = strlen(value);
    if(strncmp(this->str, value, size) != 0) {
        return 1;
    }
    this->str += size;
    return 0;
}


// #############################
// ##     InteractiveBase     ##
// #############################

void InteractiveBase::invokeImpl(const std::vector<std::string> &args) {
    termios term;
    xcfmakesane(term);
    this->handle = ProcBuilder{this->binPath.c_str()}
            .addArgs(args)
            .addEnv("TERM", "xterm")
            .setWorkingDir(this->workingDir.c_str())
            .setIn(IOConfig::PTY)
            .setOut(IOConfig::PTY)
            .setErr(IOConfig::PIPE)
            .setWinSize(24, 200)
            .setTerm(term)();
}

void InteractiveBase::interpret(std::string &line) {
    Screen screen(this->handle.getWinSize());
    screen.setReporter([&](std::string &&m) {
        this->send(m.c_str());
    });
    screen.interpret(line.c_str(), line.size());
    line = screen.toString();
}
