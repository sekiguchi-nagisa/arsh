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
#include "../misc/debug.h"


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
    OP(LB) \
    OP(LBC)

#define EACH_LA_expression(OP) \
    OP(NOT) \
    OP(PLUS) \
    OP(MINUS) \
    EACH_LA_primary(OP)

#define EACH_LA_statement(OP) \
    OP(ASSERT) \
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

#define EACH_LA_toplevelStatement(OP) \
    OP(FUNCTION) \
    OP(INTERFACE) \
    OP(TYPE_ALIAS) \
    EACH_LA_statement(OP)

#define EACH_LA_typeName(OP) \
    OP(IDENTIFIER)

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


#define E_ALTER(alt) this->alternative(alt)

// for check converted number range
#define CONVERT_TO_NUM(out, token, func) \
    do {\
        int status;\
        out = func(token, status);\
        if(status != 0) { throw OutOfRangeNumError(token); }\
    } while(0)


#define RET_NODE(node) return std::unique_ptr<Node>(node)

#define PRECEDENCE() getPrecedence(CUR_KIND())

namespace ydsh {
namespace parser {

// ####################
// ##     Parser     ##
// ####################

void Parser::parse(Lexer &lexer, RootNode &rootNode) {
    this->lexer = &lexer;

    // first, fetch token.
    NEXT_TOKEN();

    // start parsing
    this->parse_toplevel(rootNode);
}

void Parser::noNewLine() {
    if(HAS_NL()) {
        throw UnexpectedNewLineError(this->curToken);
    }
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
    bool next = true;
    while(next) {
        switch(CUR_KIND()) {
        EACH_LA_toplevelStatement(GEN_LA_CASE) {
            // parse
            rootNode.addNode(this->parse_toplevelStatement().release());
            break;
        }
        default: {
            next = false;
            break;
        }
        }
    }

    this->expect(EOS);
}

std::unique_ptr<Node> Parser::parse_toplevelStatement() {
    static TokenKind alters[] = {
            EACH_LA_toplevelStatement(GEN_LA_ALTER)
            DUMMY
    };

    switch(CUR_KIND()) {
    case FUNCTION: {
        return this->parse_function();
    }
    case INTERFACE: {
        return this->parse_interface();
    };
    case TYPE_ALIAS: {
        return this->parse_typeAlias();
    };
    EACH_LA_statement(GEN_LA_CASE) {
        return this->parse_statement();
    }
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_function() {
    auto node(this->parse_funcDecl());
    node->setBlockNode(this->parse_block().release());
    return std::move(node);
}

std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    unsigned int n = LN();
    this->expect(FUNCTION);
    Token token;
    this->expect(IDENTIFIER, token);
    std::unique_ptr<FunctionNode> node(new FunctionNode(n, this->lexer->toName(token)));
    this->expect(LP);

    if(CUR_KIND() == APPLIED_NAME) {
        this->expect(APPLIED_NAME, token);
        std::unique_ptr<VarNode> nameNode(new VarNode(token.lineNum, this->lexer->toName(token)));
        this->noNewLine();
        this->expect(COLON, false);

        std::unique_ptr<TypeToken> type(this->parse_typeName());

        node->addParamNode(nameNode.release(), type.release());

        while(!HAS_NL() && CUR_KIND() == COMMA) {
            this->expect(COMMA);
            this->expect(APPLIED_NAME, token);

            nameNode.reset(new VarNode(token.lineNum, this->lexer->toName(token)));

            this->noNewLine();
            this->expect(COLON, false);

            type = this->parse_typeName();

            node->addParamNode(nameNode.release(), type.release());
        }
    }

    this->expect(RP);

    if(!HAS_NL() && CUR_KIND() == COLON) {
        this->expect(COLON, false);
        std::unique_ptr<ReturnTypeToken> type(new ReturnTypeToken(this->parse_typeName().release()));
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA, false);
            type->addTypeToken(this->parse_typeName().release());
        }
        node->setReturnTypeToken(type.release());
    }

    return std::move(node);
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

    std::unique_ptr<InterfaceNode> node(new InterfaceNode(n, this->lexer->toTokenText(token)));
    this->expect(LBC);

    static TokenKind alters[] = {
            FUNCTION,
            VAR,
            LET,
            DUMMY,
    };

    bool next = true;
    unsigned int count = 0;
    while(next) {
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
            break;
        };
        case FUNCTION: {
            auto funcNode(this->parse_funcDecl());
            this->parse_statementEnd();
            node->addMethodDeclNode(funcNode.release());
            break;
        };
        default:
            next = false;
            if(count == 0) {
                E_ALTER(alters);
            }
            break;
        }
        count++;
    }

    this->expect(RBC);

    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_typeAlias() {
    unsigned int n = LN();
    this->expect(TYPE_ALIAS);
    Token token;
    this->expect(IDENTIFIER, token, false);
    auto typeToken = this->parse_typeName();
    RET_NODE(new TypeAliasNode(n, this->lexer->toTokenText(token), typeToken.release()));
}

void Parser::restoreLexerState(const Token &prevToken) {
    unsigned int pos = prevToken.startPos + prevToken.size;
    this->lexer->setPos(pos);
    this->lexer->popLexerMode();
    this->lexer->setLineNum(prevToken.lineNum);
    NEXT_TOKEN();
}

std::unique_ptr<TypeToken> Parser::parse_typeName() {
    static TokenKind alters[] = {
            EACH_LA_typeName(GEN_LA_ALTER)
            FUNC,
            TYPE_PATH,
            DUMMY
    };

    // change lexer state to TYPE
    this->lexer->pushLexerMode(yycTYPE);
    NEXT_TOKEN();

    switch(CUR_KIND()) {
    EACH_LA_typeName(GEN_LA_CASE) {
        Token token;
        this->expect(IDENTIFIER, token);
        std::unique_ptr<ClassTypeToken> typeToken(
                new ClassTypeToken(token.lineNum, this->lexer->toName(token)));
        if(!HAS_NL() && CUR_KIND() == TYPE_OPEN) {
            this->expect(TYPE_OPEN, false);

            std::unique_ptr<ReifiedTypeToken> reified(
                    new ReifiedTypeToken(typeToken.release()));
            reified->addElementTypeToken(this->parse_typeName().release());

            while(CUR_KIND() == TYPE_SEP) {
                this->expect(TYPE_SEP, false);
                reified->addElementTypeToken(this->parse_typeName().release());
            }
            this->expect(TYPE_CLOSE, token);

            this->restoreLexerState(token);
            return std::move(reified);
        }

        this->restoreLexerState(token);
        return std::move(typeToken);
    }
    case FUNC: {
        this->expect(FUNC);
        this->noNewLine();
        this->expect(TYPE_OPEN, false);

        // parse return type
        std::unique_ptr<FuncTypeToken> func(
                new FuncTypeToken(this->parse_typeName().release()));

        if(CUR_KIND() == TYPE_SEP) {   // ,[
            this->expect(TYPE_SEP);
            this->expect(PTYPE_OPEN, false);

            // parse first arg type
            func->addParamTypeToken(this->parse_typeName().release());

            // rest arg type
            while(CUR_KIND() == TYPE_SEP) {
                this->expect(TYPE_SEP, false);
                func->addParamTypeToken(this->parse_typeName().release());
            }
            this->expect(PTYPE_CLOSE);
        }

        Token token;
        this->expect(TYPE_CLOSE, token);

        this->restoreLexerState(token);
        return std::move(func);
    }
    case TYPE_PATH: {
        Token token;
        this->expect(TYPE_PATH, token);
        this->restoreLexerState(token);
        return std::unique_ptr<TypeToken>(new DBusInterfaceToken(token.lineNum, this->lexer->toTokenText(token)));
    };
    default:
        E_ALTER(alters);
        return std::unique_ptr<TypeToken>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_statement() {
    static TokenKind alters[] = {
            EACH_LA_statement(GEN_LA_ALTER)
            DUMMY
    };

    unsigned int n = LN();

    switch(CUR_KIND()) {
    case LINE_END: {
        this->expect(LINE_END);
        RET_NODE(new EmptyNode(n));
    }
    case ASSERT: {
        this->expect(ASSERT);
        this->expect(LP);
        std::unique_ptr<Node> node(
                new AssertNode(n, this->parse_commandOrExpression().release()));
        this->expect(RP);
        this->parse_statementEnd();
        return node;
    }
    case BREAK: {
        this->expect(BREAK);
        std::unique_ptr<Node> node(new BreakNode(n));
        this->parse_statementEnd();
        return node;
    }
    case CONTINUE: {
        this->expect(BREAK);
        std::unique_ptr<Node> node(new ContinueNode(n));
        this->parse_statementEnd();
        return node;
    }
    case EXPORT_ENV: {
        this->expect(EXPORT_ENV);
        Token token;
        this->expect(IDENTIFIER, token);
        std::string name(
                this->lexer->toName(token));
        this->noNewLine();
        this->expect(ASSIGN);
        std::unique_ptr<Node> node(new ExportEnvNode(n, std::move(name),
                                                     this->parse_expression().release()));
        this->parse_statementEnd();
        return node;
    }
    case FOR: {
        return this->parse_forStatement();
    }
    case IF: {
        this->expect(IF);
        this->expect(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        std::unique_ptr<IfNode> ifNode(new IfNode(n,
                                                  condNode.release(), blockNode.release()));

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
        return std::move(ifNode);
    }
    case IMPORT_ENV: {
        this->expect(IMPORT_ENV);
        Token token;
        this->expect(IDENTIFIER, token);
        std::unique_ptr<Node> node(
                new ImportEnvNode(n, this->lexer->toName(token)));
        this->parse_statementEnd();
        return node;
    }
    case RETURN: {
        this->expect(RETURN);
        std::unique_ptr<Node> node(nullptr);

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
            node.reset(new ReturnNode(n, this->parse_expression().release()));
        } else {
            node.reset(new ReturnNode(n));
        }
        this->parse_statementEnd();
        return node;
    }
    case THROW: {
        this->expect(THROW);
        std::unique_ptr<Node> node(
                new ThrowNode(n, this->parse_expression().release()));
        this->parse_statementEnd();
        return node;
    }
    case WHILE: {
        this->expect(WHILE);
        this->expect(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        RET_NODE(new WhileNode(n, condNode.release(), blockNode.release()));
    }
    case DO: {
        this->expect(DO);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        this->expect(WHILE);
        this->expect(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->expect(RP);
        std::unique_ptr<Node> node(new DoWhileNode(n, blockNode.release(), condNode.release()));
        this->parse_statementEnd();
        return node;
    }
    case TRY: {
        this->expect(TRY);
        std::unique_ptr<TryNode> tryNode(new TryNode(n, this->parse_block().release()));

        // parse catch
        bool next = true;
        while(next) {
            if(CUR_KIND() == CATCH) {
                tryNode->addCatchNode(this->parse_catchStatement().release());
            } else {
                next = false;
            }
        }

        // parse finally
        if(CUR_KIND() == FINALLY) {
            this->expect(FINALLY);
            tryNode->addFinallyNode(this->parse_block().release());
        }
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
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
    }
}

void Parser::parse_statementEnd() {
    switch(CUR_KIND()) {
    case EOS:
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
    std::unique_ptr<BlockNode> blockNode(new BlockNode(n));

    blockNode->addNode(this->parse_statement().release());

    bool next = true;
    while(next) {
        switch(CUR_KIND()) {
        EACH_LA_statement(GEN_LA_CASE) {
            blockNode->addNode(this->parse_statement().release());
            break;
        }
        default: {
            next = false;
            break;
        }
        }
    }

    this->expect(RBC);

    return blockNode;
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
    static TokenKind alters[] = {
            EACH_LA_varDecl(GEN_LA_ALTER)
            DUMMY
    };

    switch(CUR_KIND()) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        bool readOnly = this->consume() != VAR;

        Token token;
        this->expect(IDENTIFIER, token);
        std::string name(this->lexer->toName(token));
        this->noNewLine();
        this->expect(ASSIGN);
        RET_NODE(new VarDeclNode(token.lineNum, std::move(name),
                                 this->parse_commandOrExpression().release(), readOnly));
    }
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_forStatement() {
    unsigned int n = LN();
    this->expect(FOR);
    this->expect(LP);

    std::unique_ptr<Node> initNode(this->parse_forInit());

    VarNode *varNode = dynamic_cast<VarNode *>(initNode.get());
    if(varNode != 0) { // treat as for-in
        initNode.release();
        std::unique_ptr<VarNode> nameNode(varNode);

        this->noNewLine();
        this->expect(IN);
        std::unique_ptr<Node> exprNode(this->parse_expression());
        this->expect(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());

        RET_NODE(createForInNode(n, nameNode.release(),
                                 exprNode.release(), blockNode.release()));
    }

    this->expect(LINE_END);

    std::unique_ptr<Node> condNode(this->parse_forCond());
    this->expect(LINE_END);

    std::unique_ptr<Node> iterNode(this->parse_forIter());

    this->expect(RP);
    std::unique_ptr<BlockNode> blockNode(this->parse_block());

    RET_NODE(new ForNode(n, initNode.release(), condNode.release(),
                         iterNode.release(), blockNode.release()));
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
    std::unique_ptr<TypeToken> typeToken;
    if(!HAS_NL() && CUR_KIND() == COLON) {
        this->expect(COLON, false);
        typeToken = this->parse_typeName();
    }
    this->expect(RP);
    std::unique_ptr<BlockNode> blockNode(this->parse_block());

    if(typeToken) {
        return std::unique_ptr<CatchNode>(new CatchNode(n, this->lexer->toName(token),
                                                        typeToken.release(), blockNode.release()));
    } else {
        return std::unique_ptr<CatchNode>(new CatchNode(n, this->lexer->toName(token),
                                                        blockNode.release()));
    }
}

// command
std::unique_ptr<Node> Parser::parse_commandListExpression() {
    RET_NODE(new CmdContextNode(this->parse_orListCommand().release()));
}

std::unique_ptr<Node> Parser::parse_orListCommand() {
    std::unique_ptr<Node> node(this->parse_andListCommand());

    while(CUR_KIND() == OR_LIST) {
        this->expect(OR_LIST);
        std::unique_ptr<Node> rightNode(this->parse_andListCommand());
        node.reset(new CondOpNode(node.release(), rightNode.release(), false));
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_andListCommand() {
    std::unique_ptr<Node> node(this->parse_pipedCommand());

    while(CUR_KIND() == AND_LIST) {
        this->expect(AND_LIST);
        std::unique_ptr<Node> rightNode(this->parse_pipedCommand());
        node.reset(new CondOpNode(node.release(), rightNode.release(), true));
    }
    return node;
}

std::unique_ptr<Node> Parser::parse_pipedCommand() {
    std::unique_ptr<PipedCmdNode> node(new PipedCmdNode(this->parse_command().release()));

    if(CUR_KIND() == PIPE) {
        while(CUR_KIND() == PIPE) {
            this->expect(PIPE);
            node->addCmdNodes(this->parse_command().release());
        }
    }
    return std::move(node);
}

std::unique_ptr<CmdNode> Parser::parse_command() {
    Token token;
    this->expect(COMMAND, token);
    std::unique_ptr<CmdNode> node;

    {
        std::string rest;
        std::string prefix;
        if(this->checkTildeExpansion(token, prefix, rest)) {
            node.reset(new CmdNode(new TildeNode(token.lineNum, std::move(prefix), std::move(rest))));
        } else {
            node.reset(new CmdNode(token.lineNum, this->lexer->toCmdArg(token)));
        }
    }

    bool next = true;
    while(HAS_SPACE() && next) {
        switch(CUR_KIND()) {
        EACH_LA_cmdArg(GEN_LA_CASE) {
            node->addArgNode(this->parse_cmdArg().release());
            break;
        };
        EACH_LA_redir(GEN_LA_CASE) {
            this->parse_redirOption(node);
            break;
        };
        default:
            next = false;
            break;
        }
    }
    return node;
}

void Parser::parse_redirOption(std::unique_ptr<CmdNode> &node) {
    static TokenKind alters[] = {
            EACH_LA_redir(GEN_LA_ALTER)
            DUMMY
    };

    switch(CUR_KIND()) {
    EACH_LA_redirFile(GEN_LA_CASE) {
        TokenKind kind = this->consume();
        node->addRedirOption(kind, this->parse_cmdArg().release());
        break;
    };
    EACH_LA_redirNoFile(GEN_LA_CASE) {
        TokenKind kind = this->consume();
        node->addRedirOption(kind);
        break;
    };
    default:
        E_ALTER(alters);
        break;
    }
}

std::unique_ptr<CmdArgNode> Parser::parse_cmdArg() {
    std::unique_ptr<CmdArgNode> node(new CmdArgNode(this->parse_cmdArgSeg(true).release()));

    bool next = true;
    while(!HAS_SPACE() && next) {
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
    static TokenKind alters[] = {
            EACH_LA_cmdArg(GEN_LA_ALTER)
            DUMMY
    };

    switch(CUR_KIND()) {
    case CMD_ARG_PART: {
        Token token;
        this->expect(CMD_ARG_PART, token);
        if(expandTilde) {
            std::string prefix;
            std::string rest;
            if(checkTildeExpansion(token, prefix, rest)) {
                RET_NODE(new TildeNode(token.lineNum, std::move(prefix), std::move(rest)));
            }
        }
        RET_NODE(new StringValueNode(token.lineNum, this->lexer->toCmdArg(token)));
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
        return this->parse_interpolation();
    }
    default: {
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
    }
}

bool Parser::checkTildeExpansion(const Token &token, std::string &prefix, std::string &rest) {
    if(!this->lexer->startsWith(token, '~')) {
        return false;
    }

    const unsigned int size = token.size;
    prefix.clear();
    rest.clear();

    if(size == 1) {
        return true;
    }

    char buf[size + 1];
    this->lexer->copyTokenText(token, buf);
    buf[size] = '\0';

    unsigned int index = 1;
    for(; buf[index] != '/' && index < size; index++) {
        prefix += buf[index];
    }

    for(; index < size; index++) {
        char ch = buf[index];
        if(ch == '\\') {
            char nextCh = buf[++index];
            switch(nextCh) {
            case '\n':
            case '\r':
                continue;
            default:
                ch = nextCh;
                break;
            }
        }
        rest += ch;
    }

    return true;
}

// expression
std::unique_ptr<Node> Parser::parse_commandOrExpression() {
#define EACH_LA_condExpr(OP) \
    OP(COMMAND) \
    EACH_LA_expression(OP)

    static TokenKind alters[] = {
            EACH_LA_condExpr(GEN_LA_ALTER)
            DUMMY
    };

#undef EACH_LA_condExpr

    switch(CUR_KIND()) {
    case COMMAND:
        return this->parse_commandListExpression();
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        E_ALTER(alters);
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
            std::unique_ptr<TypeToken> type(this->parse_typeName());
            node.reset(new CastNode(node.release(), type.release()));
            break;
        };
        case IS: {
            this->expect(IS, false);
            std::unique_ptr<TypeToken> type(this->parse_typeName());
            node.reset(new InstanceOfNode(node.release(), type.release()));
            break;
        };
        EACH_LA_assign(GEN_LA_CASE) {
            TokenKind op = this->consume();
            auto rightNode(this->parse_commandOrExpression());
            node.reset(createBinaryOpNode(node.release(), op, rightNode.release()));
            break;
        };
        default: {
            TokenKind op = this->consume();
            std::unique_ptr<Node> rightNode(this->parse_unaryExpression());
            for(unsigned int nextP = PRECEDENCE(); nextP > p; nextP = PRECEDENCE()) {
                rightNode = this->parse_expression(std::move(rightNode), nextP);
            }
            node.reset(createBinaryOpNode(node.release(), op, rightNode.release()));
            break;
        };
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
        RET_NODE(new UnaryOpNode(op, this->parse_unaryExpression().release()));
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
        RET_NODE(createSuffixNode(node.release(), this->consume()));
    default:
        return node;
    }
}

std::unique_ptr<Node> Parser::parse_memberExpression() {
    std::unique_ptr<Node> node(this->parse_primaryExpression());

    bool next = true;
    while(!HAS_NL() && next) {
        switch(CUR_KIND()) {
        case ACCESSOR: {
            this->expect(ACCESSOR);
            this->noNewLine();
            Token token;
            this->expect(IDENTIFIER, token);
            std::string name(this->lexer->toName(token));
            node.reset(new AccessNode(node.release(), std::move(name)));
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
            std::unique_ptr<ArgsNode> args(this->parse_arguments());
            node.reset(createCallNode(node.release(), args.release()));
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
    static TokenKind alters[] = {
            EACH_LA_primary(GEN_LA_ALTER)
            DUMMY
    };

    unsigned int n = LN();
    switch(CUR_KIND()) {
    case NEW: {
        this->expect(NEW, false);
        std::unique_ptr<TypeToken> type(this->parse_typeName());
        std::unique_ptr<ArgsNode> args(this->parse_arguments());
        RET_NODE(new NewNode(n, type.release(), args.release()));
    }
    case INT_LITERAL: {
        Token token;
        this->expect(INT_LITERAL, token);
        int value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt);
        RET_NODE(new IntValueNode(n, value));
    }
    case BYTE_LITERAL : {
        Token token;
        this->expect(BYTE_LITERAL, token);
        unsigned char value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint8);
        RET_NODE(IntValueNode::newByte(n, value));
    };
    case INT16_LITERAL : {
        Token token;
        this->expect(INT16_LITERAL, token);
        short value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt16);
        RET_NODE(IntValueNode::newInt16(n, value));
    };
    case UINT16_LITERAL : {
        Token token;
        this->expect(UINT16_LITERAL, token);
        unsigned short value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint16);
        RET_NODE(IntValueNode::newUint16(n, value));
    };
    case INT32_LITERAL : {
        Token token;
        this->expect(INT32_LITERAL, token);
        int value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt32);
        RET_NODE(IntValueNode::newInt32(n, value));
    };
    case UINT32_LITERAL : {
        Token token;
        this->expect(UINT32_LITERAL, token);
        unsigned int value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint32);
        RET_NODE(IntValueNode::newUint32(n, value));
    };
    case INT64_LITERAL: {
        Token token;
        this->expect(INT64_LITERAL, token);
        long value;
        CONVERT_TO_NUM(value, token, this->lexer->toInt64);
        RET_NODE(LongValueNode::newInt64(n, value));
    };
    case UINT64_LITERAL: {
        Token token;
        this->expect(UINT64_LITERAL, token);
        unsigned long value;
        CONVERT_TO_NUM(value, token, this->lexer->toUint64);
        RET_NODE(LongValueNode::newUint64(n, value));
    };
    case FLOAT_LITERAL: {
        Token token;
        this->expect(FLOAT_LITERAL, token);
        double value;
        CONVERT_TO_NUM(value, token, this->lexer->toDouble);
        RET_NODE(new FloatValueNode(n, value));
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
        RET_NODE(new ObjectPathNode(n, this->lexer->toString(token)));
    };
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
        return this->parse_specialName();
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
            return std::unique_ptr<Node>(new GroupNode(n, node.release()));
        }
        return node;
    }
    case LB: {
        this->expect(LB);
        std::unique_ptr<ArrayNode> node(
                new ArrayNode(n, this->parse_expression().release()));
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA);
            node->addExprNode(this->parse_expression().release());
        }
        this->expect(RB);
        return std::move(node);
    }
    case LBC: {
        this->expect(LBC);
        std::unique_ptr<Node> keyNode(this->parse_expression());

        this->noNewLine();
        this->expect(COLON);
        this->noNewLine();

        std::unique_ptr<Node> valueNode(this->parse_expression());
        std::unique_ptr<MapNode> node(
                new MapNode(n, keyNode.release(), valueNode.release()));
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA);
            keyNode = this->parse_expression();

            this->noNewLine();
            this->expect(COLON);
            this->noNewLine();

            valueNode = this->parse_expression();
            node->addEntry(keyNode.release(), valueNode.release());
        }
        this->expect(RBC);
        return std::move(node);
    }
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

std::unique_ptr<Node> Parser::parse_appliedName() {
    Token token;
    this->expect(APPLIED_NAME, token);
    RET_NODE(new VarNode(token.lineNum, this->lexer->toName(token)));
}

std::unique_ptr<Node> Parser::parse_specialName() {
    Token token;
    this->expect(SPECIAL_NAME, token);
    RET_NODE(new VarNode(token.lineNum, this->lexer->toName(token)));
}

std::unique_ptr<Node> Parser::parse_stringLiteral() {
    Token token;
    this->expect(STRING_LITERAL, token);
    RET_NODE(new StringValueNode(token.lineNum, this->lexer->singleToString(token)));
}

std::unique_ptr<ArgsNode> Parser::parse_arguments() {
    this->expect(LP);

    std::unique_ptr<ArgsNode> node(new ArgsNode());
    switch(CUR_KIND()) {
    EACH_LA_expression(GEN_LA_CASE) {
        node->addArg(this->parse_expression().release());
        while(CUR_KIND() == COMMA) {
            this->expect(COMMA);
            node->addArg(this->parse_expression().release());
        }
        break;
    }
    default:  // no args
        break;
    }

    this->expect(RP);
    return node;
}

std::unique_ptr<Node> Parser::parse_stringExpression() {
    unsigned int n = LN();
    this->expect(OPEN_DQUOTE);
    std::unique_ptr<StringExprNode> node(new StringExprNode(n));

    bool next = true;
    while(next) {
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

std::unique_ptr<Node> Parser::parse_interpolation() {
    static TokenKind alters[] = {
            EACH_LA_interpolation(GEN_LA_ALTER)
            DUMMY
    };

    switch(CUR_KIND()) {
    case APPLIED_NAME: {
        return this->parse_appliedName();
    }
    case SPECIAL_NAME: {
        return this->parse_specialName();
    }
    case START_INTERP: {
        this->expect(START_INTERP);
        std::unique_ptr<Node> node(this->parse_expression());
        this->expect(RBC);
        return node;
    }
    default: {
        E_ALTER(alters);
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

    Lexer lexer(fp);
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

std::ostream &operator<<(std::ostream &stream, const UnexpectedNewLineError &e) {
    stream << "unexpected new line before: " << e.getTokenKind();
    return stream;
}

} // namespace parser
} // namespace ydsh