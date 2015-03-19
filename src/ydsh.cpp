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

#include <iostream>
#include <histedit.h>
#include <string>
#include <string.h>

#include <util/debug.h>
#include <parser/Lexer.h>
#include <parser/Parser.h>
#include <parser/ErrorListener.h>
#include <core/RuntimeContext.h>
#include <core/TypePool.h>
#include <parser/TypeChecker.h>
#include <core/DSType.h>
#include <ast/Node.h>
#include <ast/dump.h>
using namespace std;

static bool continuation = false;

static char *prompt(EditLine *el) {
    return continuation ? (char *) "> " : (char *) "test> ";
}

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


Terminal::Terminal(const char *progName) :
        el(0), ydsh_history(0), event(), lineBuf() {
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

static inline bool isSkipLine(const char *line, int count) {
    if(line == 0) {
        return false;
    }
    for(int i = 0; i < count; i++) {
        switch(line[i]) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            break;
        default:
            return false;
        }
    }
    return true;
}

static bool checkLineContinuation(const char *line) {
    if(line == 0) {
        return false;
    }
    unsigned int size = strlen(line);
    for(unsigned int i = 0; i < size; i++) {
        if(line[i] == '\\' && i + 1 < size && line[i + 1] == '\n') {
            return true;
        }
    }
    return false;
}

const char *Terminal::readLine() {
    this->lineBuf = std::string();
    const char *line;
    do {
        line = this->readLineImpl();
        if(line == 0) {
            return line;
        }
        this->lineBuf += line;
        continuation = true;
    } while(checkLineContinuation(line));

    continuation = false;
    this->addHistory();
    return this->lineBuf.c_str();
}

const char *Terminal::readLineImpl() {
    int count;
    const char *line;

    do {
        line = el_gets(this->el, &count);
    } while(isSkipLine(line, count));
    return line;
}

void Terminal::addHistory() {
    std::string target("\\\n");
    std::string buf(this->lineBuf);
    std::string::size_type pos = buf.find(target);
    while(pos != std::string::npos) {
        buf.replace(pos, target.size(), "");
        pos = buf.find(target, pos);
    }
    history(this->ydsh_history, &this->event, H_ENTER, buf.c_str());
}


int main(int argc, char **argv) {
    Terminal term(argv[0]);

    RuntimeContext ctx;
    TypeChecker checker(&ctx.pool);

    unsigned int lineNum = 1;
    const char *line;
    while((line = term.readLine()) != 0) {
        CommonErrorListener listener;
        Lexer lexer(line);
        lexer.setLineNum(lineNum);
        Parser parser(&lexer);
        RootNode *rootNode;
        try {
            rootNode = parser.parse();
            cout << "```` before check type ````" << endl;
            dumpAST(cout, *rootNode);
        } catch(const ParseError &e) {
            listener.displayParseError(lexer, "(stdin)", e);
            continue;
        }

        try {
            checker.checkTypeRootNode(rootNode);
            cout << "\n```` after check type ````" << endl;
            dumpAST(cout, *rootNode);

            cout << endl;
            // eval
            rootNode->eval(ctx, true);
        } catch(const TypeCheckError &e) {
            listener.displayTypeError("(stdin)", e);
            checker.recover();
        }
        delete rootNode;
        lineNum = lexer.getLineNum();
    }
    return 0;
}
