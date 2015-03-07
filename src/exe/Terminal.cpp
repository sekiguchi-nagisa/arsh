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

#include <exe/Terminal.h>
#include <util/debug.h>

static char *prompt(EditLine *el) {
    return (char *) "test> ";
}

// ######################
// ##     Terminal     ##
// ######################

Terminal::Terminal(const char *progName) :
        lineNum(0), el(0), ydsh_history(0), event() {
    this->el = el_init(progName, stdin, stdout, stderr);
    el_set(this->el, EL_PROMPT, prompt);
    el_set(this->el, EL_EDITOR, "emacs");

    this->ydsh_history = history_init();
    if(this->ydsh_history == 0) {
        fatal("editline history initialization failed\n");
    }

    history(this->ydsh_history, &this->event, H_SETSIZE, 200);
    el_set(el, EL_HIST, history, this->ydsh_history);
}

Terminal::~Terminal() {
    history_end(this->ydsh_history);
    el_end(this->el);
}

const char *Terminal::readLine() {
    const char *line = el_gets(this->el, &this->lineNum);
    if(this->lineNum > 0) {
        history(this->ydsh_history, &this->event, H_ENTER, line);
    }
    return line;
}

unsigned int Terminal::getLineNum() {
    return this->lineNum;
}


