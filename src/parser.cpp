/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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
    OP(OPEN_DQUOTE) \
    OP(START_SUB_CMD) \
    OP(APPLIED_NAME) \
    OP(SPECIAL_NAME) \
    OP(LP) \
    OP(LB)

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
    OP(LBC) \
    OP(BREAK) \
    OP(CONTINUE) \
    OP(DO) \
    OP(EXPORT_ENV) \
    OP(FOR) \
    OP(IF) \
    OP(IMPORT_ENV) \
    OP(LET) \
    OP(RETURN) \
    OP(TRY) \
    OP(VAR) \
    OP(WHILE) \
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
    OP(REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND)

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

#define EACH_LA_assign(OP) \
    OP(ASSIGN) \
    OP(ADD_ASSIGN) \
    OP(SUB_ASSIGN) \
    OP(MUL_ASSIGN) \
    OP(DIV_ASSIGN) \
    OP(MOD_ASSIGN)


#define E_ALTER(...) \
do { \
    const TokenKind alters[] = { __VA_ARGS__ };\
    this->alternativeError(sizeof(alters) / sizeof(alters[0]), alters);\
} while(false)

// for check converted number range
#define CONVERT_TO_NUM(out, kind, token, func) \
    do {\
        int status;\
        out = func(token, status);\
        if(status != 0) { raiseTokenFormatError(kind, token, "out of range"); }\
    } while(false)

#define PUSH_LEXER_MODE(mode) \
    do { this->lexer->pushLexerMode((mode)); this->fetchNext(); } while(false)

#define PRECEDENCE() getPrecedence(CUR_KIND())

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
};

static void raiseTokenFormatError(TokenKind kind, Token token, const char *msg) {
    std::string message(msg);
    message += ": ";
    message += toString(kind);

    throw ParseError(kind, token, "TokenFormat", std::move(message));
}


// ####################
// ##     Parser     ##
// ####################

void Parser::parse(Lexer &lexer, RootNode &rootNode) {
    this->lexer = &lexer;

    // first, fetch token.
    this->fetchNext();

    // start parsing
    rootNode.setSourceInfoPtr(this->lexer->getSourceInfoPtr());
    this->parse_toplevel(rootNode);
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

void Parser::expectAndChangeMode(TokenKind kind, LexerMode mode) {
    this->expect(kind, false);
    this->lexer->setLexerMode(mode);
    this->fetchNext();
}


// parse rule definition

void Parser::parse_toplevel(RootNode &rootNode) {
    while(CUR_KIND() != EOS) {
        rootNode.addNode(this->parse_statement().release());
    }
    this->expect(EOS);
}

std::unique_ptr<Node> Parser::parse_function() {
    auto node(this->parse_funcDecl());
    node->setBlockNode(this->parse_block().release());
    this->parse_statementEnd();
    return std::move(node);
}

std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    unsigned int startPos = START_POS();
    this->expect(FUNCTION);
    Token token = this->expect(IDENTIFIER);
    auto node = uniquify<FunctionNode>(startPos, this->lexer->getSourceInfoPtr(), this->lexer->toName(token));
    this->expect(LP);

    if(CUR_KIND() == APPLIED_NAME) {
        token = this->expect(APPLIED_NAME);
        auto nameNode = uniquify<VarNode>(token, this->lexer->toName(token));
        this->expect(COLON, false);

        std::unique_ptr<TypeNode> type(this->parse_typeName());

        node->addParamNode(nameNode.release(), type.release());

        for(bool next = true; next;) {
            if(CUR_KIND() == COMMA) {
                this->expect(COMMA);
                token = this->expect(APPLIED_NAME);

                nameNode = uniquify<VarNode>(token, this->lexer->toName(token));

                this->expect(COLON, false);

                type = this->parse_typeName();

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
    this->expect(RP);

    if(CUR_KIND() == COLON) {
        this->expect(COLON, false);
        auto type = uniquify<ReturnTypeNode>(this->parse_typeName().release());
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA, false);
            type->addTypeNode(this->parse_typeName().release());
        }
        node->setReturnTypeToken(type.release());
    }

    return node;
}

std::unique_ptr<Node> Parser::parse_interface() {
    unsigned int startPos = START_POS();

    this->expect(INTERFACE, false);

    // enter TYPE mode
    PUSH_LEXER_MODE(yycTYPE);

    Token token = this->expect(TYPE_PATH);

    // exit TYPE mode
    this->restoreLexerState(token);

    auto node = uniquify<InterfaceNode>(startPos, this->lexer->toTokenText(token));
    this->expect(LBC);

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
            bool readOnly = this->consume() == LET;
            token = this->expect(IDENTIFIER);
            this->expect(COLON, false);
            auto type(this->parse_typeName());
            node->addFieldDecl(
                    new VarDeclNode(startPos, this->lexer->toName(token), nullptr, readOnly), type.release());
            this->parse_statementEnd();
            count++;
            break;
        }
        case FUNCTION: {
            auto funcNode(this->parse_funcDecl());
            this->parse_statementEnd();
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

    token = this->expect(RBC);
    node->updateToken(token);
    this->parse_statementEnd();

    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_typeAlias() {
    unsigned int startPos = START_POS();
    this->expect(TYPE_ALIAS);
    Token token = this->expect(IDENTIFIER, false);
    auto typeToken(this->parse_typeName());
    this->parse_statementEnd();
    return uniquify<TypeAliasNode>(startPos, this->lexer->toTokenText(token), typeToken.release());
}

std::unique_ptr<TypeNode> Parser::parse_basicOrReifiedType(Token token) {
    auto typeToken = uniquify<BaseTypeNode>(token, this->lexer->toName(token));
    if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
        this->expect(TYPE_OPEN, false);

        auto reified = uniquify<ReifiedTypeNode>(typeToken.release());
        reified->addElementTypeNode(this->parse_typeName().release());

        while(CUR_KIND() == TYPE_SEP) {
            this->expect(TYPE_SEP, false);
            reified->addElementTypeNode(this->parse_typeName().release());
        }
        token = this->expect(TYPE_CLOSE);
        reified->updateToken(token);

        this->restoreLexerState(token);
        return std::move(reified);
    }

    this->restoreLexerState(token);
    return std::move(typeToken);
}

std::unique_ptr<TypeNode> Parser::parse_typeName() {
    // change lexer state to TYPE
    PUSH_LEXER_MODE(yycTYPE);

    switch(CUR_KIND()) {
    case IDENTIFIER: {
        Token token = this->expect(IDENTIFIER);
        return this->parse_basicOrReifiedType(token);
    }
    case TYPEOF: {
        Token token = this->expect(TYPEOF);
        if(CUR_KIND() == TYPE_OTHER && this->lexer->equals(this->curToken, "(")) {  // treat as typeof operator
            this->expect(TYPE_OTHER, false);
            PUSH_LEXER_MODE(yycSTMT);

            unsigned int startPos = token.pos;
            auto exprNode(this->parse_expression());

            token = this->expect(RP, false);

            this->restoreLexerState(token);
            return uniquify<TypeOfNode>(startPos, std::move(exprNode).release());
        }
        return this->parse_basicOrReifiedType(token);
    }
    case FUNC: {
        Token token = this->expect(FUNC);
        if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
            this->expect(TYPE_OPEN, false);

            // parse return type
            auto func = uniquify<FuncTypeNode>(token.pos, this->parse_typeName().release());

            if(CUR_KIND() == TYPE_SEP) {   // ,[
                this->expect(TYPE_SEP);
                this->expect(PTYPE_OPEN, false);

                // parse first arg type
                func->addParamTypeNode(this->parse_typeName().release());

                // rest arg type
                while(CUR_KIND() == TYPE_SEP) {
                    this->expect(TYPE_SEP, false);
                    func->addParamTypeNode(this->parse_typeName().release());
                }
                this->expect(PTYPE_CLOSE);
            }

            token = this->expect(TYPE_CLOSE);
            func->updateToken(token);

            this->restoreLexerState(token);
            return std::move(func);
        } else {
            this->restoreLexerState(token);
            return uniquify<BaseTypeNode>(token, this->lexer->toName(token));
        }
    }
    case TYPE_PATH: {
        Token token = this->expect(TYPE_PATH);
        this->restoreLexerState(token);
        return uniquify<DBusIfaceTypeNode>(token, this->lexer->toTokenText(token));
    }
    default:
        E_ALTER(
                IDENTIFIER,
                FUNC,
                TYPEOF,
                TYPE_PATH
        );
    }
}

std::unique_ptr<Node> Parser::parse_statement() {
    if(this->lexer->getPrevMode() != yycSTMT) {
        this->refetch(yycSTMT);
    }

    switch(CUR_KIND()) {
    case LINE_END: {
        Token token = this->expect(LINE_END);
        return uniquify<EmptyNode>(token);
    }
    case FUNCTION: {
        return this->parse_function();
    }
    case INTERFACE: {
        return this->parse_interface();
    }
    case TYPE_ALIAS: {
        return this->parse_typeAlias();
    }
    case ASSERT: {
        this->expect(ASSERT);
        auto condNode(this->parse_expression());
        std::unique_ptr<Node> messageNode;
        if(!HAS_NL() && CUR_KIND() == COLON) {
            this->expectAndChangeMode(COLON, yycSTMT);
            messageNode = this->parse_expression();
        }

        auto node = uniquify<AssertNode>(condNode.release(), messageNode.release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case LBC: {
        return this->parse_block();
    }
    case BREAK: {
        Token token = this->expect(BREAK);
        auto node = uniquify<BreakNode>(token);
        this->parse_statementEnd();
        return std::move(node);
    }
    case CONTINUE: {
        Token token = this->expect(CONTINUE);
        auto node = uniquify<ContinueNode>(token);
        this->parse_statementEnd();
        return std::move(node);
    }
    case EXPORT_ENV: {
        unsigned int startPos = START_POS();
        this->expect(EXPORT_ENV);
        Token token = this->expect(IDENTIFIER);
        std::string name(this->lexer->toName(token));
        this->expect(ASSIGN);
        auto node = uniquify<ExportEnvNode>(startPos, std::move(name),
                                            this->parse_expression().release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case FOR: {
        return this->parse_forStatement();
    }
    case IF: {
        auto node = this->parse_ifStatement();
        this->parse_statementEnd();
        return node;
    }
    case IMPORT_ENV: {
        unsigned int startPos = START_POS();
        this->expect(IMPORT_ENV);
        Token token = this->expect(IDENTIFIER);
        auto node = uniquify<ImportEnvNode>(startPos, this->lexer->toName(token));
        node->updateToken(token);
        if(!HAS_NL() && CUR_KIND() == COLON) {
            this->expectAndChangeMode(COLON, yycSTMT);
            node->setDefaultValueNode(this->parse_expression().release());
        }

        this->parse_statementEnd();
        return std::move(node);
    }
    case RETURN: {
        Token token = this->expect(RETURN);
        std::unique_ptr<Node> node;

        bool next;
        switch(CUR_KIND()) {
        EACH_LA_expression(GEN_LA_CASE)
            next = true;
            break;
        default:
            next = false;
            break;
        }
        if(!HAS_NL() && next) {
            node = uniquify<ReturnNode>(token.pos, this->parse_expression().release());
        } else {
            node = uniquify<ReturnNode>(token);
        }
        this->parse_statementEnd();
        return node;
    }
    case WHILE: {
        unsigned int startPos = START_POS();
        this->expect(WHILE);
        auto condNode(this->parse_expression());
        auto blockNode(this->parse_block());
        this->parse_statementEnd();
        return uniquify<WhileNode>(startPos, condNode.release(), blockNode.release());
    }
    case DO: {
        unsigned int startPos = START_POS();
        this->expect(DO);
        auto blockNode(this->parse_block());
        this->expect(WHILE);
        auto condNode(this->parse_expression());
        auto node = uniquify<DoWhileNode>(startPos, blockNode.release(), condNode.release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case TRY: {
        unsigned int startPos = START_POS();
        this->expect(TRY);
        auto tryNode = uniquify<TryNode>(startPos, this->parse_block().release());

        // parse catch
        while(CUR_KIND() == CATCH) {
            tryNode->addCatchNode(this->parse_catchStatement().release());
        }

        // parse finally
        if(CUR_KIND() == FINALLY) {
            this->expect(FINALLY);
            tryNode->addFinallyNode(this->parse_block().release());
        }
        this->parse_statementEnd();
        return std::move(tryNode);
    }
    EACH_LA_varDecl(GEN_LA_CASE) {
        auto node(this->parse_variableDeclaration());
        this->parse_statementEnd();
        return node;
    }
    EACH_LA_expression(GEN_LA_CASE) {
        auto node(this->parse_assignmentExpression());
        this->parse_statementEnd();
        return node;
    }
    default: {
        E_ALTER(EACH_LA_statement(GEN_LA_ALTER));
    }
    }
}

void Parser::parse_statementEnd() {
    switch(CUR_KIND()) {
    case EOS:
    case RBC:
        break;
    case LINE_END:
        this->consume();
        break;
    default:
        if(!HAS_NL()) {
            raiseTokenMismatchedError(CUR_KIND(), this->curToken, NEW_LINE);
        }
        break;
    }
}

std::unique_ptr<BlockNode> Parser::parse_block() {
    Token token = this->expect(LBC);
    auto blockNode = uniquify<BlockNode>(token.pos);
    while(CUR_KIND() != RBC) {
        blockNode->addNode(this->parse_statement().release());
    }
    token = this->expect(RBC);
    blockNode->updateToken(token);
    return blockNode;
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
    unsigned int startPos = START_POS();
    bool readOnly = false;
    if(CUR_KIND() == VAR) {
        this->expect(VAR);
    } else {
        this->expect(LET);
        readOnly = true;
    }

    Token token = this->expect(IDENTIFIER);
    std::string name(this->lexer->toName(token));
    this->expect(ASSIGN);
    return uniquify<VarDeclNode>(startPos, std::move(name),
                                 this->parse_expression().release(), readOnly);
}

std::unique_ptr<Node> Parser::parse_ifStatement(bool asElif) {
    unsigned int startPos = START_POS();
    this->expect(asElif ? ELIF : IF);
    std::unique_ptr<Node> condNode(this->parse_expression());
    std::unique_ptr<BlockNode> thenNode(this->parse_block());

    // parse else
    std::unique_ptr<Node> elseNode;
    switch(CUR_KIND()) {
    case ELIF:
        elseNode = this->parse_ifStatement(true);
        break;
    case ELSE:
        this->expect(ELSE);
        elseNode = this->parse_block();
        break;
    default:
        break;
    }

    return uniquify<IfNode>(startPos, condNode.release(), thenNode.release(), elseNode.release());
}

std::unique_ptr<Node> Parser::parse_forStatement() {
    unsigned int startPos = START_POS();
    this->expect(FOR);

    if(CUR_KIND() == LP) {  // for
        this->expect(LP);

        std::unique_ptr<Node> initNode(this->parse_forInit());
        this->expect(LINE_END);

        std::unique_ptr<Node> condNode(this->parse_forCond());
        this->expect(LINE_END);

        std::unique_ptr<Node> iterNode(this->parse_forIter());

        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());

        this->parse_statementEnd();
        return uniquify<ForNode>(startPos, initNode.release(), condNode.release(),
                                 iterNode.release(), blockNode.release());
    } else {    // for-in
        Token token = this->expect(APPLIED_NAME);
        this->expect(IN);
        std::unique_ptr<Node> exprNode(this->parse_expression());
        std::unique_ptr<BlockNode> blockNode(this->parse_block());

        return std::unique_ptr<Node>(
                createForInNode(startPos, this->lexer->toName(token), exprNode.release(), blockNode.release()));
    }
}

std::unique_ptr<Node> Parser::parse_forInit() {
    switch(CUR_KIND()) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        return this->parse_variableDeclaration();
    }
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_assignmentExpression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_forCond() {
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_forIter() {
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_assignmentExpression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<CatchNode> Parser::parse_catchStatement() {
    unsigned int startPos = START_POS();
    this->expect(CATCH);

    bool paren = CUR_KIND() == LP;
    if(paren) {
        this->expect(LP);
    }

    Token token = this->expect(APPLIED_NAME);
    std::unique_ptr<TypeNode> typeToken;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false);
        typeToken = this->parse_typeName();
    }

    if(paren) {
        this->expect(RP);
    }

    std::unique_ptr<BlockNode> blockNode(this->parse_block());

    if(typeToken) {
        return uniquify<CatchNode>(startPos, this->lexer->toName(token),
                                   typeToken.release(), blockNode.release());
    } else {
        return uniquify<CatchNode>(startPos, this->lexer->toName(token), blockNode.release());
    }
}

// command
std::unique_ptr<Node> Parser::parse_pipedCommand() {
    std::unique_ptr<Node> cmdNode(this->parse_command());
    if(dynamic_cast<UserDefinedCmdNode*>(cmdNode.get()) != nullptr) {
        return cmdNode;
    }

    auto node = uniquify<PipedCmdNode>(cmdNode.release());

    while(CUR_KIND() == PIPE) {
        this->expect(PIPE);
        node->addCmdNodes(this->parse_command().release());
    }
    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_command() {
    Token token = this->expect(COMMAND);

    if(CUR_KIND() == LP) {  // command definition
        this->expect(LP);
        this->expect(RP);
        return uniquify<UserDefinedCmdNode>(
                token.pos, this->lexer->getSourceInfoPtr(), this->lexer->toCmdArg(token),
                this->parse_block().release());
    }


    std::unique_ptr<CmdNode> node;

    if(this->lexer->startsWith(token, '~')) {
        node = uniquify<CmdNode>(new TildeNode(token, this->lexer->toCmdArg(token)));
    } else {
        node = uniquify<CmdNode>(token, this->lexer->toCmdArg(token));
    }

    for(bool next = true; next && HAS_SPACE();) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            node->addArgNode(this->parse_cmdArg().release());
            break;
        }
        EACH_LA_redir(GEN_LA_CASE) {
            this->parse_redirOption(node);
            break;
        }
        default:
            next = false;
            break;
        }
    }
    return std::move(node);
}

void Parser::parse_redirOption(std::unique_ptr<CmdNode> &node) {
    switch(CUR_KIND()) {
    EACH_LA_redirFile(GEN_LA_CASE) {
        TokenKind kind = this->consume();
        node->addRedirOption(kind, this->parse_cmdArg().release());
        break;
    }
    EACH_LA_redirNoFile(GEN_LA_CASE) {
        node->addRedirOption(CUR_KIND(), this->curToken);
        this->consume();
        break;
    }
    default:
        E_ALTER(EACH_LA_redir(GEN_LA_ALTER));
    }
}

std::unique_ptr<CmdArgNode> Parser::parse_cmdArg() {
    auto node = uniquify<CmdArgNode>(this->parse_cmdArgSeg(true).release());

    for(bool next = true; !HAS_SPACE() && next;) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            node->addSegmentNode(this->parse_cmdArgSeg().release());
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

std::unique_ptr<Node> Parser::parse_cmdArgSeg(bool expandTilde) {
    switch(CUR_KIND()) {
    case CMD_ARG_PART: {
        Token token = this->expect(CMD_ARG_PART);
        if(expandTilde && this->lexer->startsWith(token, '~')) {
            return uniquify<TildeNode>(token, this->lexer->toCmdArg(token));
        }
        return uniquify<StringValueNode>(token, this->lexer->toCmdArg(token));
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
std::unique_ptr<Node> Parser::parse_assignmentExpression() {
    if(CUR_KIND() == THROW) {
        return this->parse_expression();
    }

    auto node(this->parse_unaryExpression());
    if(!HAS_NL()) {
        switch(CUR_KIND()) {
        EACH_LA_assign(GEN_LA_CASE) {
            TokenKind op = this->consume();
            auto rightNode(this->parse_expression());
            node.reset(createAssignNode(node.release(), op, rightNode.release()));
            break;
        }
        case INC:
        case DEC: {
            Token token = this->curToken;
            TokenKind op = this->consume();
            node.reset(createSuffixNode(node.release(), op, token));
            break;
        }
        default:
            node = this->parse_binaryExpression(std::move(node), getPrecedence(TERNARY));
            break;
        }
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_expression() {
    if(CUR_KIND() == THROW) {
        unsigned int startPos = START_POS();
        this->expect(THROW);
        return uniquify<ThrowNode>(startPos, this->parse_expression().release());
    }
    return this->parse_binaryExpression(
            this->parse_unaryExpression(), getPrecedence(TERNARY));
}

std::unique_ptr<Node> Parser::parse_binaryExpression(std::unique_ptr<Node> &&leftNode,
                                                     unsigned int basePrecedence) {
    std::unique_ptr<Node> node(std::move(leftNode));
    for(unsigned int p = PRECEDENCE();
        !HAS_NL() && p >= basePrecedence; p = PRECEDENCE()) {
        switch(CUR_KIND()) {
        case AS: {
            this->expect(AS, false);
            std::unique_ptr<TypeNode> type(this->parse_typeName());
            node = uniquify<CastNode>(node.release(), type.release());
            break;
        }
        case IS: {
            this->expect(IS, false);
            std::unique_ptr<TypeNode> type(this->parse_typeName());
            node = uniquify<InstanceOfNode>(node.release(), type.release());
            break;
        }
        case PRINT: {
            Token token = this->expect(PRINT);
            node = uniquify<PrintNode>(node.release());
            node->updateToken(token);
            break;
        }
        case TERNARY: {
            this->consume();
            auto tleftNode(this->parse_expression());
            this->expectAndChangeMode(COLON, yycSTMT);
            auto trightNode(this->parse_expression());
            node = uniquify<TernaryNode>(node.release(), tleftNode.release(), trightNode.release());
            break;
        }
        default: {
            TokenKind op = this->consume();
            std::unique_ptr<Node> rightNode(this->parse_unaryExpression());
            for(unsigned int nextP = PRECEDENCE(); !HAS_NL() && nextP > p; nextP = PRECEDENCE()) {
                rightNode = this->parse_binaryExpression(std::move(rightNode), nextP);
            }
            node.reset(createBinaryOpNode(node.release(), op, rightNode.release()));
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
        return uniquify<UnaryOpNode>(startPos, op, this->parse_unaryExpression().release());
    }
    default: {
        return this->parse_memberExpression();
    }
    }
}

std::unique_ptr<Node> Parser::parse_memberExpression() {
    auto node(this->parse_primaryExpression());

    for(bool next = true; !HAS_NL() && next;) {
        switch(CUR_KIND()) {
        case ACCESSOR: {
            this->expect(ACCESSOR);
            Token token = this->expect(IDENTIFIER);
            std::string name(this->lexer->toName(token));
            if(CUR_KIND() == LP && !HAS_NL()) {  // treat as method call
                ArgsWrapper args(this->parse_arguments());
                node = uniquify<MethodCallNode>(node.release(), std::move(name),
                                                ArgsWrapper::extract(std::move(args)));
                node->updateToken(args.getToken());
            } else {    // treat as field access
                node = uniquify<AccessNode>(node.release(), std::move(name));
                node->updateToken(token);
            }
            break;
        }
        case LB: {
            this->expect(LB);
            auto indexNode(this->parse_expression());
            auto token = this->expect(RB);
            node.reset(createIndexNode(node.release(), indexNode.release()));
            node->updateToken(token);
            break;
        }
        case LP: {
            ArgsWrapper args(this->parse_arguments());
            node = uniquify<ApplyNode>(node.release(), ArgsWrapper::extract(std::move(args)));
            node->updateToken(args.getToken());
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
        return parse_pipedCommand();
    case NEW: {
        unsigned int startPos = START_POS();
        this->expect(NEW, false);
        std::unique_ptr<TypeNode> type(this->parse_typeName());
        ArgsWrapper args(this->parse_arguments());
        auto node = uniquify<NewNode>(startPos, type.release(), ArgsWrapper::extract(std::move(args)));
        node->updateToken(args.getToken());
        return std::move(node);
    }
    case BYTE_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(BYTE_LITERAL);
        unsigned char value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toUint8);
        return std::unique_ptr<Node>(IntValueNode::newByte(token, value));
    }
    case INT16_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(INT16_LITERAL);
        short value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toInt16);
        return std::unique_ptr<Node>(IntValueNode::newInt16(token, value));
    }
    case UINT16_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(UINT16_LITERAL);
        unsigned short value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toUint16);
        return std::unique_ptr<Node>(IntValueNode::newUint16(token, value));
    }
    case INT32_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(INT32_LITERAL);
        int value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toInt32);
        return std::unique_ptr<Node>(IntValueNode::newInt32(token, value));
    }
    case UINT32_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(UINT32_LITERAL);
        unsigned int value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toUint32);
        return std::unique_ptr<Node>(IntValueNode::newUint32(token, value));
    }
    case INT64_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(INT64_LITERAL);
        long value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toInt64);
        return std::unique_ptr<Node>(LongValueNode::newInt64(token, value));
    }
    case UINT64_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(UINT64_LITERAL);
        unsigned long value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toUint64);
        return std::unique_ptr<Node>(LongValueNode::newUint64(token, value));
    }
    case FLOAT_LITERAL: {
        TokenKind kind = CUR_KIND();
        Token token = this->expect(FLOAT_LITERAL);
        double value;
        CONVERT_TO_NUM(value, kind, token, this->lexer->toDouble);
        return uniquify<FloatValueNode>(token, value);
    }
    case STRING_LITERAL: {
        return this->parse_stringLiteral();
    }
    case PATH_LITERAL: {
        Token token = this->expect(PATH_LITERAL);

        /**
         * skip prefix 'p'
         */
        token.pos++;
        token.size--;
        std::string str;
        this->lexer->singleToString(token, str);    // always success
        return uniquify<ObjectPathNode>(token, std::move(str));
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
        Token token = this->expect(LP);
        auto node(this->parse_expression());
        if(CUR_KIND() == COMMA) {   // tuple
            this->expect(COMMA);
            auto rightNode(this->parse_expression());
            auto tuple = uniquify<TupleNode>(token.pos, node.release(), rightNode.release());
            for(bool next = true; next;) {
                if(CUR_KIND() == COMMA) {
                    this->expect(COMMA);
                    tuple->addNode(this->parse_expression().release());
                } else if(CUR_KIND() == RP) {
                    next = false;
                } else {
                    E_ALTER(COMMA, RP);
                }
            }
            node.reset(tuple.release());
        } else {
            node->setPos(token.pos);
        }

        token = this->expect(RP);
        node->updateToken(token);
        return node;
    }
    case LB: {  // array or map
        Token token = this->expect(LB);

        auto keyNode(this->parse_expression());

        std::unique_ptr<Node> node;
        if(CUR_KIND() == COLON) {   // map
            this->expectAndChangeMode(COLON, yycSTMT);

            auto valueNode(this->parse_expression());
            auto mapNode = uniquify<MapNode>(token.pos, keyNode.release(), valueNode.release());
            for(bool next = true; next;) {
                if(CUR_KIND() == COMMA) {
                    this->expect(COMMA);
                    keyNode = this->parse_expression();
                    this->expectAndChangeMode(COLON, yycSTMT);
                    valueNode = this->parse_expression();
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
                    this->expect(COMMA);
                    arrayNode->addExprNode(this->parse_expression().release());
                } else if(CUR_KIND() == RB) {
                    next = false;
                } else {
                    E_ALTER(COMMA, RB);
                }
            }
            node = std::move(arrayNode);
        }

        token = this->expect(RB);
        node->updateToken(token);
        return node;
    }
    default:
        E_ALTER(EACH_LA_primary(GEN_LA_ALTER));
    }
}

std::unique_ptr<Node> Parser::parse_appliedName(bool asSpecialName) {
    Token token = this->expect(asSpecialName ? SPECIAL_NAME : APPLIED_NAME);
    return uniquify<VarNode>(token, this->lexer->toName(token));
}

std::unique_ptr<Node> Parser::parse_stringLiteral() {
    Token token = this->expect(STRING_LITERAL);
    std::string str;
    bool s = this->lexer->singleToString(token, str);
    if(!s) {
        raiseTokenFormatError(STRING_LITERAL, token, "illegal escape sequence");
    }
    return uniquify<StringValueNode>(token, std::move(str));
}

ArgsWrapper Parser::parse_arguments() {
    Token token = this->expect(LP);

    ArgsWrapper args(token.pos);
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        args.addArgNode(this->parse_expression());
        for(bool next = true; next;) {
            if(CUR_KIND() == COMMA) {
                this->expect(COMMA);
                args.addArgNode(this->parse_expression());
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

    token = this->expect(RP);
    args.updateToken(token);
    return args;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
    Token token = this->expect(OPEN_DQUOTE);
    auto node = uniquify<StringExprNode>(token.pos);

    for(bool next = true; next;) {
        switch(CUR_KIND()) {
        case STR_ELEMENT: {
            token = this->expect(STR_ELEMENT);
            node->addExprNode(
                    new StringValueNode(token, this->lexer->doubleElementToString(token)));
            break;
        }
        EACH_LA_interpolation(GEN_LA_CASE) {
            node->addExprNode(this->parse_interpolation().release());
            break;
        }
        case START_SUB_CMD: {
            auto subNode(this->parse_substitution());
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

    token = this->expect(CLOSE_DQUOTE);
    node->updateToken(token);
    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_interpolation() {
    switch(CUR_KIND()) {
    case APPLIED_NAME:
    case SPECIAL_NAME:
        return this->parse_appliedName(CUR_KIND() == SPECIAL_NAME);
    default:
        this->expect(START_INTERP);
        auto node(this->parse_expression());
        this->expect(RBC);
        return node;
    }
}

std::unique_ptr<Node> Parser::parse_paramExpansion() {
    switch(CUR_KIND()) {
    case APPLIED_NAME_WITH_BRACKET:
    case SPECIAL_NAME_WITH_BRACKET: {
        Token token = this->curToken;
        this->consume();
        auto node =  uniquify<VarNode>(token, this->lexer->toName(token));
        auto indexNode(this->parse_expression());
        this->expect(RB);
        return std::unique_ptr<Node>(createIndexNode(node.release(), indexNode.release()));
    }
    default:
        return this->parse_interpolation();
    }
}

std::unique_ptr<SubstitutionNode> Parser::parse_substitution() {
    this->expect(START_SUB_CMD);
    auto node(this->parse_expression());
    this->expect(RP);
    return uniquify<SubstitutionNode>(node.release());
}

bool parse(const char *sourceName, RootNode &rootNode) {
    FILE *fp = fopen(sourceName, "rb");
    if(fp == NULL) {
        return false;
    }

    Lexer lexer(sourceName, fp);
    Parser parser;

    try {
        parser.parse(lexer, rootNode);
    } catch(const ParseError &e) {
        return false;   //FIXME: display error message.
    }
    return true;
}

} // namespace ydsh