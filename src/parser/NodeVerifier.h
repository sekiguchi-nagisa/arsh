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

#ifndef YDSH_NODEVERIFIER_H
#define YDSH_NODEVERIFIER_H

#include "../ast/Node.h"
#include "TypeCheckError.h"

namespace ydsh {
namespace parser {

using namespace ydsh::ast;

class ToplevelStatementVerifier : public BaseVisitor {
private:
    /**
     * maintain current processing node.
     */
    std::vector<Node *> currentNodes;

public:
    ToplevelStatementVerifier() = default;
    ~ToplevelStatementVerifier() = default;

    /**
     * entry point
     */
    void visit(Node *node); // override

    void visitDefault(Node *node);  // override

    void visitPipedCmdNode(PipedCmdNode *node); // override
    void visitBlockNode(BlockNode *node);   // override
    void visitForNode(ForNode *node);   // override
    void visitWhileNode(WhileNode *node);   // override
    void visitDoWhileNode(DoWhileNode *node);   // override
    void visitIfNode(IfNode *node); // override
    void visitCatchNode(CatchNode *node);   // override
    void visitTryNode(TryNode *node);   // override
    void visitFunctionNode(FunctionNode *node); // override
    void visitUserDefinedCmdNode(UserDefinedCmdNode *node); // override
    void visitRootNode(RootNode *node); // override
};


} // namespace parser
} // namespace ydsh

#endif //YDSH_NODEVERIFIER_H
