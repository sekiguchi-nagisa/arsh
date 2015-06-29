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
#include <cstring>
#include <vector>
#include <list>
#include <string>

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

inline void getFileList(const char *dirPath, bool recursive, std::vector<std::string> &results) {
    std::list<std::string> dirList;
    dirList.push_back(dirPath);

    while(!dirList.empty()) {
        std::string path = std::move(dirList.front());
        dirList.pop_front();
        DIR *dir = opendir(path.c_str());
        if(dir == nullptr) {
            return;
        }

        dirent *entry;

        do {
            entry = readdir(dir);
            if(entry == nullptr) {
                break;
            }
            if(entry->d_type == DT_REG) {
                std::string name(path);
                name += "/";
                name += entry->d_name;
                results.push_back(std::move(name));
            } else if(recursive && entry->d_type == DT_DIR &&
                      strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                std::string name(path);
                name += "/";
                name += entry->d_name;
                dirList.push_back(std::move(name));
            }
        } while(true);
    }
}

/**
 * get full file path in specific directory
 */
inline std::vector<std::string> getFileList(const char *dirPath, bool recursive = false) {
    std::vector<std::string> fileList;
    getFileList(dirPath, recursive, fileList);
    return fileList;
}

#endif //MISC_FILES_H
