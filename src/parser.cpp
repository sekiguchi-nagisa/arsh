/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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
#include "symbol.h"
#include "signals.h"
#include "misc/util.hpp"

// helper macro
#define HAS_NL() (this->lexer->isPrevNewLine())

#define HAS_SPACE() (this->lexer->isPrevSpace())

#define CUR_KIND() (this->curKind)

#define START_POS() (this->curToken.pos)

#define GEN_LA_CASE(CASE) case CASE:
#define GEN_LA_ALTER(CASE) CASE,

// for lookahead
#define EACH_LA_interpolation(OP) \
    OP(APPLIED_NAME) \
    OP(SPECIAL_NAME) \
    OP(START_INTERP)

#define EACH_LA_paramExpansion(OP) \
    OP(APPLIED_NAME_WITH_BRACKET) \
    OP(SPECIAL_NAME_WITH_BRACKET) \
    EACH_LA_interpolation(OP)

#define EACH_LA_primary(OP) \
    OP(COMMAND) \
    OP(NEW) \
    OP(BYTE_LITERAL) \
    OP(INT16_LITERAL) \
    OP(UINT16_LITERAL) \
    OP(INT32_LITERAL) \
    OP(UINT32_LITERAL) \
    OP(INT64_LITERAL) \
    OP(UINT64_LITERAL) \
    OP(FLOAT_LITERAL) \
    OP(STRING_LITERAL) \
    OP(PATH_LITERAL) \
    OP(REGEX_LITERAL) \
    OP(SIGNAL_LITERAL) \
    OP(OPEN_DQUOTE) \
    OP(START_SUB_CMD) \
    OP(APPLIED_NAME) \
    OP(SPECIAL_NAME) \
    OP(LP) \
    OP(LB) \
    OP(LBC) \
    OP(DO) \
    OP(FOR) \
    OP(IF) \
    OP(TRY) \
    OP(WHILE)

#define EACH_LA_expression(OP) \
    OP(NOT) \
    OP(PLUS) \
    OP(MINUS) \
    OP(THROW) \
    EACH_LA_primary(OP)

#define EACH_LA_statement(OP) \
    OP(FUNCTION) \
    OP(INTERFACE) \
    OP(ALIAS) \
    OP(ASSERT) \
    OP(BREAK) \
    OP(CONTINUE) \
    OP(EXPORT_ENV) \
    OP(IMPORT_ENV) \
    OP(LET) \
    OP(RETURN) \
    OP(VAR) \
    OP(LINE_END) \
    EACH_LA_expression(OP)

#define EACH_LA_varDecl(OP) \
    OP(VAR) \
    OP(LET)

#define EACH_LA_redirFile(OP) \
    OP(REDIR_IN_2_FILE) \
    OP(REDIR_OUT_2_FILE) \
    OP(REDIR_OUT_2_FILE_APPEND) \
    OP(REDIR_ERR_2_FILE) \
    OP(REDIR_ERR_2_FILE_APPEND) \
    OP(REDIR_MERGE_ERR_2_OUT_2_FILE) \
    OP(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND) \
    OP(REDIR_HERE_STR)

#define EACH_LA_redirNoFile(OP) \
    OP(REDIR_MERGE_ERR_2_OUT) \
    OP(REDIR_MERGE_OUT_2_ERR)

#define EACH_LA_redir(OP) \
    EACH_LA_redirFile(OP) \
    EACH_LA_redirNoFile(OP)

#define EACH_LA_cmdArg(OP) \
    OP(CMD_ARG_PART) \
    OP(STRING_LITERAL) \
    OP(OPEN_DQUOTE) \
    OP(START_SUB_CMD) \
    EACH_LA_paramExpansion(OP)


#define E_ALTER(...) \
do { this->raiseNoViableAlterError((TokenKind[]) { __VA_ARGS__ }); return nullptr; } while(false)

#define PRECEDENCE() getPrecedence(CUR_KIND())

#define TRY(expr) \
({ auto v = expr; if(this->hasError()) { return nullptr; } std::forward<decltype(v)>(v); })


namespace ydsh {

struct CallCounter {
    unsigned int &count;

    explicit CallCounter(unsigned int &count) : count(count) {}

    ~CallCounter() {
        --this->count;
    }
};

#define GUARD_DEEP_NESTING(name) \
if(++this->callCount == MAX_NESTING_DEPTH) { this->raiseDeepNestingError(); return nullptr; } \
CallCounter name(this->callCount); \
(void) name
//fprintf(stderr, "depth: %d\n", name.count)


// #########################
// ##     ArgsWrapper     ##
// #########################

ArgsWrapper::~ArgsWrapper() {
    for(Node *n : this->nodes) {
        delete n;
    }
}

void ArgsWrapper::addArgNode(std::unique_ptr<Node> &&node) {
    nodes.push_back(node.release());
}


// ####################
// ##     Parser     ##
// ####################

std::unique_ptr<RootNode> Parser::operator()() {
    auto rootNode = make_unique<RootNode>();
    rootNode->setSourceInfoPtr(this->lexer->getSourceInfoPtr());

    // start parsing
    while(CUR_KIND() != EOS) {
        auto node = TRY(this->parse_statement());
        rootNode->addNode(node.release());
    }
    TRY(this->expect(EOS));

    return rootNode;
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

bool Parser::expectAndChangeMode(TokenKind kind, LexerMode mode, bool fetchNext) {
    bool r = this->expect(kind, false);
    if(!this->hasError()) {
        this->lexer->setLexerMode(mode);
        if(fetchNext) {
            this->fetchNext();
        }
    }
    return r;
}

void Parser::raiseTokenFormatError(TokenKind kind, Token token, const char *msg) {
    std::string message(msg);
    message += ": ";
    message += toString(kind);

    this->createError(kind, token, "TokenFormat", std::move(message));
}

void Parser::raiseDeepNestingError() {
    std::string message = "parser recursion depth exceeded";
    this->createError(this->curKind, this->curToken, "DeepNesting", std::move(message));
}


// parse rule definition
std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == FUNCTION);
    unsigned int startPos = START_POS();
    this->consume();    // FUNCTION
    Token token = TRY(this->expectAndGet(IDENTIFIER));
    auto node = make_unique<FunctionNode>(startPos, this->lexer->getSourceInfoPtr(), this->lexer->toName(token));
    TRY(this->expect(LP));

    if(CUR_KIND() == APPLIED_NAME) {
        token = this->expectAndGet(APPLIED_NAME); // always success
        auto nameNode = make_unique<VarNode>(token, this->lexer->toName(token));
        TRY(this->expect(COLON, false));

        auto type = TRY(this->parse_typeName());

        node->addParamNode(nameNode.release(), type.release());

        while(true) {
            if(CUR_KIND() == COMMA) {
                this->consume();    // COMMA
                token = TRY(this->expectAndGet(APPLIED_NAME));

                nameNode = make_unique<VarNode>(token, this->lexer->toName(token));

                TRY(this->expect(COLON, false));

                type = TRY(this->parse_typeName());

                node->addParamNode(nameNode.release(), type.release());
            } else if(CUR_KIND() == RP) {
                break;
            } else {
                E_ALTER(COMMA, RP);
            }
        }
    } else if(CUR_KIND() != RP) {
        E_ALTER(APPLIED_NAME, RP);
    }

    node->updateToken(this->curToken);
    TRY(this->expect(RP));

    std::unique_ptr<TypeNode> retTypeNode;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false); // always success
        auto type = make_unique<ReturnTypeNode>(TRY(this->parse_typeName()).release());
        while(CUR_KIND() == COMMA) {
            this->expectAndChangeMode(COMMA, yycEXPR, false); // always success
            type->addTypeNode(TRY(this->parse_typeName()).release());
        }
        retTypeNode = std::move(type);
    }
    if(!retTypeNode) {
        retTypeNode.reset(newVoidTypeNode());
    }
    node->setReturnTypeToken(retTypeNode.release());

    return node;
}

std::unique_ptr<Node> Parser::parse_interface() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == INTERFACE);
    unsigned int startPos = START_POS();

    this->expect(INTERFACE, false); // always success

    // enter TYPE mode
    this->pushLexerMode(yycTYPE);

    Token token = TRY(this->expectAndGet(TYPE_PATH));

    // exit TYPE mode
    this->restoreLexerState(token);

    auto node = make_unique<InterfaceNode>(startPos, this->lexer->toTokenText(token));
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
            auto readOnly = this->consumeAndGet() == LET ? VarDeclNode::CONST : VarDeclNode::VAR;
            token = TRY(this->expectAndGet(IDENTIFIER));
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

    token = TRY(this->expectAndGet(RBC));
    node->updateToken(token);

    return std::move(node);
}

TypeWrapper Parser::parse_basicOrReifiedType(Token token) {
    GUARD_DEEP_NESTING(guard);

    auto typeToken = make_unique<BaseTypeNode>(token, this->lexer->toName(token));
    if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
        this->expect(TYPE_OPEN, false); // always success

        auto reified = make_unique<ReifiedTypeNode>(typeToken.release());
        reified->addElementTypeNode(TRY(this->parse_typeName()).release());

        while(CUR_KIND() == TYPE_SEP) {
            this->expect(TYPE_SEP, false);  // always success
            reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        }
        token = TRY(this->expectAndGet(TYPE_CLOSE));
        reified->updateToken(token);

        return {std::move(reified), token};
    }

    return {std::move(typeToken), token};
}

TypeWrapper Parser::parse_typeNameImpl() {
    // change lexer state to TYPE
    this->pushLexerMode(yycTYPE);

    switch(CUR_KIND()) {
    case IDENTIFIER: {
        Token token = this->expectAndGet(IDENTIFIER); // always success
        return this->parse_basicOrReifiedType(token);
    }
    case PTYPE_OPEN: {
        Token token = this->expectAndGet(PTYPE_OPEN, false);  // always success
        auto reified = make_unique<ReifiedTypeNode>(new BaseTypeNode(token, TYPE_TUPLE));
        reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        while(CUR_KIND() == TYPE_SEP) {
            this->expect(TYPE_SEP, false);  // always success
            reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        }
        token = TRY(this->expectAndGet(PTYPE_CLOSE));
        reified->updateToken(token);
        return {std::move(reified), token};
    }
    case ATYPE_OPEN: {
        Token token = this->expectAndGet(ATYPE_OPEN, false);  // always success
        auto left = TRY(this->parse_typeName());
        bool isMap = CUR_KIND() == TYPE_MSEP;
        auto reified = make_unique<ReifiedTypeNode>(new BaseTypeNode(token, isMap ? TYPE_MAP : TYPE_ARRAY));
        reified->addElementTypeNode(left.release());
        if(isMap) {
            this->expect(TYPE_MSEP, false); // always success
            reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        }
        token = TRY(this->expectAndGet(ATYPE_CLOSE));
        reified->updateToken(token);
        return {std::move(reified), token};
    }
    case TYPEOF: {
        Token token = this->expectAndGet(TYPEOF); // always success
        if(CUR_KIND() == PTYPE_OPEN) {
            this->expect(PTYPE_OPEN, false);    // always success
            this->pushLexerMode(yycSTMT);

            unsigned int startPos = token.pos;
            auto exprNode(TRY(this->parse_expression()));

            token = TRY(this->expectAndGet(RP, false));

            return {make_unique<TypeOfNode>(startPos, std::move(exprNode).release()), token};
        }
        return this->parse_basicOrReifiedType(token);
    }
    case FUNC: {
        Token token = this->expectAndGet(FUNC);   // always success
        if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
            this->expect(TYPE_OPEN, false); // always success

            // parse return type
            auto func = make_unique<FuncTypeNode>(token.pos, TRY(this->parse_typeName()).release());

            if(CUR_KIND() == TYPE_SEP) {   // ,[
                this->consume();    // TYPE_SEP
                TRY(this->expect(ATYPE_OPEN, false));

                // parse first arg type
                func->addParamTypeNode(TRY(this->parse_typeName()).release());

                // rest arg type
                while(CUR_KIND() == TYPE_SEP) {
                    this->expect(TYPE_SEP, false);  // always success
                    func->addParamTypeNode(TRY(this->parse_typeName()).release());
                }
                TRY(this->expect(ATYPE_CLOSE));
            }

            token = TRY(this->expectAndGet(TYPE_CLOSE));
            func->updateToken(token);

            return {std::move(func), token};
        }
        return {make_unique<BaseTypeNode>(token, this->lexer->toName(token)), token};
    }
    case TYPE_PATH: {
        Token token = this->expectAndGet(TYPE_PATH);  // always success
        return {make_unique<DBusIfaceTypeNode>(token, this->lexer->toTokenText(token)), token};
    }
    default:
        E_ALTER(
                IDENTIFIER,
                PTYPE_OPEN,
                ATYPE_OPEN,
                FUNC,
                TYPEOF,
                TYPE_PATH
        );
    }
}

std::unique_ptr<TypeNode> Parser::parse_typeName() {
    GUARD_DEEP_NESTING(guard);

    auto result = TRY(this->parse_typeNameImpl());
    if(!HAS_NL() && CUR_KIND() == TYPE_OPT) {
        result.token = this->expectAndGet(TYPE_OPT); // always success
        auto reified = make_unique<ReifiedTypeNode>(new BaseTypeNode(result.token, TYPE_OPTION));
        reified->setPos(result.typeNode->getPos());
        reified->addElementTypeNode(result.typeNode.release());
        reified->updateToken(result.token);
        result.typeNode = std::move(reified);
    }

    this->restoreLexerState(result.token);
    return std::move(result.typeNode);
}

std::unique_ptr<Node> Parser::parse_statementImp() {
    if(this->lexer->getPrevMode() != yycSTMT) {
        this->refetch(yycSTMT);
    }

    switch(CUR_KIND()) {
    case LINE_END: {
//        Token token = this->expect(LINE_END);
        Token token = this->curToken;   // not consume LINE_END token
        return make_unique<EmptyNode>(token);
    }
    case FUNCTION: {
        auto node = TRY(this->parse_funcDecl());
        node->setBlockNode(TRY(this->parse_block()).release());
        return std::move(node);
    }
    case INTERFACE: {
        return this->parse_interface();
    }
    case ALIAS: {
        unsigned int startPos = START_POS();
        this->consume();    // ALIAS
        Token token = TRY(this->expectAndGet(IDENTIFIER));
        TRY(this->expect(ASSIGN, false));
        auto typeToken = TRY(this->parse_typeName());
        return make_unique<TypeAliasNode>(startPos, this->lexer->toTokenText(token), typeToken.release());
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
            messageNode = make_unique<StringNode>(std::move(msg));
        }

        return make_unique<AssertNode>(pos, condNode.release(), messageNode.release());
    }
    case BREAK: {
        Token token = this->expectAndGet(BREAK); // always success
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
        return make_unique<JumpNode>(token, true, exprNode.release());
    }
    case CONTINUE: {
        Token token = this->expectAndGet(CONTINUE);  // always success
        return make_unique<JumpNode>(token, false);
    }
    case EXPORT_ENV: {
        unsigned int startPos = START_POS();
        this->consume();    // EXPORT_ENV
        Token token = TRY(this->expectAndGet(IDENTIFIER));
        std::string name(this->lexer->toName(token));
        TRY(this->expect(ASSIGN));
        return make_unique<VarDeclNode>(startPos, std::move(name),
                                        TRY(this->parse_expression()).release(), VarDeclNode::EXPORT_ENV);
    }
    case IMPORT_ENV: {
        unsigned int startPos = START_POS();
        this->consume();    // IMPORT_ENV
        Token token = TRY(this->expectAndGet(IDENTIFIER));
        std::unique_ptr<Node> exprNode;
        if(!HAS_NL() && CUR_KIND() == COLON) {
            TRY(this->expectAndChangeMode(COLON, yycSTMT));
            exprNode = TRY(this->parse_expression());
        }

        auto node = make_unique<VarDeclNode>(startPos, this->lexer->toName(token),
                                             exprNode.release(), VarDeclNode::IMPORT_ENV);
        node->updateToken(token);
        return std::move(node);
    }
    case RETURN: {
        Token token = this->expectAndGet(RETURN); // always success
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
        return make_unique<ReturnNode>(token, exprNode.release());
    }
    EACH_LA_varDecl(GEN_LA_CASE) {
        return this->parse_variableDeclaration();
    }
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        E_ALTER(EACH_LA_statement(GEN_LA_ALTER));
    }
}

std::unique_ptr<Node> Parser::parse_statement() {
    GUARD_DEEP_NESTING(guard);

    auto node = TRY(this->parse_statementImp());
    TRY(this->parse_statementEnd());
    return node;
}

std::unique_ptr<Node> Parser::parse_statementEnd() {
    switch(CUR_KIND()) {
    case EOS:
    case RBC:
        break;
    case LINE_END:
        this->consume();
        break;
    default:
        if(!HAS_NL()) {
            this->raiseTokenMismatchedError(NEW_LINE);
        }
        break;
    }
    return nullptr;
}

std::unique_ptr<BlockNode> Parser::parse_block() {
    GUARD_DEEP_NESTING(guard);

    Token token = TRY(this->expectAndGet(LBC));
    auto blockNode = make_unique<BlockNode>(token.pos);
    while(CUR_KIND() != RBC) {
        blockNode->addNode(TRY(this->parse_statement()).release());
    }
    token = TRY(this->expectAndGet(RBC));
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

    Token token = TRY(this->expectAndGet(IDENTIFIER));
    std::string name = this->lexer->toName(token);
    TRY(this->expect(ASSIGN));
    return make_unique<VarDeclNode>(startPos, std::move(name), TRY(this->parse_expression()).release(), readOnly);
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
    } else if(CUR_KIND() == ELSE) {
        this->consume();    // ELSE
        elseNode = TRY(this->parse_block());
    }
    return make_unique<IfNode>(startPos, condNode.release(), thenNode.release(), elseNode.release());
}

std::unique_ptr<Node> Parser::parse_forExpression() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == FOR);
    unsigned int startPos = START_POS();
    this->consume();    // FOR

    if(CUR_KIND() == LP) {  // for
        this->consume();    // LP

        auto initNode = TRY(this->parse_forInit());
        TRY(this->expect(LINE_END));

        auto condNode = TRY(this->parse_forCond());
        TRY(this->expect(LINE_END));

        auto iterNode = TRY(this->parse_forIter());

        TRY(this->expect(RP));
        auto blockNode = TRY(this->parse_block());

        return make_unique<LoopNode>(startPos, initNode.release(), condNode.release(),
                                     iterNode.release(), blockNode.release());
    }
    // for-in
    Token token = TRY(this->expectAndGet(APPLIED_NAME));
    TRY(this->expect(IN));
    auto exprNode = TRY(this->parse_expression());
    auto blockNode = TRY(this->parse_block());

    return std::unique_ptr<Node>(
            createForInNode(startPos, this->lexer->toName(token), exprNode.release(), blockNode.release()));
}

std::unique_ptr<Node> Parser::parse_forInit() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        return this->parse_variableDeclaration();
    }
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return make_unique<EmptyNode>();
    }
}

std::unique_ptr<Node> Parser::parse_forCond() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        Token token{0, 0};
        return make_unique<VarNode>(token, std::string(VAR_TRUE));
    }
}

std::unique_ptr<Node> Parser::parse_forIter() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return make_unique<EmptyNode>();
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

    Token token = TRY(this->expectAndGet(APPLIED_NAME));
    std::unique_ptr<TypeNode> typeToken;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false); // always success
        typeToken = TRY(this->parse_typeName());
    }

    if(paren) {
        TRY(this->expect(RP));
    }

    auto blockNode = TRY(this->parse_block());
    return make_unique<CatchNode>(startPos, this->lexer->toName(token), typeToken.release(), blockNode.release());
}

// command
std::unique_ptr<Node> Parser::parse_command() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == COMMAND);
    Token token = this->expectAndGet(COMMAND);   // always success

    if(CUR_KIND() == LP) {  // command definition
        this->consume();    // LP
        TRY(this->expect(RP));
        auto blockNode = TRY(this->parse_block());
        return make_unique<UserDefinedCmdNode>(
                token.pos, this->lexer->getSourceInfoPtr(), this->lexer->toCmdArg(token), blockNode.release());
    }

    auto kind = this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
    auto node = make_unique<CmdNode>(new StringNode(token, this->lexer->toCmdArg(token), kind));

    for(bool next = true; next && HAS_SPACE();) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            node->addArgNode(TRY(this->parse_cmdArg()).release());
            break;
        }
        EACH_LA_redir(GEN_LA_CASE) {
            node->addRedirNode(TRY(this->parse_redirOption()).release());
            break;
        }
        case INVALID: {
#define EACH_LA_cmdArgs(E) \
            EACH_LA_cmdArg(E) \
            EACH_LA_redir(E)

            E_ALTER(EACH_LA_cmdArgs(GEN_LA_ALTER));
#undef EACH_LA_cmdArgs
        }
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
        TokenKind kind = this->consumeAndGet();
        return make_unique<RedirNode>(kind, TRY(this->parse_cmdArg()).release());
    }
    EACH_LA_redirNoFile(GEN_LA_CASE) {
        Token token = this->curToken;
        TokenKind kind = this->consumeAndGet();
        return make_unique<RedirNode>(kind, token);
    }
    default:
        E_ALTER(EACH_LA_redir(GEN_LA_ALTER));
    }
}

std::unique_ptr<CmdArgNode> Parser::parse_cmdArg() {
    GUARD_DEEP_NESTING(guard);

    auto node = make_unique<CmdArgNode>(TRY(this->parse_cmdArgSeg(0)).release());

    unsigned int pos = 1;
    for(bool next = true; !HAS_SPACE() && next; pos++) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            node->addSegmentNode(TRY(this->parse_cmdArgSeg(pos)).release());
            break;
        }
        default:
            next = false;
            break;
        }
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_cmdArgSeg(unsigned int pos) {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case CMD_ARG_PART: {
        Token token = this->expectAndGet(CMD_ARG_PART);   // always success
        auto kind = pos == 0 && this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
        return make_unique<StringNode>(token, this->lexer->toCmdArg(token), kind);
    }
    case STRING_LITERAL: {
        return this->parse_stringLiteral();
    }
    case OPEN_DQUOTE: {
        return this->parse_stringExpression();
    }
    case START_SUB_CMD: {
        return this->parse_substitution();
    }
    EACH_LA_paramExpansion(GEN_LA_CASE) {
        return this->parse_paramExpansion();
    }
    default:
        E_ALTER(EACH_LA_cmdArg(GEN_LA_ALTER));
    }
}

// expression
std::unique_ptr<Node> Parser::parse_expression() {
    GUARD_DEEP_NESTING(guard);

    if(CUR_KIND() == THROW) {
        unsigned int startPos = START_POS();
        this->consume();    // THROW
        return make_unique<ThrowNode>(startPos, TRY(this->parse_expression()).release());
    }
    return this->parse_binaryExpression(
            TRY(this->parse_unaryExpression()), getPrecedence(ASSIGN));
}

static std::unique_ptr<Node> createBinaryNode(std::unique_ptr<Node> &&leftNode,
                                              TokenKind op, std::unique_ptr<Node> &&rightNode) {
    if(op == PIPE) {
        if(leftNode->is(NodeKind::Pipeline)) {
            static_cast<PipelineNode *>(leftNode.get())->addNode(rightNode.release());
            return std::move(leftNode);
        }
        return make_unique<PipelineNode>(leftNode.release(), rightNode.release());
    }
    if(isAssignOp(op)) {
        return std::unique_ptr<Node>(createAssignNode(leftNode.release(), op, rightNode.release()));
    }

    return make_unique<BinaryOpNode>(leftNode.release(), op, rightNode.release());
}

std::unique_ptr<Node> Parser::parse_binaryExpression(std::unique_ptr<Node> &&leftNode,
                                                     unsigned int basePrecedence) {
    GUARD_DEEP_NESTING(guard);

    std::unique_ptr<Node> node(std::move(leftNode));
    for(unsigned int p = PRECEDENCE();
        !HAS_NL() && p >= basePrecedence; p = PRECEDENCE()) {
        switch(CUR_KIND()) {
        case AS: {
            this->expect(AS, false);    // always success
            auto type = TRY(this->parse_typeName());
            node = make_unique<TypeOpNode>(node.release(), type.release(), TypeOpNode::NO_CAST);
            break;
        }
        case IS: {
            this->expect(IS, false);   // always success
            auto type = TRY(this->parse_typeName());
            node = make_unique<TypeOpNode>(node.release(), type.release(), TypeOpNode::ALWAYS_FALSE);
            break;
        }
        case WITH: {
            this->consume();    // WITH
            auto redirNode = TRY(this->parse_redirOption());
            auto withNode = make_unique<WithNode>(node.release(), redirNode.release());
            for(bool next = true; next && HAS_SPACE();) {
                switch(CUR_KIND()) {
                EACH_LA_redir(GEN_LA_CASE) {
                    withNode->addRedirNode(TRY(this->parse_redirOption()).release());
                    break;
                }
                case INVALID: {
                    E_ALTER(EACH_LA_redir(GEN_LA_ALTER));
                }
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
            auto tleftNode = TRY(this->parse_expression());
            TRY(this->expectAndChangeMode(COLON, yycSTMT));
            auto trightNode = TRY(this->parse_expression());
            unsigned int pos = node->getPos();
            node = make_unique<IfNode>(pos, node.release(), tleftNode.release(), trightNode.release());
            break;
        }
        default: {
            TokenKind op = this->consumeAndGet();
            auto rightNode = TRY(this->parse_unaryExpression());
            for(unsigned int nextP = PRECEDENCE();
                !HAS_NL() && (nextP > p || (nextP == p && isAssignOp(op))); nextP = PRECEDENCE()) {
                rightNode = TRY(this->parse_binaryExpression(std::move(rightNode), nextP));
            }
            node = createBinaryNode(std::move(node), op, std::move(rightNode));
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
        unsigned int startPos = START_POS();
        TokenKind op = this->consumeAndGet();
        return make_unique<UnaryOpNode>(startPos, op, TRY(this->parse_unaryExpression()).release());
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
            Token token = TRY(this->expectAndGet(IDENTIFIER));
            std::string name = this->lexer->toName(token);
            if(CUR_KIND() == LP && !HAS_NL()) {  // treat as method call
                auto args = TRY(this->parse_arguments());
                Token token = args.getToken();
                node = make_unique<MethodCallNode>(node.release(), std::move(name),
                                                   ArgsWrapper::extract(std::move(args)));
                node->updateToken(token);
            } else {    // treat as field access
                node = make_unique<AccessNode>(node.release(), new VarNode(token, std::move(name)));
                node->updateToken(token);
            }
            break;
        }
        case LB: {
            this->consume();    // LB
            auto indexNode = TRY(this->parse_expression());
            auto token = TRY(this->expectAndGet(RB));
            node.reset(createIndexNode(node.release(), indexNode.release()));
            node->updateToken(token);
            break;
        }
        case LP: {
            auto args = TRY(this->parse_arguments());
            Token token = args.getToken();
            node = make_unique<ApplyNode>(node.release(), ArgsWrapper::extract(std::move(args)));
            node->updateToken(token);
            break;
        }
        case INC:
        case DEC: {
            Token token = this->curToken;
            TokenKind op = this->consumeAndGet();
            node.reset(createSuffixNode(node.release(), op, token));
            break;
        }
        case UNWRAP: {
            Token token = this->curToken;
            TokenKind op = this->consumeAndGet(); // UNWRAP
            unsigned int pos = node->getPos();
            node = make_unique<UnaryOpNode>(pos, op, node.release());
            node->updateToken(token);
            break;
        }
        default:
            next = false;
            break;
        }
    }
    return node;
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
        auto node = make_unique<NewNode>(startPos, type.release(), ArgsWrapper::extract(std::move(args)));
        node->updateToken(token);
        return std::move(node);
    }
    case BYTE_LITERAL: {
        auto pair = TRY(this->expectNum(BYTE_LITERAL, &Lexer::toUint8));
        return std::unique_ptr<Node>(NumberNode::newByte(pair.first, pair.second));
    }
    case INT16_LITERAL: {
        auto pair = TRY(this->expectNum(INT16_LITERAL, &Lexer::toInt16));
        return std::unique_ptr<Node>(NumberNode::newInt16(pair.first, pair.second));
    }
    case UINT16_LITERAL: {
        auto pair = TRY(this->expectNum(UINT16_LITERAL, &Lexer::toUint16));
        return std::unique_ptr<Node>(NumberNode::newUint16(pair.first, pair.second));
    }
    case INT32_LITERAL: {
        auto pair = TRY(this->expectNum(INT32_LITERAL, &Lexer::toInt32));
        return std::unique_ptr<Node>(NumberNode::newInt32(pair.first, pair.second));
    }
    case UINT32_LITERAL: {
        auto pair = TRY(this->expectNum(UINT32_LITERAL, &Lexer::toUint32));
        return std::unique_ptr<Node>(NumberNode::newUint32(pair.first, pair.second));
    }
    case INT64_LITERAL: {
        auto pair = TRY(this->expectNum(INT64_LITERAL, &Lexer::toInt64));
        return std::unique_ptr<Node>(NumberNode::newInt64(pair.first, pair.second));
    }
    case UINT64_LITERAL: {
        auto pair = TRY(this->expectNum(UINT64_LITERAL, &Lexer::toUint64));
        return std::unique_ptr<Node>(NumberNode::newUint64(pair.first, pair.second));
    }
    case FLOAT_LITERAL: {
        auto pair = TRY(this->expectNum(FLOAT_LITERAL, &Lexer::toDouble));
        return std::unique_ptr<Node>(NumberNode::newFloat(pair.first, pair.second));
    }
    case STRING_LITERAL: {
        return this->parse_stringLiteral();
    }
    case PATH_LITERAL: {
        Token token = this->expectAndGet(PATH_LITERAL);   // always success

        /**
         * skip prefix 'p'
         */
        token.pos++;
        token.size--;
        std::string str;
        this->lexer->singleToString(token, str);    // always success
        return make_unique<StringNode>(token, std::move(str), StringNode::OBJECT_PATH);
    }
    case REGEX_LITERAL: {
        Token token = this->expectAndGet(REGEX_LITERAL);  // always success
        auto old = token;

        /**
         * skip prefix '$/'
         */
        token.pos += 2;
        token.size -= 2;
        std::string str = this->lexer->toTokenText(token);

        /**
         * parse regex flag
         */
        int regexFlag = 0;
        while(str.back() != '/') {
            char ch = str.back();
            if(ch == 'i') {
                regexFlag |= PCRE_CASELESS;
            } else if(ch == 'm') {
                regexFlag |= PCRE_MULTILINE;
            }
            str.pop_back();
        }
        str.pop_back(); // skip suffix '/'

        const char *errorStr;
        auto re = compileRegex(str.c_str(), errorStr, regexFlag);
        if(!re) {
            raiseTokenFormatError(REGEX_LITERAL, old, errorStr);
            return nullptr;
        }
        return make_unique<RegexNode>(old, std::move(str), std::move(re));
    }
    case SIGNAL_LITERAL: {
        Token token = this->expectAndGet(SIGNAL_LITERAL); // always success
        auto str = this->lexer->toTokenText(token);
        str.pop_back(); // skip suffix [']
        int num = getSignalNum(str.c_str() + 2); // skip prefix [%']
        if(num < 0) {
            raiseTokenFormatError(SIGNAL_LITERAL, token, "unsupported signal");
            return nullptr;
        }
        return std::unique_ptr<Node>(NumberNode::newSignal(token, num));
    }
    case OPEN_DQUOTE: {
        return this->parse_stringExpression();
    }
    case START_SUB_CMD: {
        return this->parse_substitution();
    }
    case APPLIED_NAME:
    case SPECIAL_NAME: {
        return this->parse_appliedName(CUR_KIND() == SPECIAL_NAME);
    }
    case LP: {  // group or tuple
        Token token = this->expectAndGet(LP); // always success
        auto node = TRY(this->parse_expression());
        if(CUR_KIND() == COMMA) {   // tuple
            this->consume();    // COMMA
            auto tuple = make_unique<TupleNode>(token.pos, node.release());
            if(CUR_KIND() != RP) {
                tuple->addNode(TRY(this->parse_expression()).release());
                while(true) {
                    if(CUR_KIND() == COMMA) {
                        this->consume();    // COMMA
                        tuple->addNode(TRY(this->parse_expression()).release());
                    } else if(CUR_KIND() == RP) {
                        break;
                    } else {
                        E_ALTER(COMMA, RP);
                    }
                }
            }

            node = std::move(tuple);
        } else {
            node->setPos(token.pos);
        }

        token = TRY(this->expectAndGet(RP));
        node->updateToken(token);
        return node;
    }
    case LB: {  // array or map
        Token token = this->expectAndGet(LB); // always success

        auto keyNode = TRY(this->parse_expression());

        std::unique_ptr<Node> node;
        if(CUR_KIND() == COLON) {   // map
            this->expectAndChangeMode(COLON, yycSTMT);  // always success

            auto valueNode = TRY(this->parse_expression());
            auto mapNode = make_unique<MapNode>(token.pos, keyNode.release(), valueNode.release());
            while(true) {
                if(CUR_KIND() == COMMA) {
                    this->consume();    // COMMA
                    if(CUR_KIND() != RB) {
                        keyNode = TRY(this->parse_expression());
                        TRY(this->expectAndChangeMode(COLON, yycSTMT));
                        valueNode = TRY(this->parse_expression());
                        mapNode->addEntry(keyNode.release(), valueNode.release());
                    }
                } else if(CUR_KIND() == RB) {
                    break;
                } else {
                    E_ALTER(COMMA, RB);
                }
            }
            node = std::move(mapNode);
        } else {    // array
            auto arrayNode = make_unique<ArrayNode>(token.pos, keyNode.release());
            while(true) {
                if(CUR_KIND() == COMMA) {
                    this->consume();    // COMMA
                    if(CUR_KIND() != RB) {
                        arrayNode->addExprNode(TRY(this->parse_expression()).release());
                    }
                } else if(CUR_KIND() == RB) {
                    break;
                } else {
                    E_ALTER(COMMA, RB);
                }
            }
            node = std::move(arrayNode);
        }

        token = TRY(this->expectAndGet(RB));
        node->updateToken(token);
        return node;
    }
    case LBC: {
        return this->parse_block();
    }
    case FOR: {
        return this->parse_forExpression();
    }
    case IF: {
        return this->parse_ifExpression();
    }
    case WHILE: {
        unsigned int startPos = START_POS();
        this->consume();    // WHILE
        auto condNode = TRY(this->parse_expression());
        auto blockNode = TRY(this->parse_block());
        return make_unique<LoopNode>(startPos, condNode.release(), blockNode.release());
    }
    case DO: {
        unsigned int startPos = START_POS();
        this->consume();    // DO
        auto blockNode = TRY(this->parse_block());
        TRY(this->expect(WHILE));
        auto condNode = TRY(this->parse_expression());
        return make_unique<LoopNode>(startPos, condNode.release(), blockNode.release(), true);
    }
    case TRY: {
        unsigned int startPos = START_POS();
        this->consume();    // TRY
        auto tryNode = make_unique<TryNode>(startPos, TRY(this->parse_block()).release());

        // parse catch
        while(CUR_KIND() == CATCH) {
            auto catchNode = TRY(this->parse_catchStatement());
            tryNode->addCatchNode(catchNode.release());
        }

        // parse finally
        if(CUR_KIND() == FINALLY) {
            this->consume();    // FINALLY
            auto blockNode = TRY(this->parse_block());
            tryNode->addFinallyNode(blockNode.release());
        }
        return std::move(tryNode);
    }
    default:
        E_ALTER(EACH_LA_primary(GEN_LA_ALTER));
    }
}

std::unique_ptr<Node> Parser::parse_appliedName(bool asSpecialName) {
    Token token = TRY(this->expectAndGet(asSpecialName ? SPECIAL_NAME : APPLIED_NAME));
    return make_unique<VarNode>(token, this->lexer->toName(token));
}

std::unique_ptr<Node> Parser::parse_stringLiteral() {
    assert(CUR_KIND() == STRING_LITERAL);
    Token token = this->expectAndGet(STRING_LITERAL); // always success
    std::string str;
    bool s = this->lexer->singleToString(token, str);
    if(!s) {
        raiseTokenFormatError(STRING_LITERAL, token, "illegal escape sequence");
        return nullptr;
    }
    return make_unique<StringNode>(token, std::move(str));
}

ArgsWrapper Parser::parse_arguments() {
    GUARD_DEEP_NESTING(guard);

    Token token = TRY(this->expectAndGet(LP));

    ArgsWrapper args(token.pos);
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        args.addArgNode(TRY(this->parse_expression()));
        for(bool next = true; next;) {
            if(CUR_KIND() == COMMA) {
                this->consume();    // COMMA
                args.addArgNode(TRY(this->parse_expression()));
            } else if(CUR_KIND() == RP) {
                next = false;
            } else {
                E_ALTER(COMMA, RP);
            }
        }
        break;
    }
    case RP:
        break;
    default:  // no args
        E_ALTER(EACH_LA_expression(GEN_LA_ALTER) RP);
    }

    token = TRY(this->expectAndGet(RP));
    args.updateToken(token);
    return args;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == OPEN_DQUOTE);
    Token token = this->expectAndGet(OPEN_DQUOTE);   // always success
    auto node = make_unique<StringExprNode>(token.pos);

    for(bool next = true; next;) {
        switch(CUR_KIND()) {
        case STR_ELEMENT: {
            token = this->expectAndGet(STR_ELEMENT);  // always success
            node->addExprNode(
                    new StringNode(token, this->lexer->doubleElementToString(token)));
            break;
        }
        EACH_LA_interpolation(GEN_LA_CASE) {
            auto interp = TRY(this->parse_interpolation());
            node->addExprNode(interp.release());
            break;
        }
        case START_SUB_CMD: {
            auto subNode = TRY(this->parse_substitution());
            subNode->setStrExpr(true);
            node->addExprNode(subNode.release());
            break;
        }
        case CLOSE_DQUOTE:
            next = false;
            break;
        default:
            E_ALTER(STR_ELEMENT, EACH_LA_interpolation(GEN_LA_ALTER) START_SUB_CMD, CLOSE_DQUOTE);
        }
    }

    token = TRY(this->expectAndGet(CLOSE_DQUOTE));
    node->updateToken(token);
    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_interpolation() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case APPLIED_NAME:
    case SPECIAL_NAME:
        return this->parse_appliedName(CUR_KIND() == SPECIAL_NAME);
    default:
        TRY(this->expect(START_INTERP));
        auto node = TRY(this->parse_expression());
        TRY(this->expect(RBC));
        return node;
    }
}

std::unique_ptr<Node> Parser::parse_paramExpansion() {
    GUARD_DEEP_NESTING(guard);

    switch(CUR_KIND()) {
    case APPLIED_NAME_WITH_BRACKET:
    case SPECIAL_NAME_WITH_BRACKET: {
        Token token = this->curToken;
        this->consume();    // always success
        auto node = make_unique<VarNode>(token, this->lexer->toName(token));
        auto indexNode = TRY(this->parse_expression());
        TRY(this->expect(RB));
        return std::unique_ptr<Node>(createIndexNode(node.release(), indexNode.release()));
    }
    default:
        return this->parse_interpolation();
    }
}

std::unique_ptr<SubstitutionNode> Parser::parse_substitution() {
    GUARD_DEEP_NESTING(guard);

    assert(CUR_KIND() == START_SUB_CMD);
    unsigned int pos = START_POS();
    this->consume();    // START_SUB_CMD
    auto exprNode = TRY(this->parse_expression());
    Token token = TRY(this->expectAndGet(RP));
    auto node = make_unique<SubstitutionNode>(pos, exprNode.release());
    node->updateToken(token);
    return node;
}

std::unique_ptr<RootNode> parse(const char *sourceName) {
    FILE *fp = fopen(sourceName, "rb");
    if(fp == nullptr) {
        return nullptr;
    }

    Lexer lexer(sourceName, fp);
    Parser parser(lexer);
    return parser();    //FIXME: display error message
}

} // namespace ydsh
