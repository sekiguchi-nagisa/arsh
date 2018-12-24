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


#include <cstdarg>
#include <climits>

#include <misc/files.h>
#include <misc/fatal.h>

#include "test_common.h"

#define error_at fatal_perror

// #############################
// ##     TempFileFactory     ##
// #############################

TempFileFactory::~TempFileFactory() {
    this->freeName();
}

static char *getTempRoot() {
    const char *tmpdir = getenv("TMPDIR");
    if(tmpdir == nullptr) {
        tmpdir = "/tmp";
    }
    return realpath(tmpdir, nullptr);
}

static char *makeTempDir() {
    char *tmpdir = getTempRoot();
    char *name = nullptr;
    if(asprintf(&name, "%s/test_tmp_dirXXXXXX", tmpdir) < 0) {
        error_at("");
    }
    free(tmpdir);
    char *dirName = mkdtemp(name);
    assert(dirName != nullptr);
    assert(dirName == name);
    return dirName;
}

void TempFileFactory::createTemp() {
    this->tmpDirName = makeTempDir();

    char *name;
    if(asprintf(&name, "%s/test_tmpXXXXXX", this->tmpDirName) < 0) {
        error_at("");
    }
    int fd = mkstemp(name);
    if(fd < 0) {
        error_at("");
    }
    close(fd);
    this->tmpFileName = name;
}

std::string TempFileFactory::createTempFile(const char *baseName, const std::string &content) const {
    std::string fileName = this->getTempDirName();
    fileName += '/';
    fileName += baseName;

    FILE *fp = fopen(fileName.c_str(), "w");
    fwrite(content.c_str(), sizeof(char), content.size(), fp);
    fclose(fp);
    return fileName;
}

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

void TempFileFactory::deleteTemp() {
    removeRecursive(this->tmpDirName);
    this->freeName();
}

void TempFileFactory::freeName() {
    free(this->tmpFileName);
    this->tmpFileName = nullptr;
    free(this->tmpDirName);
    this->tmpDirName = nullptr;
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

std::pair<std::string, std::string> InteractiveBase::readAll() {
    auto ret = this->handle.readAll(80);
    Screen screen(24, 200);
    screen.setReporter([&](std::string &&m) {
        this->send(m.c_str());
    });

    screen.interpret(ret.first.c_str(), ret.first.size());
    ret.first = screen.toString();
    return ret;
}