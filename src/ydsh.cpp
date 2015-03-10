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
#include <util/debug.h>
#include <parser/Lexer.h>
#include <parser/Parser.h>
#include <parser/CommonErrorListener.h>
#include <exe/Terminal.h>
#include <ast/Node.h>
#include <ast/dump.h>
using namespace std;

int main(int argc, char **argv) {
    cout << "!!!Hello World!!!" << endl; // prints !!!Hello World!!!
    debugp("hello debug print %d!!\n", 12);
    debugp("hello debug print no arg\n");
   // Lexer lexer("12345; $a");
    Lexer lexer("assert");
    Token t;
    TokenKind k = EOS;
    do {
        k = lexer.nextToken(t);
        cout << TO_NAME(k) << endl;
    } while(k != EOS && k != INVALID);

    Terminal term(argv[0]);

    unsigned int lineNum = 1;
    const char *line;
    while((line = term.readLine()) != 0) {
        CommonErrorListener listener;
        Lexer lexer(line);
        lexer.setLineNum(lineNum);
        Parser parser(&lexer);
        try {
            RootNode *rootNode = parser.parse();
            dumpAST(cout, *rootNode);
            delete rootNode;
        } catch(const ParseError &e) {
            listener.displayParseError(lexer, "(stdin)", e);
        }
        lineNum = lexer.getLineNum();
    }
    return 0;
}
