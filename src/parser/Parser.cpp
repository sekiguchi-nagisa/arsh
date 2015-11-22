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

#include "Parser.h"


// helper macro
#define NEXT_TOKEN() this->fetchNext()

#define LN() this->lexer->getLineNum()

#define HAS_NL() this->lexer->isPrevNewLine()

#define HAS_SPACE() this->lexer->isPrevSpace()

#define CUR_KIND() this->curToken.kind

#define GEN_LA_CASE(CASE) case CASE:
#define GEN_LA_ALTER(CASE) CASE,

// for lookahead
#define EACH_LA_interpolation(OP) \
    OP(APPLIED_NAME) \
    OP(SPECIAL_NAME) \
    OP(START_INTERP)

#define EACH_LA_primary(OP) \
    OP(NEW) \
    OP(INT_LITERAL) \
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
    OP(THROW) \
    OP(VAR) \
    OP(WHILE) \
    OP(COMMAND) \
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
    EACH_LA_interpolation(OP)

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
    this->alternative(alters);\
} while(false)

// for check converted number range
#define CONVERT_TO_NUM(out, token, func) \
    do {\
        int status;\
        out = func(token, status);\
        if(status != 0) { throw OutOfRangeNumError(token); }\
    } while(0)

#define PRECEDENCE() getPrecedence(CUR_KIND())

namespace ydsh {
namespace parser {

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

std::vector<Node *> ArgsWrapper::remove() {
    return std::move(this->nodes);
}

template <typename T, typename ... A>
static inline std::unique_ptr<T> uniquify(A &&... args) {
    return std::unique_ptr<T>(new T(std::forward<A>(args)...));
};


// ####################
// ##     Parser     ##
// ####################

void Parser::parse(Lexer &lexer, RootNode &rootNode) {
    this->lexer = &lexer;

    // first, fetch token.
    NEXT_TOKEN();

    // start parsing
    rootNode.setSourceInfoPtr(this->lexer->getSourceInfoPtr());
    this->parse_toplevel(rootNode);
}

void Parser::alternative(const TokenKind *kinds) {
    std::vector<TokenKind> alters;
    for(unsigned int i = 0; kinds[i] != DUMMY; i++) {
        alters.push_back(kinds[i]);
    }
    this->alternativeError(std::move(alters));
}

// parse rule definition

void Parser::parse_toplevel(RootNode &rootNode) {
    while(CUR_KIND() != EOS) {
        rootNode.addNode(this->parse_statement().release());
    }
    this->expect(EOS);
}

void Parser::refetch(LexerMode mode) {
    this->lexer->setLineNum(this->curToken.lineNum);
    this->lexer->setPos(this->curToken.startPos);
    this->lexer->setLexerMode(mode);
    this->fetchNext();
}

void Parser::expectAfter(TokenKind kind, LexerMode mode) {
    this->expect(kind, false);
    this->lexer->setLexerMode(mode);
    this->fetchNext();
}

std::unique_ptr<Node> Parser::parse_function() {
    auto node(this->parse_funcDecl());
    node->setBlockNode(this->parse_block().release());
    this->parse_statementEnd();
    return std::move(node);
}

std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    unsigned int n = LN();
    this->expect(FUNCTION);
    Token token;
    this->expect(IDENTIFIER, token);
    auto node = uniquify<FunctionNode>(n, this->lexer->getSourceInfoPtr(), this->lexer->toName(token));
    this->expect(LP);

    if(CUR_KIND() == APPLIED_NAME) {
        this->expect(APPLIED_NAME, token);
        auto nameNode = uniquify<VarNode>(token.lineNum, this->lexer->toName(token));
        this->expect(COLON, false);

        std::unique_ptr<TypeNode> type(this->parse_typeName());

        node->addParamNode(nameNode.release(), type.release());

        while(CUR_KIND() == COMMA) {
            this->expect(COMMA);
            this->expect(APPLIED_NAME, token);

            nameNode = uniquify<VarNode>(token.lineNum, this->lexer->toName(token));

            this->expect(COLON, false);

            type = this->parse_typeName();

            node->addParamNode(nameNode.release(), type.release());
        }
    }

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
    unsigned int n = LN();

    this->expect(INTERFACE, false);

    // enter TYPE mode
    this->lexer->pushLexerMode(yycTYPE);
    NEXT_TOKEN();

    Token token;
    this->expect(TYPE_PATH, token);

    // exit TYPE mode
    this->restoreLexerState(token);

    auto node = uniquify<InterfaceNode>(n, this->lexer->toTokenText(token));
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
            n = LN();
            bool readOnly = this->consume() == LET;
            this->expect(IDENTIFIER, token);
            this->expect(COLON, false);
            auto type(this->parse_typeName());
            node->addFieldDecl(
                    new VarDeclNode(n, this->lexer->toName(token), nullptr, readOnly), type.release());
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
        E_ALTER(
                FUNCTION,
                VAR,
                LET,
                DUMMY,
        );
    }

    this->expect(RBC);
    this->parse_statementEnd();

    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_typeAlias() {
    unsigned int n = LN();
    this->expect(TYPE_ALIAS);
    Token token;
    this->expect(IDENTIFIER, token, false);
    auto typeToken(this->parse_typeName());
    this->parse_statementEnd();
    return uniquify<TypeAliasNode>(n, this->lexer->toTokenText(token), typeToken.release());
}

void Parser::restoreLexerState(const Token &prevToken) {
    unsigned int pos = prevToken.startPos + prevToken.size;
    this->lexer->setPos(pos);
    this->lexer->popLexerMode();
    this->lexer->setLineNum(prevToken.lineNum);
    NEXT_TOKEN();
}

std::unique_ptr<TypeNode> Parser::parse_basicOrReifiedType(Token &token) {
    auto typeToken = uniquify<BaseTypeNode>(token.lineNum, this->lexer->toName(token));
    if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
        this->expect(TYPE_OPEN, false);

        auto reified = uniquify<ReifiedTypeNode>(typeToken.release());
        reified->addElementTypeNode(this->parse_typeName().release());

        while(CUR_KIND() == TYPE_SEP) {
            this->expect(TYPE_SEP, false);
            reified->addElementTypeNode(this->parse_typeName().release());
        }
        this->expect(TYPE_CLOSE, token);

        this->restoreLexerState(token);
        return std::move(reified);
    }

    this->restoreLexerState(token);
    return std::move(typeToken);
}

std::unique_ptr<TypeNode> Parser::parse_typeName() {
    // change lexer state to TYPE
    this->lexer->pushLexerMode(yycTYPE);
    NEXT_TOKEN();

    switch(CUR_KIND()) {
    case IDENTIFIER: {
        Token token;
        this->expect(IDENTIFIER, token);
        return this->parse_basicOrReifiedType(token);
    }
    case TYPEOF: {
        Token token;
        this->expect(TYPEOF, token);
        if(CUR_KIND() == TYPE_OTHER && this->lexer->equals(this->curToken, "(")) {  // treat as typeof operator
            this->expect(TYPE_OTHER, false);
            this->lexer->pushLexerMode(yycSTMT);
            NEXT_TOKEN();

            std::unique_ptr<Node> exprNode(this->parse_expression());

            this->expect(RP, token, false);

            this->restoreLexerState(token);
            return uniquify<TypeOfNode>(std::move(exprNode).release());
        }
        return this->parse_basicOrReifiedType(token);
    }
    case FUNC: {
        Token token;
        this->expect(FUNC, token);
        if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
            this->expect(TYPE_OPEN, false);

            // parse return type
            auto func = uniquify<FuncTypeNode>(this->parse_typeName().release());

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

            this->expect(TYPE_CLOSE, token);

            this->restoreLexerState(token);
            return std::move(func);
        } else {
            this->restoreLexerState(token);
            return uniquify<BaseTypeNode>(token.lineNum, this->lexer->toName(token));
        }
    }
    case TYPE_PATH: {
        Token token;
        this->expect(TYPE_PATH, token);
        this->restoreLexerState(token);
        return uniquify<DBusIfaceTypeNode>(token.lineNum, this->lexer->toTokenText(token));
    }
    default:
        E_ALTER(
                IDENTIFIER,
                FUNC,
                TYPEOF,
                TYPE_PATH,
                DUMMY
        );
        return std::unique_ptr<TypeNode>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_statement() {
    if(this->lexer->getPrevMode() != yycSTMT) {
        this->refetch(yycSTMT);
    }

    switch(CUR_KIND()) {
    case LINE_END: {
        unsigned int n = LN();
        this->expect(LINE_END);
        return uniquify<EmptyNode>(n);
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
        unsigned int n = LN();
        this->expect(ASSERT);
        this->expect(LP);
        auto node = uniquify<AssertNode>(n, this->parse_commandOrExpression().release());
        this->expect(RP);
        this->parse_statementEnd();
        return std::move(node);
    }
    case LBC: {
        return this->parse_block();
    }
    case BREAK: {
        unsigned int n = LN();
        this->expect(BREAK);
        auto node = uniquify<BreakNode>(n);
        this->parse_statementEnd();
        return std::move(node);
    }
    case CONTINUE: {
        unsigned int n = LN();
        this->expect(CONTINUE);
        auto node = uniquify<ContinueNode>(n);
        this->parse_statementEnd();
        return std::move(node);
    }
    case EXPORT_ENV: {
        unsigned int n = LN();
        this->expect(EXPORT_ENV);
        Token token;
        this->expect(IDENTIFIER, token);
        std::string name(this->lexer->toName(token));
        this->expect(ASSIGN);
        auto node = uniquify<ExportEnvNode>(n, std::move(name), this->parse_expression().release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case FOR: {
        return this->parse_forStatement();
    }
    case IF: {
        unsigned int n = LN();
        this->expect(IF);
        this->expect(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        auto ifNode = uniquify<IfNode>(n, condNode.release(), blockNode.release());

        // parse elif
        while(CUR_KIND() == ELIF) {
            this->expect(ELIF);
            this->expect(LP);
            condNode = this->parse_commandOrExpression();
            this->expect(RP);
            blockNode = this->parse_block();
            ifNode->addElifNode(condNode.release(), blockNode.release());
        }

        // parse else
        if(CUR_KIND() == ELSE) {
            this->expect(ELSE);
            ifNode->addElseNode(this->parse_block().release());
        }
        this->parse_statementEnd();
        return std::move(ifNode);
    }
    case IMPORT_ENV: {
        unsigned int n = LN();
        this->expect(IMPORT_ENV);
        Token token;
        this->expect(IDENTIFIER, token);
        std::unique_ptr<Node> defaultValueNode;
        if(!HAS_NL() && CUR_KIND() == COLON) {
            this->expectAfter(COLON, yycSTMT);
            defaultValueNode = this->parse_expression();
        }

        auto node = uniquify<ImportEnvNode>(n, this->lexer->toName(token), defaultValueNode.release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case RETURN: {
        unsigned int n = LN();
        this->expect(RETURN);
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
            node = uniquify<ReturnNode>(n, this->parse_expression().release());
        } else {
            node = uniquify<ReturnNode>(n);
        }
        this->parse_statementEnd();
        return node;
    }
    case THROW: {
        unsigned int n = LN();
        this->expect(THROW);
        auto node = uniquify<ThrowNode>(n, this->parse_expression().release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case WHILE: {
        unsigned int n = LN();
        this->expect(WHILE);
        this->expect(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        this->parse_statementEnd();
        return uniquify<WhileNode>(n, condNode.release(), blockNode.release());
    }
    case DO: {
        unsigned int n = LN();
        this->expect(DO);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        this->expect(WHILE);
        this->expect(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->expect(RP);
        auto node = uniquify<DoWhileNode>(n, blockNode.release(), condNode.release());
        this->parse_statementEnd();
        return std::move(node);
    }
    case TRY: {
        unsigned int n = LN();
        this->expect(TRY);
        auto tryNode = uniquify<TryNode>(n, this->parse_block().release());

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
    case VAR:
    case LET: {
        std::unique_ptr<Node> node(this->parse_variableDeclaration());
        this->parse_statementEnd();
        return node;
    }
    case COMMAND: {
        std::unique_ptr<Node> node(this->parse_commandListExpression());
        this->parse_statementEnd();
        return node;
    }
    EACH_LA_expression(GEN_LA_CASE) {
        std::unique_ptr<Node> node(this->parse_expression());
        this->parse_statementEnd();
        return node;
    }
    default: {
        E_ALTER(
                EACH_LA_statement(GEN_LA_ALTER)
                DUMMY
        );
        return std::unique_ptr<Node>(nullptr);
    }
    }
}

void Parser::parse_statementEnd() {
    switch(CUR_KIND()) {
    case EOS:
    case RBC:
        break;
    case LINE_END:
        NEXT_TOKEN();
        break;
    default:
        if(!HAS_NL()) {
            throw TokenMismatchedError(this->curToken, NEW_LINE);
        }
        break;
    }
}

std::unique_ptr<BlockNode> Parser::parse_block() {
    unsigned int n = LN();
    this->expect(LBC);
    auto blockNode = uniquify<BlockNode>(n);
    while(CUR_KIND() != RBC) {
        blockNode->addNode(this->parse_statement().release());
    }
    this->expect(RBC);
    return blockNode;
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
    switch(CUR_KIND()) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        bool readOnly = this->consume() != VAR;

        Token token;
        this->expect(IDENTIFIER, token);
        std::string name(this->lexer->toName(token));
        this->expect(ASSIGN);
        return uniquify<VarDeclNode>(token.lineNum, std::move(name),
                                     this->parse_commandOrExpression().release(), readOnly);
    }
    default:
        E_ALTER(
                EACH_LA_varDecl(GEN_LA_ALTER)
                DUMMY
        );
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_forStatement() {
    unsigned int n = LN();
    this->expect(FOR);
    this->expect(LP);

    std::unique_ptr<Node> initNode(this->parse_forInit());

    if(dynamic_cast<VarNode *>(initNode.get()) != nullptr) { // treat as for-in
        std::unique_ptr<VarNode> nameNode(static_cast<VarNode *>(initNode.release()));

        this->expect(IN);
        std::unique_ptr<Node> exprNode(this->parse_expression());
        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());

        return std::unique_ptr<Node>(
                createForInNode(n, nameNode.release(), exprNode.release(), blockNode.release()));
    }

    this->expect(LINE_END);

    std::unique_ptr<Node> condNode(this->parse_forCond());
    this->expect(LINE_END);

    std::unique_ptr<Node> iterNode(this->parse_forIter());

    this->expect(RP);
    std::unique_ptr<BlockNode> blockNode(this->parse_block());

    this->parse_statementEnd();
    return uniquify<ForNode>(n, initNode.release(), condNode.release(),
                             iterNode.release(), blockNode.release());
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
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_forCond() {
#define EACH_LA_cmdOrExpr(OP) \
    OP(COMMAND) \
    EACH_LA_expression(OP)

    switch(CUR_KIND()) {
    EACH_LA_cmdOrExpr(GEN_LA_CASE) {
        return this->parse_commandOrExpression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
#undef EACH_LA_cmdOrExpr
}

std::unique_ptr<Node> Parser::parse_forIter() {
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<CatchNode> Parser::parse_catchStatement() {
    this->expect(CATCH);
    unsigned int n = LN();
    this->expect(LP);
    Token token;
    this->expect(APPLIED_NAME, token);
    std::unique_ptr<TypeNode> typeToken;
    if(CUR_KIND() == COLON) {
        this->expect(COLON, false);
        typeToken = this->parse_typeName();
    }
    this->expect(RP);
    std::unique_ptr<BlockNode> blockNode(this->parse_block());

    if(typeToken) {
        return uniquify<CatchNode>(n, this->lexer->toName(token),
                                   typeToken.release(), blockNode.release());
    } else {
        return uniquify<CatchNode>(n, this->lexer->toName(token), blockNode.release());
    }
}

// command
std::unique_ptr<Node> Parser::parse_commandListExpression() {
    std::unique_ptr<Node> node(this->parse_orListCommand());
    if(dynamic_cast<UserDefinedCmdNode *>(node.get()) != nullptr) {
        return node;
    }
    return uniquify<CmdContextNode>(node.release());
}

std::unique_ptr<Node> Parser::parse_orListCommand() {
    std::unique_ptr<Node> node(this->parse_andListCommand());

    while(CUR_KIND() == OR_LIST) {
        this->expect(OR_LIST);
        std::unique_ptr<Node> rightNode(this->parse_andListCommand());
        node = uniquify<CondOpNode>(node.release(), rightNode.release(), false);
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_andListCommand() {
    std::unique_ptr<Node> node(this->parse_pipedCommand());

    while(CUR_KIND() == AND_LIST) {
        this->expect(AND_LIST);
        std::unique_ptr<Node> rightNode(this->parse_pipedCommand());
        node = uniquify<CondOpNode>(node.release(), rightNode.release(), true);
    }
    return node;
}

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
    Token token;
    this->expect(COMMAND, token);

    if(CUR_KIND() == LP) {  // command definition
        this->expect(LP);
        this->expect(RP);
        return uniquify<UserDefinedCmdNode>(
                token.lineNum, this->lexer->getSourceInfoPtr(), this->lexer->toCmdArg(token),
                this->parse_block().release());
    }


    std::unique_ptr<CmdNode> node;

    if(this->lexer->startsWith(token, '~')) {
        node = uniquify<CmdNode>(new TildeNode(token.lineNum, this->lexer->toCmdArg(token)));
    } else {
        node = uniquify<CmdNode>(token.lineNum, this->lexer->toCmdArg(token));
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
        TokenKind kind = this->consume();
        node->addRedirOption(kind);
        break;
    }
    default:
        E_ALTER(
                EACH_LA_redir(GEN_LA_ALTER)
                DUMMY
        );
        break;
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
        Token token;
        this->expect(CMD_ARG_PART, token);
        if(expandTilde && this->lexer->startsWith(token, '~')) {
            return uniquify<TildeNode>(token.lineNum, this->lexer->toCmdArg(token));
        }
        return uniquify<StringValueNode>(token.lineNum, this->lexer->toCmdArg(token));
    }
    case STRING_LITERAL: {
        return this->parse_stringLiteral();
    }
    case OPEN_DQUOTE: {
        return this->parse_stringExpression();
    }
    case START_SUB_CMD: {
        return this->parse_commandSubstitution();
    }
    EACH_LA_interpolation(GEN_LA_CASE) {
        return this->parse_interpolation(true);
    }
    default: {
        E_ALTER(
                EACH_LA_cmdArg(GEN_LA_ALTER)
                DUMMY
        );
        return std::unique_ptr<Node>(nullptr);
    }
    }
}

// expression
std::unique_ptr<Node> Parser::parse_commandOrExpression() {
    switch(CUR_KIND()) {
    case COMMAND:
        return this->parse_commandListExpression();
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        E_ALTER(
                EACH_LA_expression(GEN_LA_ALTER)
                COMMAND,
                DUMMY
        );
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_expression() {
    return this->parse_expression(
            this->parse_unaryExpression(), getPrecedence(ASSIGN));
}

std::unique_ptr<Node> Parser::parse_expression(std::unique_ptr<Node> &&leftNode,
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
        EACH_LA_assign(GEN_LA_CASE) {
            TokenKind op = this->consume();
            auto rightNode(this->parse_commandOrExpression());
            node.reset(createBinaryOpNode(node.release(), op, rightNode.release()));
            break;
        }
        default: {
            TokenKind op = this->consume();
            std::unique_ptr<Node> rightNode(this->parse_unaryExpression());
            for(unsigned int nextP = PRECEDENCE(); !HAS_NL() && nextP > p; nextP = PRECEDENCE()) {
                rightNode = this->parse_expression(std::move(rightNode), nextP);
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
        TokenKind op = this->consume();
        return uniquify<UnaryOpNode>(op, this->parse_unaryExpression().release());
    }
    default: {
        return this->parse_suffixExpression();
    }
    }
}

std::unique_ptr<Node> Parser::parse_suffixExpression() {
    std::unique_ptr<Node> node(this->parse_memberExpression());

    switch(CUR_KIND()) {
    case INC:
    case DEC:
        return std::unique_ptr<Node>(createSuffixNode(node.release(), this->consume()));
    default:
        return node;
    }
}

std::unique_ptr<Node> Parser::parse_memberExpression() {
    std::unique_ptr<Node> node(this->parse_primaryExpression());

    for(bool next = true; !HAS_NL() && next;) {
        switch(CUR_KIND()) {
        case ACCESSOR: {
            this->expect(ACCESSOR);
            Token token;
            this->expect(IDENTIFIER, token);
            std::string name(this->lexer->toName(token));
            node = uniquify<AccessNode>(node.release(), std::move(name));
            break;
        }
        case LB: {
            this->expect(LB);
            std::unique_ptr<Node> indexNode(this->parse_expression());
            this->expect(RB);
            node.reset(createIndexNode(node.release(), indexNode.release()));
            break;
        }
        case LP: {
            ArgsWrapper args(this->parse_arguments());
            node.reset(createCallNode(node.release(), std::move(args).remove()));
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
    unsigned int n = LN();
    switch(CUR_KIND()) {
    case NEW: {
        this->expect(NEW, false);
        std::unique_ptr<TypeNode> type(this->parse_typeName());
        ArgsWrapper args(this->parse_arguments());
        return uniquify<NewNode>(n, type.release(), std::move(args).remove());
    }
    case INT_LITERAL: {
        Token token;
        this->expect(INT_LITERAL, token);
        int value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt);
        return uniquify<IntValueNode>(n, value);
    }
    case BYTE_LITERAL: {
        Token token;
        this->expect(BYTE_LITERAL, token);
        unsigned char value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint8);
        return std::unique_ptr<Node>(IntValueNode::newByte(n, value));
    }
    case INT16_LITERAL: {
        Token token;
        this->expect(INT16_LITERAL, token);
        short value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt16);
        return std::unique_ptr<Node>(IntValueNode::newInt16(n, value));
    }
    case UINT16_LITERAL: {
        Token token;
        this->expect(UINT16_LITERAL, token);
        unsigned short value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint16);
        return std::unique_ptr<Node>(IntValueNode::newUint16(n, value));
    }
    case INT32_LITERAL: {
        Token token;
        this->expect(INT32_LITERAL, token);
        int value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt32);
        return std::unique_ptr<Node>(IntValueNode::newInt32(n, value));
    }
    case UINT32_LITERAL: {
        Token token;
        this->expect(UINT32_LITERAL, token);
        unsigned int value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint32);
        return std::unique_ptr<Node>(IntValueNode::newUint32(n, value));
    }
    case INT64_LITERAL: {
        Token token;
        this->expect(INT64_LITERAL, token);
        long value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt64);
        return std::unique_ptr<Node>(LongValueNode::newInt64(n, value));
    }
    case UINT64_LITERAL: {
        Token token;
        this->expect(UINT64_LITERAL, token);
        unsigned long value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint64);
        return std::unique_ptr<Node>(LongValueNode::newUint64(n, value));
    }
    case FLOAT_LITERAL: {
        Token token;
        this->expect(FLOAT_LITERAL, token);
        double value;
        CONVERT_TO_NUM(value, token, this->lexer->toDouble);
        return uniquify<FloatValueNode>(n, value);
    }
    case STRING_LITERAL: {
        return this->parse_stringLiteral();
    }
    case PATH_LITERAL: {
        Token token;
        this->expect(PATH_LITERAL, token);

        /**
         * skip prefix 'p'
         */
        token.startPos++;
        token.size--;
        return uniquify<ObjectPathNode>(n, this->lexer->singleToString(token));
    }
    case OPEN_DQUOTE: {
        return this->parse_stringExpression();
    }
    case START_SUB_CMD: {
        return this->parse_commandSubstitution();
    }
    case APPLIED_NAME: {
        return this->parse_appliedName();
    }
    case SPECIAL_NAME: {
        return this->parse_appliedName(true);
    }
    case LP: {  // group or tuple
        this->expect(LP);
        std::unique_ptr<Node> node(this->parse_expression());
        bool isTuple = false;
        if(CUR_KIND() == COMMA) {
            isTuple = true;
            this->expect(COMMA);
            std::unique_ptr<Node> rightNode(this->parse_expression());
            std::unique_ptr<TupleNode> tuple(
                    new TupleNode(n, node.release(), rightNode.release()));

            while(CUR_KIND() == COMMA) {
                this->expect(COMMA);
                tuple->addNode(this->parse_expression().release());
            }
            node.reset(tuple.release());
        }
        this->expect(RP);
        if(!isTuple) {
            return uniquify<GroupNode>(n, node.release());
        }
        return node;
    }
    case LB: {  // array or map
        this->expect(LB);

        std::unique_ptr<Node> keyNode(this->parse_expression());

        std::unique_ptr<Node> node;
        if(CUR_KIND() == COLON) {   // map
            this->expectAfter(COLON, yycSTMT);

            std::unique_ptr<Node> valueNode(this->parse_expression());
            std::unique_ptr<MapNode> mapNode(
                    new MapNode(n, keyNode.release(), valueNode.release()));
            while(CUR_KIND() == COMMA) {
                this->expect(COMMA);
                keyNode = this->parse_expression();
                this->expectAfter(COLON, yycSTMT);
                valueNode = this->parse_expression();
                mapNode->addEntry(keyNode.release(), valueNode.release());
            }
            node = std::move(mapNode);
        } else {    // array
            auto arrayNode = uniquify<ArrayNode>(n, keyNode.release());
            while(CUR_KIND() == COMMA) {
                this->expect(COMMA);
                arrayNode->addExprNode(this->parse_expression().release());
            }
            node = std::move(arrayNode);
        }

        this->expect(RB);
        return node;
    }
    default:
        E_ALTER(
                EACH_LA_primary(GEN_LA_ALTER)
                DUMMY
        );
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_appliedName(bool asSpecialName) {
    Token token;
    this->expect(asSpecialName ? SPECIAL_NAME : APPLIED_NAME, token);
    return uniquify<VarNode>(token.lineNum, this->lexer->toName(token));
}

std::unique_ptr<Node> Parser::parse_stringLiteral() {
    Token token;
    this->expect(STRING_LITERAL, token);
    return uniquify<StringValueNode>(token.lineNum, this->lexer->singleToString(token));
}

ArgsWrapper Parser::parse_arguments() {
    this->expect(LP);

    ArgsWrapper args;
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        args.addArgNode(this->parse_expression());
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA);
            args.addArgNode(this->parse_expression());
        }
        break;
    }
    default:  // no args
        break;
    }

    this->expect(RP);
    return args;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
    unsigned int n = LN();
    this->expect(OPEN_DQUOTE);
    std::unique_ptr<StringExprNode> node(new StringExprNode(n));

    for(bool next = true; next;) {
        switch(CUR_KIND()) {
        case STR_ELEMENT: {
            Token token;
            this->expect(STR_ELEMENT, token);
            node->addExprNode(
                    new StringValueNode(token.lineNum, this->lexer->doubleElementToString(token)));
            break;
        }
        EACH_LA_interpolation(GEN_LA_CASE) {
            node->addExprNode(this->parse_interpolation().release());
            break;
        }
        case START_SUB_CMD: {
            node->addExprNode(this->parse_commandSubstitution().release());
            break;
        }
        default: {
            next = false;
            break;
        }
        }
    }

    this->expect(CLOSE_DQUOTE);
    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_interpolation(bool asCmdArg) {
    switch(CUR_KIND()) {
    case APPLIED_NAME:
    case SPECIAL_NAME: {
        auto node(this->parse_appliedName(CUR_KIND() == SPECIAL_NAME));
        if(asCmdArg && CUR_KIND() == LB) {
            this->expect(LB);
            auto indexNode(this->parse_expression());
            this->expect(RB);
            node.reset(createIndexNode(node.release(), indexNode.release()));
        }
        return node;
    }
    case START_INTERP: {
        this->expect(START_INTERP);
        std::unique_ptr<Node> node(this->parse_expression());
        this->expect(RBC);
        return node;
    }
    default: {
        E_ALTER(
                EACH_LA_interpolation(GEN_LA_ALTER)
                DUMMY
        );
        return std::unique_ptr<Node>(nullptr);
    }
    }
}

std::unique_ptr<Node> Parser::parse_commandSubstitution() {
    this->expect(START_SUB_CMD);
    std::unique_ptr<Node> node(this->parse_commandListExpression());
    node->inCmdArgNode();
    this->expect(RP);
    return node;
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

// parser error
std::ostream &operator<<(std::ostream &stream, const OutOfRangeNumError &e) {
    stream << "out of range: " << e.getTokenKind();
    return stream;
}

} // namespace parser
} // namespace ydsh