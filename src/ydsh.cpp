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
#include <parser/ErrorListener.h>
#include <core/RuntimeContext.h>
#include <core/TypePool.h>
#include <parser/TypeChecker.h>
#include <core/DSType.h>
#include <exe/Terminal.h>
#include <ast/Node.h>
#include <ast/dump.h>
using namespace std;

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
