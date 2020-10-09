/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#include "parser.h"
#include "signals.h"
#include "complete.h"

// helper macro
#define HAS_NL() (this->lexer->isPrevNewLine())

#define HAS_SPACE() (this->lexer->isPrevSpace())

#define CUR_KIND() (this->curKind)

#define START_POS() (this->curToken.pos)

#define GEN_LA_CASE(CASE) case CASE:
#define GEN_LA_ALTER(CASE) CASE,


#define E_ALTER(...) \
do { this->reportNoViableAlterError((TokenKind[]) { __VA_ARGS__ }, false); return nullptr; } while(false)

#define E_ALTER_OR_COMP(...) \
do { this->reportNoViableAlterError((TokenKind[]) { __VA_ARGS__ }, true); return nullptr; } while(false)

#define TRY(expr) \
({ auto v = expr; if(this->hasError()) { return nullptr; } std::forward<decltype(v)>(v); })


namespace ydsh {

#define GUARD_DEEP_NESTING(name) \
CallCounter name(this->callCount); \
if(this->callCount == MAX_NESTING_DEPTH) { this->reportDeepNestingError(); return nullptr; } \
(void) name


// ####################
// ##     Parser     ##
// ####################

Parser::Parser(Lexer &lexer, ObserverPtr<CodeCompletionHandler> handler) {
    this->consumedKind = EOS;
    this->lexer = &lexer;
    this->ccHandler = handler;
    if(this->ccHandler) {
        this->lexer->setComplete(true);
        this->ccHandler->setLexer(*this->lexer);
    }
    this->fetchNext();
}

void Parser::refetch(LexerMode mode) {
    this->lexer->setPos(START_POS());
    this->lexer->setLexerMode(mode);
    this->fetchNext();
}

void Parser::restoreLexerState(Token prevToken) {
    unsigned int pos = prevToken.pos + prevToken.size;
    this->lexer->setPos(pos);
    this->lexer->popLexerMode();
    this->fetchNext();
}

void Parser::changeLexerModeToSTMT() {
    if(this->lexer->getPrevMode() != yycSTMT) {
        if(CUR_KIND() != LP && CUR_KIND() != LB && CUR_KIND() != LBC) {
            if(CUR_KIND() == WITH) {
                this->lexer->popLexerMode();
            }
            this->refetch(yycSTMT);
        }
    }
}

Token Parser::expect(TokenKind kind, bool fetchNext) {
    if(this->curKind == COMPLETION && !this->ccHandler->hasCompRequest()) {
        this->ccHandler->addExpectedTokenRequest(kind);
    }
    return parse_base_type::expect(kind, fetchNext);
}

Token Parser::expectAndChangeMode(TokenKind kind, LexerMode mode, bool fetchNext) {
    Token token = this->expect(kind, false);
    if(!this->hasError()) {
        this->lexer->setLexerMode(mode);
        if(fetchNext) {
            this->fetchNext();
        }
    }
    return token;
}

bool Parser::inVarNameCompletionPoint() const {
    if(this->inCompletionPoint()) {
        auto compTokenKind  = this->lexer->getCompTokenKind();
        if(compTokenKind == APPLIED_NAME) {
            auto ref = this->lexer->toStrRef(this->curToken);
            return ref.find('{') == StringRef::npos;
        }
    }
    return false;
}

void Parser::reportNoViableAlterError(unsigned int size, const TokenKind *alters, bool allowComp) {
    if(allowComp && this->inCompletionPoint()) {
        this->ccHandler->addExpectedTokenRequests(size, alters);
    }
    parse_base_type::reportNoViableAlterError(size, alters);
}

// parse rule definition
std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == FUNCTION);
    unsigned int startPos = START_POS();
    this->consume();    // FUNCTION
    Token token = TRY(this->expect(IDENTIFIER));
    auto node = std::make_unique<FunctionNode>(startPos, this->lexer->toName(token));
    TRY(this->expect(LP));

    for(unsigned int count = 0; CUR_KIND() != RP; count++) {
        if(count > 0) {
            if(CUR_KIND() != COMMA) {
                E_ALTER(COMMA, RP);
            }
            this->consume();    // COMMA
        }

        if(CUR_KIND() == APPLIED_NAME) {
            token = this->expect(APPLIED_NAME); // always success
            auto nameNode = this->newVarNode(token);
            TRY(this->expect(COLON, false));

            auto type = TRY(this->parse_typeName());

            node->addParamNode(std::move(nameNode), std::move(type));
        } else {
            E_ALTER(APPLIED_NAME, RP);
        }
    }
    this->expect(RP);   // always success
    node->updateToken(this->curToken);

    std::unique_ptr<TypeNode> retTypeNode;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false); // always success
        auto type = std::make_unique<ReturnTypeNode>(TRY(this->parse_typeName()));
        while(CUR_KIND() == COMMA) {
            this->expectAndChangeMode(COMMA, yycEXPR, false); // always success
            type->addTypeNode(TRY(this->parse_typeName()));
        }
        retTypeNode = std::move(type);
    }
    if(!retTypeNode) {
        retTypeNode = newVoidTypeNode();
    }
    node->setReturnTypeToken(std::move(retTypeNode));

    return node;
}

std::unique_ptr<Node> Parser::parse_interface() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == INTERFACE);
    unsigned int startPos = START_POS();

    this->expect(INTERFACE, false); // always success

    // enter TYPE mode
    this->pushLexerMode(yycTYPE);

    Token token = TRY(this->expect(IDENTIFIER));

    // exit TYPE mode
    this->restoreLexerState(token);

    auto node = std::make_unique<InterfaceNode>(startPos, this->lexer->toTokenText(token));
    TRY(this->expect(LBC));

    unsigned int count = 0;
    for(bool next = true; next && CUR_KIND() != RBC;) {
        // set lexer mode
        if(this->lexer->getPrevMode() != yycSTMT) {
            this->refetch(yycSTMT);
        }

        switch(CUR_KIND()) {
        case VAR:
        case LET: {
            startPos = START_POS();
            auto readOnly = this->scan() == LET ? VarDeclNode::CONST : VarDeclNode::VAR;
            token = TRY(this->expect(IDENTIFIER));
            TRY(this->expect(COLON, false));
            auto type = TRY(this->parse_typeName());
            node->addFieldDecl(
                    new VarDeclNode(startPos, this->lexer->toName(token), nullptr, readOnly), type.release());
            TRY(this->parse_statementEnd());
            count++;
            break;
        }
        case FUNCTION: {
            auto funcNode = TRY(this->parse_funcDecl());
            TRY(this->parse_statementEnd());
            node->addMethodDeclNode(funcNode.release());
            count++;
            break;
        }
        default:
            next = false;
            break;
        }
    }
    if(count == 0) {
        E_ALTER(FUNCTION, VAR, LET);
    }

    token = TRY(this->expect(RBC));
    node->updateToken(token);

    return std::move(node);
}

std::unique_ptr<TypeNode> Parser::parse_basicOrReifiedType(Token token) {
    GUARD_DEEP_NESTING(guard);

    auto typeToken = std::make_unique<BaseTypeNode>(token, this->lexer->toName(token));
    if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
        this->consume();
        std::vector<std::unique_ptr<TypeNode>> types;
        types.push_back(TRY(this->parse_typeName(false)));

        while(CUR_KIND() == TYPE_SEP) {
            this->consume();
            types.push_back(TRY(this->parse_typeName(false)));
        }
        token = TRY(this->expect(TYPE_CLOSE));
        return std::make_unique<ReifiedTypeNode>(std::move(typeToken), std::move(types), token);
    }
    return std::move(typeToken);
}

static std::unique_ptr<TypeNode> createTupleOrBasicType(
        Token open, std::vector<std::unique_ptr<TypeNode>> &&types,
        Token close, unsigned int commaCount) {
    if(commaCount == 0) {
        auto type = std::move(types[0]);
        type->setPos(open.pos);
        type->updateToken(close);
        return type;
    }
    return std::make_unique<ReifiedTypeNode>(std::make_unique<BaseTypeNode>(open, TYPE_TUPLE), std::move(types), close);
}

std::unique_ptr<TypeNode> Parser::parse_typeNameImpl() {
    switch(CUR_KIND()) {
    case IDENTIFIER: {
        Token token = this->expect(IDENTIFIER); // always success
        return this->parse_basicOrReifiedType(token);
    }
    case PTYPE_OPEN: {
        Token openToken = this->expect(PTYPE_OPEN);  // always success
        unsigned int count = 0;
        std::vector<std::unique_ptr<TypeNode>> types;
        while(CUR_KIND() != PTYPE_CLOSE) {
            types.push_back(TRY(this->parse_typeName(false)));
            if(CUR_KIND() == TYPE_SEP) {
                this->consume();    // COMMA
                count++;
            } else if(CUR_KIND() != PTYPE_CLOSE) {
                E_ALTER(TYPE_SEP, PTYPE_CLOSE);
            }
        }
        Token closeToken = TRY(this->expect(PTYPE_CLOSE));

        if(types.empty() || CUR_KIND() == TYPE_ARROW) {
            TRY(this->expect(TYPE_ARROW));
            return std::make_unique<FuncTypeNode>(openToken.pos, std::move(types),
                    TRY(this->parse_typeName(false)));
        } else {
            return createTupleOrBasicType(openToken, std::move(types), closeToken, count);
        }
    }
    case ATYPE_OPEN: {
        Token token = this->expect(ATYPE_OPEN);  // always success
        std::vector<std::unique_ptr<TypeNode>> types;
        types.push_back(TRY(this->parse_typeName(false)));
        bool isMap = CUR_KIND() == TYPE_MSEP;
        auto tempNode = std::make_unique<BaseTypeNode>(token, isMap ? TYPE_MAP : TYPE_ARRAY);
        if(isMap) {
            this->consume();
            types.push_back(TRY(this->parse_typeName(false)));
        }
        token = TRY(this->expect(ATYPE_CLOSE));
        return std::make_unique<ReifiedTypeNode>(std::move(tempNode), std::move(types), token);
    }
    case TYPEOF: {
        Token token = this->expect(TYPEOF); // always success
        if(CUR_KIND() == PTYPE_OPEN) {
            this->expect(PTYPE_OPEN, false);    // always success
            this->pushLexerMode(yycSTMT);

            unsigned int startPos = token.pos;
            auto exprNode(TRY(this->parse_expression()));

            token = TRY(this->expect(RP));
            return std::make_unique<TypeOfNode>(startPos, std::move(exprNode), token);
        }
        return this->parse_basicOrReifiedType(token);
    }
    case FUNC: {
        Token token = this->expect(FUNC);   // always success
        if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
            this->expect(TYPE_OPEN); // always success

            // parse return type
            unsigned int pos = token.pos;
            auto retNode = TRY(this->parse_typeName(false));
            std::vector<std::unique_ptr<TypeNode>> types;

            if(CUR_KIND() == TYPE_SEP) {   // ,[
                this->consume();    // TYPE_SEP
                TRY(this->expect(ATYPE_OPEN));

                // parse first arg type
                types.push_back(TRY(this->parse_typeName(false)));

                // rest arg type
                while(CUR_KIND() == TYPE_SEP) {
                    this->consume();
                    types.push_back(TRY(this->parse_typeName(false)));
                }
                TRY(this->expect(ATYPE_CLOSE));
            }

            token = TRY(this->expect(TYPE_CLOSE));
            return std::make_unique<FuncTypeNode>(pos, std::move(retNode), std::move(types), token);
        }
        return std::make_unique<BaseTypeNode>(token, this->lexer->toName(token));
    }
    default:
        E_ALTER(EACH_LA_typeName(GEN_LA_ALTER));
    }
}

std::unique_ptr<TypeNode> Parser::parse_typeName(bool enterTYPEMode) {
    GUARD_DEEP_NESTING(guard);

    if(enterTYPEMode) { // change lexer state to TYPE
        this->pushLexerMode(yycTYPE);
    }

    auto typeNode = TRY(this->parse_typeNameImpl());
    while(!HAS_NL() && CUR_KIND() == TYPE_OPT) {
        Token token = this->expect(TYPE_OPT); // always success
        typeNode = std::make_unique<ReifiedTypeNode>(std::move(typeNode),
                std::make_unique<BaseTypeNode>(token, TYPE_OPTION));
    }

    if(enterTYPEMode) {
        this->restoreLexerState(typeNode->getToken());
    }
    return typeNode;
}

std::unique_ptr<Node> Parser::parse_statementImp() {
    this->changeLexerModeToSTMT();

    if(this->inCompletionPoint()) {
        this->inStmtCompCtx = true;
    }
    auto cleanup = finally([&]{
        this->inStmtCompCtx = false;
    });

    switch(CUR_KIND()) {
    case LINE_END: {
        Token token = this->curToken;   // not consume LINE_END token
        return std::make_unique<EmptyNode>(token);
    }
    case FUNCTION: {
        auto node = TRY(this->parse_funcDecl());
        node->setBlockNode(TRY(this->parse_block()));
        return std::move(node);
    }
    case INTERFACE:
        return this->parse_interface();
    case ALIAS: {
        unsigned int startPos = START_POS();
        this->consume();    // ALIAS
        Token token = TRY(this->expect(IDENTIFIER));
        TRY(this->expect(ASSIGN, false));
        auto typeToken = TRY(this->parse_typeName());
        return std::make_unique<TypeAliasNode>(startPos, this->lexer->toTokenText(token), std::move(typeToken));
    }
    case ASSERT: {
        unsigned int pos = START_POS();
        this->consume();    // ASSERT
        auto condNode = TRY(this->parse_expression());
        std::unique_ptr<Node> messageNode;
        if(!HAS_NL() && CUR_KIND() == COLON) {
            TRY(this->expectAndChangeMode(COLON, yycSTMT));
            messageNode = TRY(this->parse_expression());
        } else {
            std::string msg = "`";
            msg += this->lexer->toTokenText(condNode->getToken());
            msg += "'";
            messageNode = std::make_unique<StringNode>(std::move(msg));
        }
        return std::make_unique<AssertNode>(pos, std::move(condNode), std::move(messageNode));
    }
    case EXPORT_ENV: {
        unsigned int startPos = START_POS();
        this->consume();    // EXPORT_ENV
        Token token = TRY(this->expect(IDENTIFIER));
        std::string name(this->lexer->toName(token));
        TRY(this->expect(ASSIGN));
        return std::make_unique<VarDeclNode>(startPos, std::move(name),
                                   TRY(this->parse_expression()), VarDeclNode::EXPORT_ENV);
    }
    case IMPORT_ENV: {
        unsigned int startPos = START_POS();
        this->consume();    // IMPORT_ENV
        if(this->inCompletionPointAt(IDENTIFIER)) {  // complete env name
            this->ccHandler->addCompRequest(CodeCompOp::ENV, this->lexer->toTokenText(this->curToken));
        }
        Token token = TRY(this->expect(IDENTIFIER));
        std::unique_ptr<Node> exprNode;
        if(!HAS_NL() && CUR_KIND() == COLON) {
            TRY(this->expectAndChangeMode(COLON, yycSTMT));
            exprNode = TRY(this->parse_expression());
        }

        auto node = std::make_unique<VarDeclNode>(startPos, this->lexer->toName(token),
                                        std::move(exprNode), VarDeclNode::IMPORT_ENV);
        node->updateToken(token);
        return std::move(node);
    }
    case SOURCE:
    case SOURCE_OPT: {
        bool optional = CUR_KIND() == SOURCE_OPT;
        unsigned int startPos = START_POS();
        this->consume();   // always success
        auto pathNode = TRY(this->parse_cmdArg(CmdArgParseOpt::MODULE));
        auto node = std::make_unique<SourceListNode>(startPos, std::move(pathNode), optional);
        if(!optional && CUR_KIND() == CMD_ARG_PART && this->lexer->toStrRef(this->curToken) == "as") {
            this->lexer->popLexerMode();
            this->expectAndChangeMode(CMD_ARG_PART, yycNAME); // always success
            Token token = TRY(this->expectAndChangeMode(IDENTIFIER, yycSTMT));
            node->setName(token, this->lexer->toName(token));
        }
        return std::move(node);
    }
    EACH_LA_varDecl(GEN_LA_CASE)
        return this->parse_variableDeclaration();
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        E_ALTER(EACH_LA_statement(GEN_LA_ALTER));
    }
}

std::unique_ptr<Node> Parser::parse_statement(bool disallowEOS) {
    GUARD_DEEP_NESTING(guard);

    auto node = TRY(this->parse_statementImp());
    TRY(this->parse_statementEnd(disallowEOS));
    return node;
}

std::unique_ptr<Node> Parser::parse_statementEnd(bool disallowEOS) {
    switch(CUR_KIND()) {
    case EOS:
        if(disallowEOS) {
            this->reportTokenMismatchedError(NEW_LINE);
        }
        break;
    case RBC:
        break;
    case LINE_END:
        this->consume();
        break;
    default:
        if(this->consumedKind == BACKGROUND || this->consumedKind == DISOWN_BG) {
            break;
        }
        if(this->inCompletionPoint()) {
            this->lexer->setComplete(false);
            this->consume();
        }
        if(!HAS_NL()) {
            this->reportTokenMismatchedError(NEW_LINE);
        }
        break;
    }
    return nullptr;
}

std::unique_ptr<BlockNode> Parser::parse_block() {
    GUARD_DEEP_NESTING(guard);

    Token token = TRY(this->expect(LBC));
    auto blockNode = std::make_unique<BlockNode>(token.pos);
    while(CUR_KIND() != RBC) {
        blockNode->addNode(TRY(this->parse_statement()));
    }
    token = TRY(this->expect(RBC));
    blockNode->updateToken(token);
    return blockNode;
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == VAR || CUR_KIND() == LET);
    unsigned int startPos = START_POS();
    auto readOnly = VarDeclNode::VAR;
    if(CUR_KIND() == VAR) {
        this->consume();
    } else if(CUR_KIND() == LET) {
        this->consume();
        readOnly = VarDeclNode::CONST;
    }

    Token token = TRY(this->expect(IDENTIFIER));
    std::string name = this->lexer->toName(token);
    std::unique_ptr<Node> exprNode;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false);
        auto typeNode = TRY(this->parse_typeName());
        exprNode = std::make_unique<NewNode>(std::move(typeNode));
    } else {
        TRY(this->expect(ASSIGN));
        exprNode = TRY(this->parse_expression());
    }
    return std::make_unique<VarDeclNode>(startPos, std::move(name), std::move(exprNode), readOnly);
}

std::unique_ptr<Node> Parser::parse_ifExpression(bool asElif) {
    GUARD_DEEP_NESTING(guard);

    unsigned int startPos = START_POS();
    TRY(this->expect(asElif ? ELIF : IF));
    auto condNode = TRY(this->parse_expression());
    auto thenNode = TRY(this->parse_block());

    // parse else
    std::unique_ptr<Node> elseNode;
    if(CUR_KIND() == ELIF) {
        elseNode = TRY(this->parse_ifExpression(true));
    } else if(CUR_KIND() == ELSE && this->lexer->getPrevMode() == yycEXPR) {
        this->consume();    // ELSE
        elseNode = TRY(this->parse_block());
    }
    return std::make_unique<IfNode>(startPos, std::move(condNode), std::move(thenNode), std::move(elseNode));
}

std::unique_ptr<Node> Parser::parse_caseExpression() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == CASE);
    unsigned int pos = START_POS();
    this->consume();    // CASE

    auto caseNode = std::make_unique<CaseNode>(pos, TRY(this->parse_expression()));
    TRY(this->expect(LBC));
    do {
        caseNode->addArmNode(TRY(this->parse_armExpression()));
    } while(CUR_KIND() != RBC);
    Token token = this->expect(RBC);    // always success
    caseNode->updateToken(token);
    return std::move(caseNode);
}

std::unique_ptr<ArmNode> Parser::parse_armExpression() {
    GUARD_DEEP_NESTING(guard);

    this->changeLexerModeToSTMT();

    std::unique_ptr<ArmNode> armNode;
    if(CUR_KIND() == ELSE) {
        unsigned int pos = START_POS();
        this->consume();    // ELSE
        armNode = std::make_unique<ArmNode>(pos);
    } else {
        auto base = getPrecedence(PIPE) + 1;
        armNode = std::make_unique<ArmNode>(TRY(this->parse_expression(base)));
        while(CUR_KIND() == PIPE) {
            this->expect(PIPE); // always success
            armNode->addPatternNode(TRY(this->parse_expression(base)));
        }
    }

    TRY(this->expect(CASE_ARM));
    armNode->setActionNode(TRY(this->parse_expression()));
    TRY(this->parse_statementEnd());

    return armNode;
}

std::unique_ptr<Node> Parser::parse_forExpression() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == FOR);
    unsigned int startPos = START_POS();
    this->consume();    // FOR

    if(CUR_KIND() == LP) {  // for
        this->consume();    // LP

        auto initNode = TRY(this->parse_statementImp());
        TRY(this->expect(LINE_END));

        auto condNode = TRY(this->parse_forCond());
        TRY(this->expect(LINE_END));

        auto iterNode = TRY(this->parse_forIter());

        TRY(this->expect(RP));
        auto blockNode = TRY(this->parse_block());

        return std::make_unique<LoopNode>(startPos, std::move(initNode), std::move(condNode),
                                          std::move(iterNode), std::move(blockNode));
    }
    // for-in
    Token token = TRY(this->expect(APPLIED_NAME));
    TRY(this->expect(IN));
    auto exprNode = TRY(this->parse_expression());
    auto blockNode = TRY(this->parse_block());

    return createForInNode(startPos, this->lexer->toName(token), std::move(exprNode), std::move(blockNode));
}

std::unique_ptr<Node> Parser::parse_forCond() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        return nullptr;
    }
}

std::unique_ptr<Node> Parser::parse_forIter() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        return std::make_unique<EmptyNode>();
    }
}

std::unique_ptr<CatchNode> Parser::parse_catchStatement() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == CATCH);
    unsigned int startPos = START_POS();
    this->consume();    // CATCH

    bool paren = CUR_KIND() == LP;
    if(paren) {
        TRY(this->expect(LP));
    }

    Token token = TRY(this->expect(APPLIED_NAME));
    std::unique_ptr<TypeNode> typeToken;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false); // always success
        typeToken = TRY(this->parse_typeName());
    }

    if(paren) {
        TRY(this->expect(RP));
    }

    auto blockNode = TRY(this->parse_block());
    return std::make_unique<CatchNode>(startPos, this->lexer->toName(token),
            std::move(typeToken), std::move(blockNode));
}

// command
std::unique_ptr<Node> Parser::parse_command() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == COMMAND);
    Token token = this->expect(COMMAND);   // always success

    if(CUR_KIND() == LP) {  // command definition
        this->consume();    // LP
        TRY(this->expect(RP));
        auto blockNode = TRY(this->parse_block());
        return std::make_unique<UserDefinedCmdNode>(token.pos, this->lexer->toCmdArg(token), std::move(blockNode));
    }

    auto kind = this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
    auto node = std::make_unique<CmdNode>(std::make_unique<StringNode>(token, this->lexer->toCmdArg(token), kind));

    for(bool next = true; next && HAS_SPACE() && !HAS_NL();) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            auto argNode = this->parse_cmdArg();
            if(this->hasError()) {
                if(this->inCompletionPoint()
                    && hasFlag(this->ccHandler->getCompOp(), CodeCompOp::FILE)) {
                    this->ccHandler->addCompHookRequest(std::move(node));
                }
                return nullptr;
            }
            node->addArgNode(std::move(argNode));
            break;
        }
        EACH_LA_redir(GEN_LA_CASE)
            node->addRedirNode(TRY(this->parse_redirOption()));
            break;
        case INVALID:
#define EACH_LA_cmdArgs(E) \
            EACH_LA_cmdArg(E) \
            EACH_LA_redir(E)

            E_ALTER(EACH_LA_cmdArgs(GEN_LA_ALTER));
#undef EACH_LA_cmdArgs
        default:
            next = false;
            break;
        }
    }
    return std::move(node);
}

std::unique_ptr<RedirNode> Parser::parse_redirOption() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    EACH_LA_redirFile(GEN_LA_CASE) {
        TokenKind kind = this->scan();
        return std::make_unique<RedirNode>(kind, TRY(this->parse_cmdArg(CmdArgParseOpt::REDIR)));
    }
    EACH_LA_redirNoFile(GEN_LA_CASE) {
        Token token = this->curToken;
        TokenKind kind = this->scan();
        return std::make_unique<RedirNode>(kind, token);
    }
    default:
        E_ALTER(EACH_LA_redir(GEN_LA_ALTER));
    }
}

std::unique_ptr<CmdArgNode> Parser::parse_cmdArg(CmdArgParseOpt opt) {
    GUARD_DEEP_NESTING(guard);

    assert(!hasFlag(opt, CmdArgParseOpt::FIRST));
    auto node = std::make_unique<CmdArgNode>(TRY(this->parse_cmdArgSeg(opt | CmdArgParseOpt::FIRST)));

    for(bool next = true; !HAS_SPACE() && !HAS_NL() && next;) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE)
            node->addSegmentNode(TRY(this->parse_cmdArgSeg(opt)));
            break;
        default:
            next = false;
            break;
        }
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_cmdArgSeg(CmdArgParseOpt opt) {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case CMD_ARG_PART:
        return this->parse_cmdArgPart(opt);
    case GLOB_ANY: {
        Token token = this->curToken;
        this->consume();
        return std::make_unique<WildCardNode>(token, GlobMeta::ANY);
    }
    case GLOB_ZERO_OR_MORE: {
        Token token = this->curToken;
        this->consume();
        return std::make_unique<WildCardNode>(token, GlobMeta::ZERO_OR_MORE);
    }
    case STRING_LITERAL:
        return this->parse_stringLiteral();
    case OPEN_DQUOTE:
        return this->parse_stringExpression();
    case START_SUB_CMD:
        return this->parse_cmdSubstitution();
    case START_IN_SUB:
    case START_OUT_SUB:
        return this->parse_procSubstitution();
    EACH_LA_paramExpansion(GEN_LA_CASE)
        return this->parse_paramExpansion();
    default:
        if(this->inCompletionPoint()) {
            if(this->inVarNameCompletionPoint()) {  //FIXME: scope-aware completion
                this->ccHandler->addVarNameRequest(this->curToken);
            } else if(HAS_SPACE() || !hasFlag(opt, CmdArgParseOpt::MODULE)) {
                this->ccHandler->addCmdArgOrModRequest(this->curToken, opt);
            }
        }
        E_ALTER(EACH_LA_cmdArg(GEN_LA_ALTER));
    }
}

std::unique_ptr<StringNode> Parser::parse_cmdArgPart(CmdArgParseOpt opt) {
    GUARD_DEEP_NESTING(guard);

    Token token = TRY(this->expect(CMD_ARG_PART));
    auto kind = hasFlag(opt, CmdArgParseOpt::FIRST) && this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
    return std::make_unique<StringNode>(token, this->lexer->toCmdArg(token), kind);
}

static std::unique_ptr<Node> createBinaryNode(std::unique_ptr<Node> &&leftNode, TokenKind op,
                                              Token token, std::unique_ptr<Node> &&rightNode) {
    if(op == PIPE) {
        if(isa<PipelineNode>(*leftNode)) {
            cast<PipelineNode>(leftNode.get())->addNode(std::move(rightNode));
            return std::move(leftNode);
        }
        return std::make_unique<PipelineNode>(std::move(leftNode), std::move(rightNode));
    }
    if(isAssignOp(op)) {
        return createAssignNode(std::move(leftNode), op, token, std::move(rightNode));
    }
    return std::make_unique<BinaryOpNode>(std::move(leftNode), op, token, std::move(rightNode));
}

/**
 * see. https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing
 * @param basePrecedence
 * @return
 */
std::unique_ptr<Node> Parser::parse_expression(unsigned int basePrecedence) {
    GUARD_DEEP_NESTING(guard);

    auto node = TRY(this->parse_unaryExpression());
    while(!HAS_NL()) {
        const auto info = getOpInfo(this->curKind);
        if(!hasFlag(info.attr, OperatorAttr::INFIX) || info.prece < basePrecedence) {
            break;
        }

        switch(this->curKind) {
        case AS: {
            this->expect(AS, false);    // always success
            auto type = TRY(this->parse_typeName());
            node = std::make_unique<TypeOpNode>(std::move(node), std::move(type), TypeOpNode::NO_CAST);
            break;
        }
        case IS: {
            this->expect(IS, false);   // always success
            auto type = TRY(this->parse_typeName());
            node = std::make_unique<TypeOpNode>(std::move(node), std::move(type), TypeOpNode::ALWAYS_FALSE);
            break;
        }
        case WITH: {
            this->consume();    // WITH
            auto redirNode = TRY(this->parse_redirOption());
            auto withNode = std::make_unique<WithNode>(std::move(node), std::move(redirNode));
            for(bool next = true; next && HAS_SPACE();) {
                switch(CUR_KIND()) {
                EACH_LA_redir(GEN_LA_CASE) {
                    withNode->addRedirNode(TRY(this->parse_redirOption()));
                    break;
                }
                case INVALID:
                    E_ALTER(EACH_LA_redir(GEN_LA_ALTER));
                default:
                    next = false;
                    break;
                }
            }
            node = std::move(withNode);
            break;
        }
        case TERNARY: {
            this->consume();    // TERNARY
            auto tleftNode = TRY(this->parse_expression(getPrecedence(TERNARY)));
            TRY(this->expectAndChangeMode(COLON, yycSTMT));
            auto trightNode = TRY(this->parse_expression(getPrecedence(TERNARY)));
            unsigned int pos = node->getPos();
            node = std::make_unique<IfNode>(pos, std::move(node), std::move(tleftNode), std::move(trightNode));
            break;
        }
        case BACKGROUND:
        case DISOWN_BG: {
            Token token = this->curToken;
            bool disown = this->scan() == DISOWN_BG;
            return ForkNode::newBackground(std::move(node), token, disown);
        }
        default: {
            Token token = this->curToken;
            TokenKind op = this->scan();
            unsigned int nextPrece = info.prece + (hasFlag(info.attr, OperatorAttr::RASSOC) ? 0 : 1);
            auto rightNode = TRY(this->parse_expression(nextPrece));
            node = createBinaryNode(std::move(node), op, token, std::move(rightNode));
            break;
        }
        }
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_unaryExpression() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case PLUS:
    case MINUS:
    case NOT: {
        Token token = this->curToken;
        TokenKind op = this->scan();
        return std::make_unique<UnaryOpNode>(op, token, TRY(this->parse_unaryExpression()));
    }
    case THROW: {
        auto token = this->expect(THROW);   // always success
        auto exprNode = TRY(this->parse_expression(getPrecedence(THROW)));
        return JumpNode::newThrow(token, std::move(exprNode));
    }
    case COPROC: {
        auto token = this->expect(COPROC);  // always success
        auto exprNode = TRY(this->parse_expression(getPrecedence(COPROC)));
        return ForkNode::newCoproc(token, std::move(exprNode));
    }
    default:
        return this->parse_suffixExpression();
    }
}

std::unique_ptr<Node> Parser::parse_suffixExpression() {
    GUARD_DEEP_NESTING(guard);

    auto node = TRY(this->parse_primaryExpression());

    for(bool next = true; !HAS_NL() && next;) {
        switch(CUR_KIND()) {
        case ACCESSOR: {
            this->consume();    // ACCESSOR
            Token token = TRY(this->expect(IDENTIFIER));
            node = std::make_unique<AccessNode>(std::move(node), this->newVarNode(token));
            if(CUR_KIND() == LP && !HAS_NL()) {  // treat as method call
                auto args = TRY(this->parse_arguments());
                token = args.getToken();
                node = std::make_unique<ApplyNode>(std::move(node), args.take());
            }
            node->updateToken(token);
            break;
        }
        case LB: {
            auto opToken = this->curToken;
            this->consume();    // LB
            auto indexNode = TRY(this->parse_expression());
            auto token = TRY(this->expect(RB));
            node = ApplyNode::newIndexCall(std::move(node), opToken, std::move(indexNode));
            node->updateToken(token);
            break;
        }
        case LP: {
            auto args = TRY(this->parse_arguments());
            Token token = args.getToken();
            node = std::make_unique<ApplyNode>(std::move(node), args.take(), ApplyNode::FUNC_CALL);
            node->updateToken(token);
            break;
        }
        case INC:
        case DEC: {
            Token token = this->curToken;
            TokenKind op = this->scan();
            node = createSuffixNode(std::move(node), op, token);
            break;
        }
        case UNWRAP: {
            Token token = this->curToken;
            TokenKind op = this->scan(); // UNWRAP
            node = std::make_unique<UnaryOpNode>(std::move(node), op, token);
            break;
        }
        default:
            next = false;
            break;
        }
    }
    return node;
}

static std::unique_ptr<Node> createTupleOrGroup(
        Token open,
        std::vector<std::unique_ptr<Node>> &&nodes,
        Token close, unsigned int commaCount) {
    if(commaCount == 0) {
        auto node = std::move(nodes[0]);
        node->setPos(open.pos);
        node->updateToken(close);
        return node;
    } else {
        return std::make_unique<TupleNode>(open.pos, std::move(nodes), close);
    }
}

std::unique_ptr<Node> Parser::parse_primaryExpression() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case COMMAND:
        return parse_command();
    case NEW: {
        unsigned int startPos = START_POS();
        this->expect(NEW, false);   // always success
        auto type = TRY(this->parse_typeName());
        auto args = TRY(this->parse_arguments());
        Token token = args.getToken();
        auto node = std::make_unique<NewNode>(startPos, std::move(type), args.take());
        node->updateToken(token);
        return std::move(node);
    }
    case INT_LITERAL: {
        auto pair = TRY(this->expectNum(INT_LITERAL, &Lexer::toInt64));
        return NumberNode::newInt(pair.first, pair.second);
    }
    case FLOAT_LITERAL: {
        auto pair = TRY(this->expectNum(FLOAT_LITERAL, &Lexer::toDouble));
        return NumberNode::newFloat(pair.first, pair.second);
    }
    case STRING_LITERAL:
        return this->parse_stringLiteral();
    case REGEX_LITERAL:
        return this->parse_regexLiteral();
    case SIGNAL_LITERAL:
        return this->parse_signalLiteral();
    case OPEN_DQUOTE:
        return this->parse_stringExpression();
    case START_SUB_CMD:
        return this->parse_cmdSubstitution();
    case APPLIED_NAME:
    case SPECIAL_NAME:
        return this->parse_appliedName(CUR_KIND() == SPECIAL_NAME);
    case START_IN_SUB:
    case START_OUT_SUB:
        return this->parse_procSubstitution();
    case LP: {  // group or tuple
        Token openToken = this->expect(LP); // always success
        unsigned int count = 0;
        std::vector<std::unique_ptr<Node>> nodes;
        do {
            nodes.push_back(TRY(this->parse_expression()));
            if(CUR_KIND() == COMMA) {
                this->consume();    // COMMA
                count++;
            } else if(CUR_KIND() != RP) {
                E_ALTER(COMMA, RP);
            }
        } while(CUR_KIND() != RP);
        Token closeToken = TRY(this->expect(RP));
        return createTupleOrGroup(openToken, std::move(nodes), closeToken, count);
    }
    case LB: {  // array or map
        Token token = this->expect(LB); // always success
        auto keyNode = TRY(this->parse_expression());
        std::unique_ptr<Node> node;
        if(CUR_KIND() == COMMA || CUR_KIND() == RB) {   // array
            node = TRY(this->parse_arrayBody(token, std::move(keyNode)));
        } else if(CUR_KIND() == COLON) {    // map
            node = TRY(this->parse_mapBody(token, std::move(keyNode)));
        } else {
            E_ALTER_OR_COMP(COMMA, RB, COLON);
        }
        token = TRY(this->expect(RB));
        node->updateToken(token);
        return node;
    }
    case LBC:
        return this->parse_block();
    case FOR:
        return this->parse_forExpression();
    case IF:
        return this->parse_ifExpression();
    case CASE:
        return this->parse_caseExpression();
    case WHILE: {
        unsigned int startPos = START_POS();
        this->consume();    // WHILE
        auto condNode = TRY(this->parse_expression());
        auto blockNode = TRY(this->parse_block());
        return std::make_unique<LoopNode>(startPos, std::move(condNode), std::move(blockNode));
    }
    case DO: {
        unsigned int startPos = START_POS();
        this->consume();    // DO
        auto blockNode = TRY(this->parse_block());
        TRY(this->expect(WHILE));
        auto condNode = TRY(this->parse_expression());
        return std::make_unique<LoopNode>(startPos, std::move(condNode), std::move(blockNode), true);
    }
    case TRY: {
        unsigned int startPos = START_POS();
        this->consume();    // TRY
        auto tryNode = std::make_unique<TryNode>(startPos, TRY(this->parse_block()));

        // parse catch
        while(CUR_KIND() == CATCH) {
            tryNode->addCatchNode(TRY(this->parse_catchStatement()));
        }

        // parse finally
        if(CUR_KIND() == FINALLY) {
            this->consume();    // FINALLY
            tryNode->addFinallyNode(TRY(this->parse_block()));
        }
        return std::move(tryNode);
    }
    case BREAK: {
        Token token = this->expect(BREAK); // always success
        std::unique_ptr<Node> exprNode;
        if(!HAS_NL()) {
            switch(CUR_KIND()) {
            EACH_LA_expression(GEN_LA_CASE) {
                exprNode = TRY(this->parse_expression());
                break;
            }
            default:
                break;
            }
        }
        return JumpNode::newBreak(token, std::move(exprNode));
    }
    case CONTINUE: {
        Token token = this->expect(CONTINUE);  // always success
        return JumpNode::newContinue(token);
    }
    case RETURN: {
        Token token = this->expect(RETURN); // always success
        std::unique_ptr<Node> exprNode;
        if(!HAS_NL()) {
            switch(CUR_KIND()) {
            EACH_LA_expression(GEN_LA_CASE)
                exprNode = TRY(this->parse_expression());
                break;
            default:
                break;
            }
        }
        return JumpNode::newReturn(token, std::move(exprNode));
    }
    default:
        if(this->inCompletionPoint()) {
            if(this->inVarNameCompletionPoint()) {
                this->ccHandler->addVarNameRequest(this->curToken);
            } else if(!this->inCompletionPointAt(EOS) || this->consumedKind != EOS) {
                this->ccHandler->addCmdOrKeywordRequest(this->curToken, this->inStmtCompCtx);
            }
        }
        E_ALTER(EACH_LA_primary(GEN_LA_ALTER));
    }
}

std::unique_ptr<Node> Parser::parse_arrayBody(Token token, std::unique_ptr<Node> &&firstNode) {
    GUARD_DEEP_NESTING(guard);

    auto arrayNode = std::make_unique<ArrayNode>(token.pos, std::move(firstNode));
    for(bool next = true; next;) {
        switch(CUR_KIND()) {
        case COMMA:
            this->consume();    // COMMA
            if(CUR_KIND() != RB) {
                arrayNode->addExprNode(TRY(this->parse_expression()));
            }
            break;
        case RB:
            next = false;
            break;
        default:
            E_ALTER_OR_COMP(COMMA, RB);
        }
    }
    return arrayNode;
}

std::unique_ptr<Node> Parser::parse_mapBody(Token token, std::unique_ptr<Node> &&keyNode) {
    GUARD_DEEP_NESTING(guard);

    this->expectAndChangeMode(COLON, yycSTMT);  // always success

    auto valueNode = TRY(this->parse_expression());
    auto mapNode = std::make_unique<MapNode>(token.pos, std::move(keyNode), std::move(valueNode));
    for(bool next = true; next; ) {
        switch(CUR_KIND()) {
        case COMMA:
            this->consume();    //  COMMA
            if(CUR_KIND() != RB) {
                keyNode = TRY(this->parse_expression());
                TRY(this->expectAndChangeMode(COLON, yycSTMT));
                valueNode = TRY(this->parse_expression());
                mapNode->addEntry(std::move(keyNode), std::move(valueNode));
            }
            break;
        case RB:
            next = false;
            break;
        default:
            E_ALTER_OR_COMP(COMMA, RB);
        }
    }
    return mapNode;
}

std::unique_ptr<Node> Parser::parse_signalLiteral() {
    assert(CUR_KIND() == SIGNAL_LITERAL);
    Token token = this->expect(SIGNAL_LITERAL); // always success
    auto ref = this->lexer->toStrRef(token);
    ref.removePrefix(2);    // skip prefix [%']
    ref.removeSuffix(1);    // skip suffix [']
    int num = getSignalNum(ref);
    if(num < 0) {
        reportTokenFormatError(SIGNAL_LITERAL, token, "unsupported signal");
        return nullptr;
    }
    return NumberNode::newSignal(token, num);
}

std::unique_ptr<Node> Parser::parse_appliedName(bool asSpecialName) {
    Token token = TRY(this->expect(asSpecialName ? SPECIAL_NAME : APPLIED_NAME));
    return this->newVarNode(token);
}

std::unique_ptr<Node> Parser::parse_stringLiteral() {
    assert(CUR_KIND() == STRING_LITERAL);
    Token token = this->expect(STRING_LITERAL); // always success
    std::string str;
    bool s = this->lexer->singleToString(token, str);
    if(!s) {
        reportTokenFormatError(STRING_LITERAL, token, "illegal escape sequence");
        return nullptr;
    }
    return std::make_unique<StringNode>(token, std::move(str));
}

std::unique_ptr<Node> Parser::parse_regexLiteral() {
    Token token = this->expect(REGEX_LITERAL);  // always success
    std::string str = this->lexer->toTokenText(token.sliceFrom(2)); // skip prefix '$/'
    const char *ptr = strrchr(str.c_str(), '/');
    assert(ptr);
    std::string flag = ptr + 1;
    for(; str.back() != '/'; str.pop_back());
    str.pop_back(); // skip suffix '/'
    return std::make_unique<RegexNode>(token, std::move(str), std::move(flag));
}

ArgsWrapper Parser::parse_arguments(Token first) {
    GUARD_DEEP_NESTING(guard);

    Token token = first.size == 0 ? TRY(this->expect(LP)) : first;

    ArgsWrapper args(token.pos);
    for(unsigned int count = 0; CUR_KIND() != RP; count++) {
        if(count > 0) {
            if(CUR_KIND() != COMMA) {
                E_ALTER(COMMA, RP);
            }
            this->consume();    // COMMA
        }
        switch(CUR_KIND()) {
        EACH_LA_expression(GEN_LA_CASE)
            args.addArgNode(TRY(this->parse_expression()));
            break;
        default:
            E_ALTER(EACH_LA_expression(GEN_LA_ALTER) RP);
        }
    }
    token = this->expect(RP);   // always success
    args.updateToken(token);
    return args;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == OPEN_DQUOTE);
    Token token = this->expect(OPEN_DQUOTE);   // always success
    auto node = std::make_unique<StringExprNode>(token.pos);

    for(bool next = true; next;) {
        switch(CUR_KIND()) {
        case STR_ELEMENT: {
            token = this->expect(STR_ELEMENT);  // always success
            node->addExprNode(
                    std::make_unique<StringNode>(token, this->lexer->doubleElementToString(token)));
            break;
        }
        EACH_LA_interpolation(GEN_LA_CASE) {
            auto interp = TRY(this->parse_interpolation(EmbedNode::STR_EXPR));
            node->addExprNode(std::move(interp));
            break;
        }
        case START_SUB_CMD: {
            auto subNode = TRY(this->parse_cmdSubstitution(true));
            node->addExprNode(std::move(subNode));
            break;
        }
        case CLOSE_DQUOTE:
            next = false;
            break;
        default:
#define EACH_LA_stringExpression(OP) \
    OP(STR_ELEMENT)                  \
    EACH_LA_interpolation(OP) \
    OP(START_SUB_CMD)                \
    OP(CLOSE_DQUOTE)

            if(this->inVarNameCompletionPoint()) {
                this->ccHandler->addVarNameRequest(this->curToken);
            } else if(this->inCompletionPointAt(EOS)) {
                TokenKind kinds[] = { EACH_LA_stringExpression(GEN_LA_ALTER) };
                this->ccHandler->addExpectedTokenRequests(kinds);
            }
            E_ALTER(EACH_LA_stringExpression(GEN_LA_ALTER));

#undef EACH_LA_stringExpression
        }
    }

    token = TRY(this->expect(CLOSE_DQUOTE));
    node->updateToken(token);
    return std::move(node);
}

std::unique_ptr<Node> Parser::toAccessNode(Token token) const {
    std::unique_ptr<Node> node;
    std::vector<std::unique_ptr<VarNode>> nodes;

    const char *ptr = this->lexer->toStrRef(token).data();
    for(unsigned int index = token.size - 1; index != 0; index--) {
        if(ptr[index] == '.') {
            Token fieldToken = token.sliceFrom(index + 1);
            nodes.push_back(this->newVarNode(fieldToken));
            token = token.slice(0, index);
        }
    }
    node = this->newVarNode(token);
    for(; !nodes.empty(); nodes.pop_back()) {
        node = std::make_unique<AccessNode>(std::move(node), std::move(nodes.back()));
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_interpolation(EmbedNode::Kind kind) {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case APPLIED_NAME:
    case SPECIAL_NAME: {
        auto node = this->parse_appliedName(CUR_KIND() == SPECIAL_NAME);
        return std::make_unique<EmbedNode>(kind, std::move(node));
    }
    case APPLIED_NAME_WITH_FIELD: {
        const Token token = this->expect(APPLIED_NAME_WITH_FIELD);

        // split '${recv.field1.field2}'
        // split begin token '${'
        Token beginToken = token.slice(0, 2);

        // split inner names
        Token innerToken = token.slice(2, token.size - 1);

        // split end token '}'
        Token endToken = token.sliceFrom(token.size - 1);

        return std::make_unique<EmbedNode>(beginToken.pos, kind, this->toAccessNode(innerToken), endToken);
    }
    default:
        unsigned int pos = START_POS();
        TRY(this->expect(START_INTERP));
        auto node = TRY(this->parse_expression());
        auto endToken = TRY(this->expect(RBC));
        return std::make_unique<EmbedNode>(pos, kind, std::move(node), endToken);
    }
}

std::unique_ptr<Node> Parser::parse_paramExpansion() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case APPLIED_NAME_WITH_BRACKET:
    case SPECIAL_NAME_WITH_BRACKET: {   // $name[
        Token token = this->curToken;
        this->consume();    // always success
        auto varNode = this->newVarNode(token);
        auto indexNode = TRY(this->parse_expression());
        Token opToken = token.sliceFrom(token.size - 1);    // last ']'

        token = TRY(this->expect(RB));
        auto node = ApplyNode::newIndexCall(std::move(varNode), opToken, std::move(indexNode));
        node->updateToken(token);
        return std::make_unique<EmbedNode>(EmbedNode::CMD_ARG, std::move(node));
    }
    case APPLIED_NAME_WITH_PAREN: { // $func(
        Token token = this->curToken;
        this->consume();    // always success
        auto varNode = this->newVarNode(token.slice(0, token.size - 1));

        auto args = TRY(this->parse_arguments(token.sliceFrom(token.size - 1)));
        token = args.getToken();
        auto node = std::make_unique<ApplyNode>(std::move(varNode), args.take(), ApplyNode::FUNC_CALL);
        node->updateToken(token);
        return std::make_unique<EmbedNode>(EmbedNode::CMD_ARG, std::move(node));
    }
    default:
        return this->parse_interpolation(EmbedNode::CMD_ARG);
    }
}

std::unique_ptr<Node> Parser::parse_cmdSubstitution(bool strExpr) {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == START_SUB_CMD);
    unsigned int pos = START_POS();
    this->consume();    // START_SUB_CMD
    auto exprNode = TRY(this->parse_expression());
    Token token = TRY(this->expect(RP));
    return ForkNode::newCmdSubstitution(pos, std::move(exprNode), token, strExpr);
}

std::unique_ptr<Node> Parser::parse_procSubstitution() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == START_IN_SUB || CUR_KIND() == START_OUT_SUB);
    unsigned int pos = START_POS();
    bool inPipe = this->scan() == START_IN_SUB;
    auto exprNode = TRY(this->parse_expression());
    Token token = TRY(this->expect(RP));
    return ForkNode::newProcSubstitution(pos, std::move(exprNode), token, inPipe);
}

} // namespace ydsh
