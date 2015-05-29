/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef MISC_FILES_H
#define MISC_FILES_H

#include <unistd.h>
#include <dirent.h>
#include <cstdlib>

/**
 * get current working directory. return empty string, if error happened.
 */
inline std::string getCurrentWorkingDir() {
    size_t size = PATH_MAX;
    char buf[size];
    if(getcwd(buf, size) == nullptr) {
        return std::string();
    }
    return std::string(buf);
}

/**
 * get file list(exclude directory) of specific path.
 */
inline std::vector<const char *> getFileList(const char *path) {
    std::vector<const char *> fileList;

    DIR *dir = opendir(path);
    if(dir == nullptr) {
        exit(1);
    }

    dirent *entry;

    do {
        entry = readdir(dir);
        if(entry == nullptr) {
            break;
        }
        if(entry->d_type == DT_REG) {
            fileList.push_back(entry->d_name);
        }
    } while(true);

    return fileList;
}

#endif //MISC_FILES_H
