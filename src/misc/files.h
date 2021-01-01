/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef MISC_LIB_FILES_H
#define MISC_LIB_FILES_H

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <list>
#include <string>
#include <cerrno>

#include "fatal.h"
#include "resource.hpp"
#include "flag_util.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

/**
 * if cannot open file, return always 0.
 */
inline mode_t getStMode(const char *fileName) {
    struct stat st; //NOLINT
    if(stat(fileName, &st) != 0) {
        return 0;
    }
    return st.st_mode;
}

inline mode_t getStMode(int fd) {
    struct stat st; //NOLINT
    if(fstat(fd, &st) != 0) {
        return 0;
    }
    return st.st_mode;
}

#define S_IS_PERM_(mode, flag) (((mode) & (flag)) == (flag))

/**
 * check if fileName is regular and executable file
 * @param fileName
 * @return
 */
inline bool isExecutable(const char *fileName) {
    return S_ISREG(getStMode(fileName)) && access(fileName, X_OK) == 0;
}

inline CStrPtr getRealpath(const char *path) {
    return CStrPtr(realpath(path, nullptr));
}

inline CStrPtr getCWD() {
    return getRealpath(".");
}

inline bool isSameFile(const struct stat &st1, const struct stat &st2) {
    return st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino;
}

inline bool isSameFile(const char *f1, const char *f2) {
    struct stat st1;    //NOLINT
    if(stat(f1, &st1) != 0) {
        return false;
    }

    struct stat st2;    //NOLINT
    if(stat(f2, &st2) != 0) {
        return false;
    }
    return isSameFile(st1, st2);
}

inline bool setCloseOnExec(int fd, bool set) {
    int flag = fcntl(fd, F_GETFD);
    if(flag == -1) {
        return false;
    }
    if(set) {
        setFlag(flag, FD_CLOEXEC);
    } else {
        unsetFlag(flag, FD_CLOEXEC);
    }
    return fcntl(fd, F_SETFD, flag) != -1;
}

inline bool setFDFlag(int fd, int addFlag, bool set) {
    int flag = fcntl(fd, F_GETFL);
    if(flag == -1) {
        return false;
    }
    if(set) {
        setFlag(flag, addFlag);
    } else {
        unsetFlag(flag, addFlag);
    }
    return fcntl(fd, F_SETFL, flag) != -1;
}

inline int getFileList(const char *dirPath, bool recursive, std::vector<std::string> &results) {
    for(std::list<std::string> dirList = {dirPath}; !dirList.empty();) {
        std::string path = std::move(dirList.front());
        dirList.pop_front();
        DIR *dir = opendir(path.c_str());
        if(dir == nullptr) {
            return errno;
        }

        for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            std::string name = path;
            name += "/";
            name += entry->d_name;
            if(S_ISDIR(getStMode(name.c_str())) && recursive) {
                dirList.push_back(std::move(name));
            } else {
                results.push_back(std::move(name));
            }
        }
        closedir(dir);
    }
    return 0;
}

/**
 * get full file path in specific directory
 */
inline std::vector<std::string> getFileList(const char *dirPath, bool recursive = false) {
    std::vector<std::string> fileList;
    getFileList(dirPath, recursive, fileList);
    return fileList;
}

inline void removeDirWithRecursively(const char *currentDir) {
    if(!S_ISDIR(getStMode(currentDir))) {
        return;
    }

    DIR *dir = opendir(currentDir);
    if(dir == nullptr) {
        fatal_perror("cannot open dir: %s", currentDir);
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string fullpath = currentDir;
        fullpath += '/';
        fullpath += entry->d_name;
        const char *name = fullpath.c_str();
        if(S_ISDIR(getStMode(name))) {
            removeDirWithRecursively(name);
        } else if(remove(name) < 0) {
            fatal_perror("cannot remove: %s", name);
        }
    }
    closedir(dir);

    if(remove(currentDir) < 0) {
        fatal_perror("cannot remove: %s", currentDir);
    }
}

class TempFileFactory {
protected:
    const std::string tmpDirName;
    const std::string tmpFileName;

public:
    TempFileFactory() : tmpDirName(makeTempDir()), tmpFileName(this->createTempFile("", "")) {}

    virtual ~TempFileFactory() {
        removeDirWithRecursively(this->tmpDirName.c_str());
    }

    const char *getTempDirName() const {
        return this->tmpDirName.c_str();
    }

    const char *getTempFileName() const {
        return this->tmpFileName.c_str();
    }

    /**
     * create temp file with content
     * @param name
     * if empty string, generate random name. after file creation, write full path to it.
     * @param content
     * @return
     * opened file ptr with 'w+b' mode.
     */
    FilePtr createTempFilePtr(std::string &name, const char *data, unsigned int size) const {
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
                fatal_perror("");
            }
            filePtr = createFilePtr(fdopen, fd, "w+b");
            if(!filePtr) {
                close(fd);
            }
        }

        if(filePtr) {
            name = std::move(fileName);
            fwrite(data, sizeof(char), size, filePtr.get());
            fflush(filePtr.get());
        }
        return filePtr;
    }

    FilePtr createTempFilePtr(std::string &name, const std::string &content) const {
        return this->createTempFilePtr(name, content.data(), content.size());
    }

    /**
     * create temp file with content
     * @param baseName
     * if null or empty string, generate random name.
     * @param content
     * @return
     * full path of temp file
     */
    std::string createTempFile(const char *baseName, const std::string &content) const {
        std::string name = baseName != nullptr ? baseName : "";
        this->createTempFilePtr(name, content);
        return name;
    }

    static std::string makeTempDir() {
        const char *env = getenv("TMPDIR");
        if(env == nullptr || !S_ISDIR(getStMode(env))) {
            env = "/tmp";
        }
        auto ptr = getRealpath(env);
        assert(ptr);
        std::string name = ptr.get();
        name += "/test_tmp_dirXXXXXX";
        if(!mkdtemp(&name[0])) {
            fatal_perror("temp directory creation failed");
        }
        return name;
    }
};


END_MISC_LIB_NAMESPACE_DECL

#endif //MISC_LIB_FILES_H
