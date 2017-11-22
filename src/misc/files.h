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

#ifndef YDSH_MISC_FILES_H
#define YDSH_MISC_FILES_H

#include <dirent.h>
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <vector>
#include <list>
#include <string>
#include <cerrno>

#include "fatal.h"

namespace ydsh {

/**
 * if cannot open file, return always 0.
 */
inline mode_t getStMode(const char *fileName) {
    struct stat st;
    if(stat(fileName, &st) != 0) {
        return 0;
    }
    return st.st_mode;
}

#define S_IS_PERM_(mode, flag) (((mode) & (flag)) == (flag))

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

} // namespace ydsh

#endif //YDSH_MISC_FILES_H
