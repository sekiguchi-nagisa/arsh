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

#ifndef EXE_TERMINAL_H_
#define EXE_TERMINAL_H_

#include <histedit.h>
#include <string>

class Terminal {
private:
    EditLine *el;
    History *ydsh_history;
    HistEvent event;

    /**
     * contains previous read line.
     */
    std::string lineBuf;

public:
    Terminal(const char *progName);
    ~Terminal();

    /**
     * not delete return value.
     * return null if reach end of file or occurs error.
     * skip white space and empty string.
     */
    const char *readLine();

private:
    const char *readLineImpl();
    void addHistory();
};

#endif /* EXE_TERMINAL_H_ */
