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
    OP(TYPE_ALIAS) \
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

template <typename T, typename ... A>
static inline std::unique_ptr<T> uniquify(A &&... args) {
    return std::unique_ptr<T>(new T(std::forward<A>(args)...));
}


// ####################
// ##     Parser     ##
// ####################

std::unique_ptr<RootNode> Parser::operator()() {
    auto rootNode = uniquify<RootNode>();
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

Token Parser::expectAndChangeMode(TokenKind kind, LexerMode mode) {
    Token token = this->expect(kind, false);
    if(!this->hasError()) {
        this->lexer->setLexerMode(mode);
        this->fetchNext();
    }
    return token;
}

void Parser::raiseTokenFormatError(TokenKind kind, Token token, const char *msg) {
    std::string message(msg);
    message += ": ";
    message += toString(kind);

    this->createError(kind, token, "TokenFormat", std::move(message));
}

// parse rule definition
std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    assert(CUR_KIND() == FUNCTION);
    unsigned int startPos = START_POS();
    this->consume();    // FUNCTION
    Token token = TRY(this->expect(IDENTIFIER));
    auto node = uniquify<FunctionNode>(startPos, this->lexer->getSourceInfoPtr(), this->lexer->toName(token));
    TRY(this->expect(LP));

    if(CUR_KIND() == APPLIED_NAME) {
        token = this->expect(APPLIED_NAME); // always success
        auto nameNode = uniquify<VarNode>(token, this->lexer->toName(token));
        TRY(this->expect(COLON, false));

        auto type = TRY(this->parse_typeName());

        node->addParamNode(nameNode.release(), type.release());

        for(bool next = true; next;) {
            if(CUR_KIND() == COMMA) {
                this->consume();    // COMMA
                token = TRY(this->expect(APPLIED_NAME));

                nameNode = uniquify<VarNode>(token, this->lexer->toName(token));

                TRY(this->expect(COLON, false));

                type = TRY(this->parse_typeName());

                node->addParamNode(nameNode.release(), type.release());
            } else if(CUR_KIND() == RP) {
                next = false;
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
        auto type = uniquify<ReturnTypeNode>(TRY(this->parse_typeName()).release());
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA, false); // always success
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
    assert(CUR_KIND() == INTERFACE);
    unsigned int startPos = START_POS();

    this->expect(INTERFACE, false); // always success

    // enter TYPE mode
    this->pushLexerMode(yycTYPE);

    Token token = TRY(this->expect(TYPE_PATH));

    // exit TYPE mode
    this->restoreLexerState(token);

    auto node = uniquify<InterfaceNode>(startPos, this->lexer->toTokenText(token));
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
            auto readOnly = this->consume() == LET ? VarDeclNode::CONST : VarDeclNode::VAR;
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

TypeWrapper Parser::parse_basicOrReifiedType(Token token) {
    auto typeToken = uniquify<BaseTypeNode>(token, this->lexer->toName(token));
    if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
        this->expect(TYPE_OPEN, false); // always success

        auto reified = uniquify<ReifiedTypeNode>(typeToken.release());
        reified->addElementTypeNode(TRY(this->parse_typeName()).release());

        while(CUR_KIND() == TYPE_SEP) {
            this->expect(TYPE_SEP, false);  // always success
            reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        }
        token = TRY(this->expect(TYPE_CLOSE));
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
        Token token = this->expect(IDENTIFIER); // always success
        return this->parse_basicOrReifiedType(token);
    }
    case PTYPE_OPEN: {
        Token token = this->expect(PTYPE_OPEN, false);  // always success
        auto reified = uniquify<ReifiedTypeNode>(new BaseTypeNode(token, TYPE_TUPLE));
        reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        while(CUR_KIND() == TYPE_SEP) {
            this->expect(TYPE_SEP, false);  // always success
            reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        }
        token = TRY(this->expect(PTYPE_CLOSE));
        reified->updateToken(token);
        return {std::move(reified), token};
    }
    case ATYPE_OPEN: {
        Token token = this->expect(ATYPE_OPEN, false);  // always success
        auto left = TRY(this->parse_typeName());
        bool isMap = CUR_KIND() == TYPE_MSEP;
        auto reified = uniquify<ReifiedTypeNode>(new BaseTypeNode(token, isMap ? TYPE_MAP : TYPE_ARRAY));
        reified->addElementTypeNode(left.release());
        if(isMap) {
            this->expect(TYPE_MSEP, false); // always success
            reified->addElementTypeNode(TRY(this->parse_typeName()).release());
        }
        token = TRY(this->expect(ATYPE_CLOSE));
        reified->updateToken(token);
        return {std::move(reified), token};
    }
    case TYPEOF: {
        Token token = this->expect(TYPEOF); // always success
        if(CUR_KIND() == PTYPE_OPEN) {
            this->expect(PTYPE_OPEN, false);    // always success
            this->pushLexerMode(yycSTMT);

            unsigned int startPos = token.pos;
            auto exprNode(TRY(this->parse_expression()));

            token = TRY(this->expect(RP, false));

            return {uniquify<TypeOfNode>(startPos, std::move(exprNode).release()), token};
        }
        return this->parse_basicOrReifiedType(token);
    }
    case FUNC: {
        Token token = this->expect(FUNC);   // always success
        if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
            this->expect(TYPE_OPEN, false); // always success

            // parse return type
            auto func = uniquify<FuncTypeNode>(token.pos, TRY(this->parse_typeName()).release());

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

            token = TRY(this->expect(TYPE_CLOSE));
            func->updateToken(token);

            return {std::move(func), token};
        }
        return {uniquify<BaseTypeNode>(token, this->lexer->toName(token)), token};

    }
    case TYPE_PATH: {
        Token token = this->expect(TYPE_PATH);  // always success
        return {uniquify<DBusIfaceTypeNode>(token, this->lexer->toTokenText(token)), token};
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
    auto result = TRY(this->parse_typeNameImpl());
    if(!HAS_NL() && CUR_KIND() == TYPE_OPT) {
        result.token = this->expect(TYPE_OPT); // always success
        auto reified = uniquify<ReifiedTypeNode>(new BaseTypeNode(result.token, TYPE_OPTION));
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
        return uniquify<EmptyNode>(token);
    }
    case FUNCTION: {
        auto node = TRY(this->parse_funcDecl());
        node->setBlockNode(TRY(this->parse_block()).release());
        return std::move(node);
    }
    case INTERFACE: {
        return this->parse_interface();
    }
    case TYPE_ALIAS: {
        unsigned int startPos = START_POS();
        this->consume();    // TYPE_ALIAS
        Token token = TRY(this->expect(IDENTIFIER, false));
        auto typeToken = TRY(this->parse_typeName());
        return uniquify<TypeAliasNode>(startPos, this->lexer->toTokenText(token), typeToken.release());
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
            messageNode = uniquify<StringNode>(std::move(msg));
        }

        return uniquify<AssertNode>(pos, condNode.release(), messageNode.release());
    }
    case BREAK: {
        Token token = this->expect(BREAK); // always success
        return uniquify<JumpNode>(token, true);
    }
    case CONTINUE: {
        Token token = this->expect(CONTINUE);  // always success
        return uniquify<JumpNode>(token, false);
    }
    case EXPORT_ENV: {
        unsigned int startPos = START_POS();
        this->consume();    // EXPORT_ENV
        Token token = TRY(this->expect(IDENTIFIER));
        std::string name(this->lexer->toName(token));
        TRY(this->expect(ASSIGN));
        return uniquify<VarDeclNode>(startPos, std::move(name),
                                     TRY(this->parse_expression()).release(), VarDeclNode::EXPORT_ENV);
    }
    case IMPORT_ENV: {
        unsigned int startPos = START_POS();
        this->consume();    // IMPORT_ENV
        Token token = TRY(this->expect(IDENTIFIER));
        std::unique_ptr<Node> exprNode;
        if(!HAS_NL() && CUR_KIND() == COLON) {
            TRY(this->expectAndChangeMode(COLON, yycSTMT));
            exprNode = TRY(this->parse_expression());
        }

        auto node = uniquify<VarDeclNode>(startPos, this->lexer->toName(token),
                                          exprNode.release(), VarDeclNode::IMPORT_ENV);
        node->updateToken(token);
        return std::move(node);
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
        return uniquify<ReturnNode>(token, exprNode.release());
    }
    EACH_LA_varDecl(GEN_LA_CASE) {
        return this->parse_variableDeclaration();
    }
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default: {
        E_ALTER(EACH_LA_statement(GEN_LA_ALTER));
    }
    }
}

std::unique_ptr<Node> Parser::parse_statement() {
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
    Token token = TRY(this->expect(LBC));
    auto blockNode = uniquify<BlockNode>(token.pos);
    while(CUR_KIND() != RBC) {
        blockNode->addNode(TRY(this->parse_statement()).release());
    }
    token = TRY(this->expect(RBC));
    blockNode->updateToken(token);
    return blockNode;
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
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
    TRY(this->expect(ASSIGN));
    return uniquify<VarDeclNode>(startPos, std::move(name), TRY(this->parse_expression()).release(), readOnly);
}

std::unique_ptr<Node> Parser::parse_ifStatement(bool asElif) {
    unsigned int startPos = START_POS();
    TRY(this->expect(asElif ? ELIF : IF));
    auto condNode = TRY(this->parse_expression());
    auto thenNode = TRY(this->parse_block());

    // parse else
    std::unique_ptr<Node> elseNode;
    switch(CUR_KIND()) {
    case ELIF:
        elseNode = TRY(this->parse_ifStatement(true));
        break;
    case ELSE:
        this->consume();    // ELSE
        elseNode = TRY(this->parse_block());
        break;
    default:
        break;
    }

    return uniquify<IfNode>(startPos, condNode.release(), thenNode.release(), elseNode.release());
}

std::unique_ptr<Node> Parser::parse_forStatement() {
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

        return uniquify<LoopNode>(startPos, initNode.release(), condNode.release(),
                                  iterNode.release(), blockNode.release());
    }
    // for-in
    Token token = TRY(this->expect(APPLIED_NAME));
    TRY(this->expect(IN));
    auto exprNode = TRY(this->parse_expression());
    auto blockNode = TRY(this->parse_block());

    return std::unique_ptr<Node>(
            createForInNode(startPos, this->lexer->toName(token), exprNode.release(), blockNode.release()));
}

std::unique_ptr<Node> Parser::parse_forInit() {
    switch(CUR_KIND()) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        return this->parse_variableDeclaration();
    }
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return uniquify<EmptyNode>();
    }
}

std::unique_ptr<Node> Parser::parse_forCond() {
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        Token token{0, 0};
        return uniquify<VarNode>(token, std::string(VAR_TRUE));
    }
}

std::unique_ptr<Node> Parser::parse_forIter() {
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return uniquify<EmptyNode>();
    }
}

std::unique_ptr<CatchNode> Parser::parse_catchStatement() {
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
    return uniquify<CatchNode>(startPos, this->lexer->toName(token), typeToken.release(), blockNode.release());
}

// command
std::unique_ptr<Node> Parser::parse_command() {
    assert(CUR_KIND() == COMMAND);
    Token token = this->expect(COMMAND);   // always success

    if(CUR_KIND() == LP) {  // command definition
        this->consume();    // LP
        TRY(this->expect(RP));
        auto blockNode = TRY(this->parse_block());
        return uniquify<UserDefinedCmdNode>(
                token.pos, this->lexer->getSourceInfoPtr(), this->lexer->toCmdArg(token), blockNode.release());
    }

    auto kind = this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
    auto node = uniquify<CmdNode>(new StringNode(token, this->lexer->toCmdArg(token), kind));

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
    switch(CUR_KIND()) {
    EACH_LA_redirFile(GEN_LA_CASE) {
        TokenKind kind = this->consume();
        return uniquify<RedirNode>(kind, TRY(this->parse_cmdArg()).release());
    }
    EACH_LA_redirNoFile(GEN_LA_CASE) {
        Token token = this->curToken;
        TokenKind kind = this->consume();
        return uniquify<RedirNode>(kind, token);
    }
    default:
        E_ALTER(EACH_LA_redir(GEN_LA_ALTER));
    }
}

std::unique_ptr<CmdArgNode> Parser::parse_cmdArg() {
    auto node = uniquify<CmdArgNode>(TRY(this->parse_cmdArgSeg(0)).release());

    unsigned int pos = 1;
    for(bool next = true; !HAS_SPACE() && next; pos++) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            node->addSegmentNode(TRY(this->parse_cmdArgSeg(pos)).release());
            break;
        }
        default: {
            next = false;
            break;
        }
        }
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_cmdArgSeg(unsigned int pos) {
    switch(CUR_KIND()) {
    case CMD_ARG_PART: {
        Token token = this->expect(CMD_ARG_PART);   // always success
        auto kind = pos == 0 && this->lexer->startsWith(token, '~') ? StringNode::TILDE : StringNode::STRING;
        return uniquify<StringNode>(token, this->lexer->toCmdArg(token), kind);
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
    default: {
        E_ALTER(EACH_LA_cmdArg(GEN_LA_ALTER));
    }
    }
}

// expression
std::unique_ptr<Node> Parser::parse_expression() {
    if(CUR_KIND() == THROW) {
        unsigned int startPos = START_POS();
        this->consume();    // THROW
        return uniquify<ThrowNode>(startPos, TRY(this->parse_expression()).release());
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
        return uniquify<PipelineNode>(leftNode.release(), rightNode.release());
    }
    if(isAssignOp(op)) {
        return std::unique_ptr<Node>(createAssignNode(leftNode.release(), op, rightNode.release()));
    }

    return uniquify<BinaryOpNode>(leftNode.release(), op, rightNode.release());
}

std::unique_ptr<Node> Parser::parse_binaryExpression(std::unique_ptr<Node> &&leftNode,
                                                     unsigned int basePrecedence) {
    std::unique_ptr<Node> node(std::move(leftNode));
    for(unsigned int p = PRECEDENCE();
        !HAS_NL() && p >= basePrecedence; p = PRECEDENCE()) {
        switch(CUR_KIND()) {
        case AS: {
            this->expect(AS, false);    // always success
            auto type = TRY(this->parse_typeName());
            node = uniquify<TypeOpNode>(node.release(), type.release(), TypeOpNode::NO_CAST);
            break;
        }
        case IS: {
            this->expect(IS, false);   // always success
            auto type = TRY(this->parse_typeName());
            node = uniquify<TypeOpNode>(node.release(), type.release(), TypeOpNode::ALWAYS_FALSE);
            break;
        }
        case WITH: {
            this->consume();    // WITH
            auto redirNode = TRY(this->parse_redirOption());
            auto withNode = uniquify<WithNode>(node.release(), redirNode.release());
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
            node = uniquify<IfNode>(pos, node.release(), tleftNode.release(), trightNode.release());
            break;
        }
        default: {
            TokenKind op = this->consume();
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
    switch(CUR_KIND()) {
    case PLUS:
    case MINUS:
    case NOT: {
        unsigned int startPos = START_POS();
        TokenKind op = this->consume();
        return uniquify<UnaryOpNode>(startPos, op, TRY(this->parse_unaryExpression()).release());
    }
    default: {
        return this->parse_suffixExpression();
    }
    }
}

std::unique_ptr<Node> Parser::parse_suffixExpression() {
    auto node = TRY(this->parse_primaryExpression());

    for(bool next = true; !HAS_NL() && next;) {
        switch(CUR_KIND()) {
        case ACCESSOR: {
            this->consume();    // ACCESSOR
            Token token = TRY(this->expect(IDENTIFIER));
            std::string name = this->lexer->toName(token);
            if(CUR_KIND() == LP && !HAS_NL()) {  // treat as method call
                auto args = TRY(this->parse_arguments());
                Token token = args.getToken();
                node = uniquify<MethodCallNode>(node.release(), std::move(name),
                                                ArgsWrapper::extract(std::move(args)));
                node->updateToken(token);
            } else {    // treat as field access
                node = uniquify<AccessNode>(node.release(), new VarNode(token, std::move(name)));
                node->updateToken(token);
            }
            break;
        }
        case LB: {
            this->consume();    // LB
            auto indexNode = TRY(this->parse_expression());
            auto token = TRY(this->expect(RB));
            node.reset(createIndexNode(node.release(), indexNode.release()));
            node->updateToken(token);
            break;
        }
        case LP: {
            auto args = TRY(this->parse_arguments());
            Token token = args.getToken();
            node = uniquify<ApplyNode>(node.release(), ArgsWrapper::extract(std::move(args)));
            node->updateToken(token);
            break;
        }
        case INC:
        case DEC: {
            Token token = this->curToken;
            TokenKind op = this->consume();
            node.reset(createSuffixNode(node.release(), op, token));
            break;
        }
        case UNWRAP: {
            Token token = this->curToken;
            TokenKind op = this->consume(); // UNWRAP
            unsigned int pos = node->getPos();
            node = uniquify<UnaryOpNode>(pos, op, node.release());
            node->updateToken(token);
            break;
        }
        default: {
            next = false;
            break;
        }
        }
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_primaryExpression() {
    switch(CUR_KIND()) {
    case COMMAND:
        return parse_command();
    case NEW: {
        unsigned int startPos = START_POS();
        this->expect(NEW, false);   // always success
        auto type = TRY(this->parse_typeName());
        auto args = TRY(this->parse_arguments());
        Token token = args.getToken();
        auto node = uniquify<NewNode>(startPos, type.release(), ArgsWrapper::extract(std::move(args)));
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
        Token token = this->expect(PATH_LITERAL);   // always success

        /**
         * skip prefix 'p'
         */
        token.pos++;
        token.size--;
        std::string str;
        this->lexer->singleToString(token, str);    // always success
        return uniquify<StringNode>(token, std::move(str), StringNode::OBJECT_PATH);
    }
    case REGEX_LITERAL: {
        Token token = this->expect(REGEX_LITERAL);  // always success
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
        return uniquify<RegexNode>(old, std::move(str), std::move(re));
    }
    case SIGNAL_LITERAL: {
        Token token = this->expect(SIGNAL_LITERAL); // always success
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
        Token token = this->expect(LP); // always success
        auto node = TRY(this->parse_expression());
        if(CUR_KIND() == COMMA) {   // tuple
            this->consume();    // COMMA
            auto tuple = uniquify<TupleNode>(token.pos, node.release());
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

        token = TRY(this->expect(RP));
        node->updateToken(token);
        return node;
    }
    case LB: {  // array or map
        Token token = this->expect(LB); // always success

        auto keyNode = TRY(this->parse_expression());

        std::unique_ptr<Node> node;
        if(CUR_KIND() == COLON) {   // map
            this->expectAndChangeMode(COLON, yycSTMT);  // always success

            auto valueNode = TRY(this->parse_expression());
            auto mapNode = uniquify<MapNode>(token.pos, keyNode.release(), valueNode.release());
            for(bool next = true; next;) {
                if(CUR_KIND() == COMMA) {
                    this->consume();    // COMMA
                    keyNode = TRY(this->parse_expression());
                    TRY(this->expectAndChangeMode(COLON, yycSTMT));
                    valueNode = TRY(this->parse_expression());
                    mapNode->addEntry(keyNode.release(), valueNode.release());
                } else if(CUR_KIND() == RB) {
                    next = false;
                } else {
                    E_ALTER(COMMA, RB);
                }
            }
            node = std::move(mapNode);
        } else {    // array
            auto arrayNode = uniquify<ArrayNode>(token.pos, keyNode.release());
            for(bool next = true; next;) {
                if(CUR_KIND() == COMMA) {
                    this->consume();    // COMMA
                    arrayNode->addExprNode(TRY(this->parse_expression()).release());
                } else if(CUR_KIND() == RB) {
                    next = false;
                } else {
                    E_ALTER(COMMA, RB);
                }
            }
            node = std::move(arrayNode);
        }

        token = TRY(this->expect(RB));
        node->updateToken(token);
        return node;
    }
    case LBC: {
        return this->parse_block();
    }
    case FOR: {
        return this->parse_forStatement();
    }
    case IF: {
        return this->parse_ifStatement();
    }
    case WHILE: {
        unsigned int startPos = START_POS();
        this->consume();    // WHILE
        auto condNode = TRY(this->parse_expression());
        auto blockNode = TRY(this->parse_block());
        return uniquify<LoopNode>(startPos, condNode.release(), blockNode.release());
    }
    case DO: {
        unsigned int startPos = START_POS();
        this->consume();    // DO
        auto blockNode = TRY(this->parse_block());
        TRY(this->expect(WHILE));
        auto condNode = TRY(this->parse_expression());
        return uniquify<LoopNode>(startPos, condNode.release(), blockNode.release(), true);
    }
    case TRY: {
        unsigned int startPos = START_POS();
        this->consume();    // TRY
        auto tryNode = uniquify<TryNode>(startPos, TRY(this->parse_block()).release());

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
    Token token = TRY(this->expect(asSpecialName ? SPECIAL_NAME : APPLIED_NAME));
    return uniquify<VarNode>(token, this->lexer->toName(token));
}

std::unique_ptr<Node> Parser::parse_stringLiteral() {
    assert(CUR_KIND() == STRING_LITERAL);
    Token token = this->expect(STRING_LITERAL); // always success
    std::string str;
    bool s = this->lexer->singleToString(token, str);
    if(!s) {
        raiseTokenFormatError(STRING_LITERAL, token, "illegal escape sequence");
        return nullptr;
    }
    return uniquify<StringNode>(token, std::move(str));
}

ArgsWrapper Parser::parse_arguments() {
    Token token = TRY(this->expect(LP));

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

    token = TRY(this->expect(RP));
    args.updateToken(token);
    return args;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
    assert(CUR_KIND() == OPEN_DQUOTE);
    Token token = this->expect(OPEN_DQUOTE);   // always success
    auto node = uniquify<StringExprNode>(token.pos);

    for(bool next = true; next;) {
        switch(CUR_KIND()) {
        case STR_ELEMENT: {
            token = this->expect(STR_ELEMENT);  // always success
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

    token = TRY(this->expect(CLOSE_DQUOTE));
    node->updateToken(token);
    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_interpolation() {
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
    switch(CUR_KIND()) {
    case APPLIED_NAME_WITH_BRACKET:
    case SPECIAL_NAME_WITH_BRACKET: {
        Token token = this->curToken;
        this->consume();    // always success
        auto node =  uniquify<VarNode>(token, this->lexer->toName(token));
        auto indexNode = TRY(this->parse_expression());
        TRY(this->expect(RB));
        return std::unique_ptr<Node>(createIndexNode(node.release(), indexNode.release()));
    }
    default:
        return this->parse_interpolation();
    }
}

std::unique_ptr<SubstitutionNode> Parser::parse_substitution() {
    assert(CUR_KIND() == START_SUB_CMD);
    unsigned int pos = START_POS();
    this->consume();    // START_SUB_CMD
    auto exprNode = TRY(this->parse_expression());
    Token token = TRY(this->expect(RP));
    auto node = uniquify<SubstitutionNode>(pos, exprNode.release());
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
