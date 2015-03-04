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

%token_type { YYSTYPE }

%token_prefix { TOKEN_}
%extra_argument { ParserContext *ctx }

%include {
#include <assert.h>
#include <stdio.h>
#include <parser/ParserContext.h>
}

%syntax_error {
    fprintf(stderr, "Syntax error\n");
}

%parse_failure {
    fprintf(stderr,"Parser failure\n");
}


%start_symbol toplevel

toplevel ::= toplevelStatements(A) EOS. {
    ctx->setRootNode(A.rootNode);
    A.rootNode = 0;
}

toplevel ::= EOS. {
   ctx->setRootNode(new RootNode());
}

toplevelStatements(R) ::= toplevelStatements(A) toplevelStatement(B). {
    R.rootNode = A.rootNode;
    R.rootNode->addNode(B.node);
    A.rootNode = 0;
    B.node = 0;
}

toplevelStatements(R) ::= toplevelStatement(A). {
    R.rootNode = new RootNode(A.node);
    A.node = 0;
}

toplevelStatement(R) ::= statement(A). {
    R.node = A.node;
    A.node = 0;
}

statement(R) ::= LINE_END. {
    R.node = new EmptyNode();
}
