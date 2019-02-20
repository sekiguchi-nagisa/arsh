/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef YDSH_SIGNAL_LIST_H
#define YDSH_SIGNAL_LIST_H

#include <vector>
#include <csignal>

namespace ydsh {

struct SignalPair {
    const char *name;
    int sigNum;
};

/**
 * real time signal is not supported
 * @return
 * terminated with sentinel {nullptr, -1}
 */
const SignalPair *getSignalList();

/**
 *
 * @param name
 * @return
 * if invalid signal name, return -1
 */
int getSignalNum(const char *name);

/**
 *
 * @param sigNum
 * @return
 * if invalid signal number, return null
 */
const char *getSignalName(int sigNum);

/**
 * get sorted unique signal list
 * @return
 */
std::vector<int> getUniqueSignalList();

} // namespace ydsh

#endif //YDSH_SIGNAL_LIST_H
