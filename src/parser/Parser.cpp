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

#include <parser/Parser.h>
#include <parser/ParseError.h>
#include <misc/debug.h>

// for debug
#ifdef NDEBUG
#define INLINE inline
#else
#define INLINE
#endif


// helper macro
#define NEXT_TOKEN() \
    do {\
        this->curTokenKind = this->lexer->nextToken(this->curToken);\
    } while(0)

#define LN() this->lexer->getLineNum()

#define HAS_NL() this->lexer->isPrevNewLine()

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
    OP(CLASS) \
    OP(FUNCTION) \
    OP(INTERFACE) \
    OP(TYPE_ALIAS) \
    EACH_LA_statement(OP)

#define EACH_LA_typeName(OP) \
    OP(IDENTIFIER)

#define EACH_LA_varDecl(OP) \
    OP(VAR) \
    OP(LET)

#define EACH_LA_redir(OP) \
    OP(REDIR_OP) \
    OP(REDIR_OP_NO_ARG)

#define EACH_LA_cmdArg(OP) \
    OP(CMD_ARG_PART) \
    OP(STRING_LITERAL) \
    OP(OPEN_DQUOTE) \
    OP(START_SUB_CMD) \
    EACH_LA_interpolation(OP)


#define E_ALTER(alt) \
    do {\
        if(this->curTokenKind == INVALID) {\
            throw InvalidTokenError(LN(), this->curToken);\
        }\
        throw NoViableAlterError(LN(), this->curTokenKind, this->curToken, alt);\
    } while(0)

// for check converted number range
#define CONVERT_TO_NUM(out, n, kind, token, func) \
    do {\
        int status;\
        out = func(token, status);\
        if(status != 0) { throw OutOfRangeNumError(n, kind, token); }\
    } while(0)


#define RET_NODE(node) return std::unique_ptr<Node>(node)

#define PRECEDENCE() getPrecedence(this->curTokenKind)

namespace ydsh {
namespace parser {

// ####################
// ##     Parser     ##
// ####################

Parser::Parser() :
        lexer(), curTokenKind(), curToken() {
}

Parser::~Parser() {
}

void Parser::parse(Lexer<LexerDef, TokenKind> &lexer, RootNode &rootNode) {
    this->lexer = &lexer;

    // first, fetch token.
    NEXT_TOKEN();

    // start parsing
    this->parse_toplevel(rootNode);
}

INLINE void Parser::matchToken(TokenKind expected, bool fetchNext) {
    if(this->curTokenKind != expected) {
        if(this->curTokenKind == INVALID) {
            throw InvalidTokenError(LN(), this->curToken);
        }
        throw TokenMismatchError(LN(), this->curTokenKind, this->curToken, expected);
    }
    if(fetchNext) { NEXT_TOKEN(); }
}

INLINE Token Parser::matchAndGetToken(TokenKind expected, bool fetchNext) {
    if(this->curTokenKind != expected) {
        if(this->curTokenKind == INVALID) {
            throw InvalidTokenError(LN(), this->curToken);
        }
        throw TokenMismatchError(LN(), this->curTokenKind, this->curToken, expected);
    }
    Token token(this->curToken);
    if(fetchNext) { NEXT_TOKEN(); }
    return token;
}

INLINE TokenKind Parser::consumeAndGetKind() {
    TokenKind kind = this->curTokenKind;
    NEXT_TOKEN();
    return kind;
}

INLINE void Parser::hasNoNewLine() {
    if(HAS_NL()) {
        throw TokenMismatchError(LN(), NEW_LINE, this->curToken, this->curTokenKind);
    }
}

// parse rule definition

INLINE void Parser::parse_toplevel(RootNode & rootNode) {
    bool next = true;
    while(next) {
        switch(this->curTokenKind) {
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

    this->matchToken(EOS);
}

INLINE std::unique_ptr<Node> Parser::parse_toplevelStatement() {
    static TokenKind alters[] = {
            EACH_LA_toplevelStatement(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    case CLASS: {
        fatal("not implemented rule: %s\n", getTokenName(this->curTokenKind)); //FIXME:
        return std::unique_ptr<Node>(nullptr);
    }
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

INLINE std::unique_ptr<Node> Parser::parse_function() {
    auto node(this->parse_funcDecl());
    node->setBlockNode(this->parse_block().release());
    return std::move(node);
}

INLINE std::unique_ptr<FunctionNode> Parser::parse_funcDecl() {
    unsigned int n = LN();
    this->matchToken(FUNCTION);
    std::unique_ptr<FunctionNode> node(
            new FunctionNode(n, this->lexer->toName(this->matchAndGetToken(IDENTIFIER))));
    this->matchToken(LP);

    if(this->curTokenKind == APPLIED_NAME) {
        std::unique_ptr<VarNode> nameNode(
                new VarNode(LN(), this->lexer->toName(this->matchAndGetToken(APPLIED_NAME))));
        this->hasNoNewLine();
        this->matchToken(COLON, false);

        std::unique_ptr<TypeToken> type(this->parse_typeName());

        node->addParamNode(nameNode.release(), type.release());

        while(!HAS_NL() && this->curTokenKind == COMMA) {
            this->matchToken(COMMA);

            nameNode.reset(new VarNode(LN(),
                                       this->lexer->toName(this->matchAndGetToken(APPLIED_NAME))));
            this->hasNoNewLine();
            this->matchToken(COLON, false);

            type = this->parse_typeName();

            node->addParamNode(nameNode.release(), type.release());
        }
    }

    this->matchToken(RP);

    if(!HAS_NL() && this->curTokenKind == COLON) {
        this->matchToken(COLON, false);
        auto type(this->parse_typeName());
        if(this->curTokenKind != COMMA) {
            node->setReturnTypeToken(type.release());
        } else {
            std::unique_ptr<ReifiedTypeToken> tuple(newTupleTypeToken(type.release()));
            while(this->curTokenKind == COMMA) {
                this->matchToken(COMMA, false);
                tuple->addElementTypeToken(this->parse_typeName().release());
            }
            node->setReturnTypeToken(tuple.release());
        }
    }

    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_interface() {
    unsigned int n = LN();

    this->matchToken(INTERFACE, false);

    // enter TYPE mode
    this->lexer->modeStack.push_back(yycTYPE);
    NEXT_TOKEN();

    unsigned int prevNum = LN();
    Token token = this->matchAndGetToken(TYPE_PATH);

    // exit TYPE mode
    this->restoreLexerState(prevNum, token);

    std::unique_ptr<InterfaceNode> node(new InterfaceNode(n, this->lexer->toTokenText(token)));
    this->matchToken(LBC);

    static TokenKind alters[] = {
            FUNCTION,
            VAR,
            LET,
            DUMMY,
    };

    bool next = true;
    unsigned int count = 0;
    while(next) {
        switch(this->curTokenKind) {
        case VAR:
        case LET: {
            unsigned int n = LN();
            bool readOnly = this->consumeAndGetKind() == LET;
            Token token = this->matchAndGetToken(IDENTIFIER);
            this->matchToken(COLON, false);
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

    this->matchToken(RBC);

    return std::move(node);
}

std::unique_ptr<Node> Parser::parse_typeAlias() {
    unsigned int n = LN();
    this->matchToken(TYPE_ALIAS);
    Token token = this->matchAndGetToken(IDENTIFIER, false);
    auto typeToken = this->parse_typeName();
    RET_NODE(new TypeAliasNode(n, this->lexer->toTokenText(token), typeToken.release()));
}

void Parser::restoreLexerState(unsigned int prevLineNum, const Token &prevToken) {
    unsigned int pos = prevToken.startPos + prevToken.size;
    this->lexer->setPos(pos);
    this->lexer->modeStack.pop_back();
    this->lexer->setLineNum(prevLineNum);
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
    this->lexer->modeStack.push_back(yycTYPE);
    NEXT_TOKEN();

    switch(this->curTokenKind) {
    EACH_LA_typeName(GEN_LA_CASE) {
        unsigned int n = LN();
        Token token = this->matchAndGetToken(IDENTIFIER);
        std::unique_ptr<ClassTypeToken> typeToken(
                new ClassTypeToken(n, this->lexer->toName(token)));
        if(!HAS_NL() && this->curTokenKind == TYPE_OPEN) {
            this->matchToken(TYPE_OPEN, false);

            std::unique_ptr<ReifiedTypeToken> reified(
                    new ReifiedTypeToken(typeToken.release()));
            reified->addElementTypeToken(this->parse_typeName().release());

            while(this->curTokenKind == TYPE_SEP) {
                this->matchToken(TYPE_SEP, false);
                reified->addElementTypeToken(this->parse_typeName().release());
            }
            n = LN();
            token = this->matchAndGetToken(TYPE_CLOSE);

            this->restoreLexerState(n, token);
            return std::move(reified);
        }

        this->restoreLexerState(n, token);
        return std::move(typeToken);
    }
    case FUNC: {
        this->matchToken(FUNC);
        this->hasNoNewLine();
        this->matchToken(TYPE_OPEN, false);

        // parse return type
        std::unique_ptr<FuncTypeToken> func(
                new FuncTypeToken(this->parse_typeName().release()));

        if(this->curTokenKind == TYPE_SEP) {   // ,[
            this->matchToken(TYPE_SEP);
            this->matchToken(PTYPE_OPEN, false);

            // parse first arg type
            func->addParamTypeToken(this->parse_typeName().release());

            // rest arg type
            while(this->curTokenKind == TYPE_SEP) {
                this->matchToken(TYPE_SEP, false);
                func->addParamTypeToken(this->parse_typeName().release());
            }
            this->matchToken(PTYPE_CLOSE);
        }

        unsigned int n = LN();
        Token token = this->matchAndGetToken(TYPE_CLOSE);

        this->restoreLexerState(n, token);
        return std::move(func);
    }
    case TYPE_PATH: {
        unsigned  int n = LN();
        Token token = this->matchAndGetToken(TYPE_PATH);
        this->restoreLexerState(n, token);
        return std::unique_ptr<TypeToken>(new DBusInterfaceToken(n , this->lexer->toTokenText(token)));
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

    switch(this->curTokenKind) {
    case LINE_END: {
        this->matchToken(LINE_END);
        RET_NODE(new EmptyNode(n));
    }
    case ASSERT: {
        this->matchToken(ASSERT);
        this->matchToken(LP);
        std::unique_ptr<Node> node(
                new AssertNode(n, this->parse_commandOrExpression().release()));
        this->matchToken(RP);
        this->parse_statementEnd();
        return node;
    }
    case BREAK: {
        this->matchToken(BREAK);
        std::unique_ptr<Node> node(new BreakNode(n));
        this->parse_statementEnd();
        return node;
    }
    case CONTINUE: {
        this->matchToken(BREAK);
        std::unique_ptr<Node> node(new ContinueNode(n));
        this->parse_statementEnd();
        return node;
    }
    case EXPORT_ENV: {
        this->matchToken(EXPORT_ENV);
        std::string name(
                this->lexer->toName(this->matchAndGetToken(IDENTIFIER)));
        this->hasNoNewLine();
        this->matchToken(ASSIGN);
        std::unique_ptr<Node> node(new ExportEnvNode(n, std::move(name),
                                                     this->parse_expression().release()));
        this->parse_statementEnd();
        return node;
    }
    case FOR: {
        return this->parse_forStatement();
    }
    case IF: {
        this->matchToken(IF);
        this->matchToken(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->matchToken(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        std::unique_ptr<IfNode> ifNode(new IfNode(n,
                                                  condNode.release(), blockNode.release()));

        // parse elif
        while(this->curTokenKind == ELIF) {
            this->matchToken(ELIF);
            this->matchToken(LP);
            std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
            this->matchToken(RP);
            blockNode = this->parse_block();
            ifNode->addElifNode(condNode.release(), blockNode.release());
        }

        // parse else
        if(this->curTokenKind == ELSE) {
            this->matchToken(ELSE);
            ifNode->addElseNode(this->parse_block().release());
        }
        return  std::move(ifNode);
    }
    case IMPORT_ENV: {
        this->matchToken(IMPORT_ENV);
        std::unique_ptr<Node> node(
                new ImportEnvNode(n, this->lexer->toName(this->matchAndGetToken(IDENTIFIER))));
        this->parse_statementEnd();
        return node;
    }
    case RETURN: {
        this->matchToken(RETURN);
        std::unique_ptr<Node> node(nullptr);

        bool next;
        switch(this->curTokenKind) {
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
        this->matchToken(THROW);
        std::unique_ptr<Node> node(
                new ThrowNode(n, this->parse_expression().release()));
        this->parse_statementEnd();
        return node;
    }
    case WHILE: {
        this->matchToken(WHILE);
        this->matchToken(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->matchToken(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        RET_NODE(new WhileNode(n, condNode.release(), blockNode.release()));
    }
    case DO: {
        this->matchToken(DO);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());
        this->matchToken(WHILE);
        this->matchToken(LP);
        std::unique_ptr<Node> condNode(this->parse_commandOrExpression());
        this->matchToken(RP);
        std::unique_ptr<Node> node(new DoWhileNode(n, blockNode.release(), condNode.release()));
        this->parse_statementEnd();
        return node;
    }
    case TRY: {
        this->matchToken(TRY);
        std::unique_ptr<TryNode> tryNode(new TryNode(n, this->parse_block().release()));

        // parse catch
        bool next = true;
        while(next) {
            if(this->curTokenKind == CATCH) {
                tryNode->addCatchNode(this->parse_catchStatement().release());
            } else {
                next = false;
            }
        }

        // parse finally
        if(this->curTokenKind == FINALLY) {
            this->matchToken(FINALLY);
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

INLINE void Parser::parse_statementEnd() {
    switch(this->curTokenKind) {
    case EOS:
    case LINE_END:
        NEXT_TOKEN();
        break;
    default:
        if(!HAS_NL()) {
            throw TokenMismatchError(LN(), this->curTokenKind, this->curToken, NEW_LINE);
        }
        break;
    }
}

std::unique_ptr<BlockNode> Parser::parse_block() {
    unsigned int n = LN();
    this->matchToken(LBC);
    std::unique_ptr<BlockNode> blockNode(new BlockNode(n));

    blockNode->addNode(this->parse_statement().release());

    bool next = true;
    while(next) {
        switch(this->curTokenKind) {
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

    this->matchToken(RBC);

    return blockNode;
}

INLINE std::unique_ptr<Node> Parser::parse_variableDeclaration() {
    static TokenKind alters[] = {
            EACH_LA_varDecl(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        bool readOnly = this->consumeAndGetKind() != VAR;

        unsigned int n = LN();
        std::string name(
                this->lexer->toName(this->matchAndGetToken(IDENTIFIER)));
        this->hasNoNewLine();
        this->matchToken(ASSIGN);
        RET_NODE(new VarDeclNode(n, std::move(name),
                                 this->parse_commandOrExpression().release(), readOnly));
    }
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

INLINE std::unique_ptr<Node> Parser::parse_forStatement() {
    unsigned int n = LN();
    this->matchToken(FOR);
    this->matchToken(LP);

    std::unique_ptr<Node> initNode(this->parse_forInit());

    VarNode *varNode = dynamic_cast<VarNode *>(initNode.get());
    if(varNode != 0) { // treat as for-in
        initNode.release();
        std::unique_ptr<VarNode> nameNode(varNode);

        this->hasNoNewLine();
        this->matchToken(IN);
        std::unique_ptr<Node> exprNode(this->parse_expression());
        this->matchToken(RP);
        std::unique_ptr<BlockNode> blockNode(this->parse_block());

        RET_NODE(createForInNode(n, nameNode.release(),
                                 exprNode.release(), blockNode.release()));
    }

    this->matchToken(LINE_END);

    std::unique_ptr<Node> condNode(this->parse_forCond());
    this->matchToken(LINE_END);

    std::unique_ptr<Node> iterNode(this->parse_forIter());

    this->matchToken(RP);
    std::unique_ptr<BlockNode> blockNode(this->parse_block());

    RET_NODE(new ForNode(n, initNode.release(), condNode.release(),
                         iterNode.release(), blockNode.release()));
}

INLINE std::unique_ptr<Node> Parser::parse_forInit() {
    switch(this->curTokenKind) {
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

INLINE std::unique_ptr<Node> Parser::parse_forCond() {
#define EACH_LA_cmdOrExpr(OP) \
    OP(COMMAND) \
    EACH_LA_expression(OP)

    switch(this->curTokenKind) {
    EACH_LA_cmdOrExpr(GEN_LA_CASE) {
        return this->parse_commandOrExpression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
#undef EACH_LA_cmdOrExpr
}

INLINE std::unique_ptr<Node> Parser::parse_forIter() {
    switch(this->curTokenKind) {
    EACH_LA_expression(GEN_LA_CASE) {
        return this->parse_expression();
    }
    default:
        return std::unique_ptr<Node>(nullptr);
    }
}

INLINE std::unique_ptr<CatchNode> Parser::parse_catchStatement() {
    this->matchToken(CATCH);
    unsigned int n = LN();
    this->matchToken(LP);
    Token token(this->matchAndGetToken(APPLIED_NAME));
    std::unique_ptr<TypeToken> typeToken;
    if(!HAS_NL() && this->curTokenKind == COLON) {
        this->matchToken(COLON, false);
        typeToken = this->parse_typeName();
    }
    this->matchToken(RP);
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
INLINE std::unique_ptr<Node> Parser::parse_commandListExpression() {
    RET_NODE(new CmdContextNode(this->parse_orListCommand().release()));
}

INLINE std::unique_ptr<Node> Parser::parse_orListCommand() {
    std::unique_ptr<Node> node(this->parse_andListCommand());

    while(this->curTokenKind == OR_LIST) {
        this->matchToken(OR_LIST);
        std::unique_ptr<Node> rightNode(this->parse_andListCommand());
        node.reset(new CondOpNode(node.release(), rightNode.release(), false));
    }
    return node;
}

INLINE std::unique_ptr<Node> Parser::parse_andListCommand() {
    std::unique_ptr<Node> node(this->parse_pipedCommand());

    while(this->curTokenKind == AND_LIST) {
        this->matchToken(AND_LIST);
        std::unique_ptr<Node> rightNode(this->parse_pipedCommand());
        node.reset(new CondOpNode(node.release(), rightNode.release(), true));
    }
    return node;
}

INLINE std::unique_ptr<Node> Parser::parse_pipedCommand() {
    std::unique_ptr<PipedCmdNode> node(new PipedCmdNode(this->parse_command().release()));

    if(this->curTokenKind == PIPE) {
        while(this->curTokenKind == PIPE) {
            this->matchToken(PIPE);
            node->addCmdNodes(this->parse_command().release());
        }
    }
    return std::move(node);
}

INLINE std::unique_ptr<CmdNode> Parser::parse_command() {   //FIXME: redirect
    unsigned int n = LN();
    Token token(this->matchAndGetToken(COMMAND));
    std::unique_ptr<CmdNode> node(new CmdNode(n, this->lexer->toCmdArg(token, true)));

    while(this->curTokenKind == CMD_SEP) {
        this->matchToken(CMD_SEP);
        node->addArgNode(this->parse_cmdArg().release());
    }
    return node;
}

INLINE std::unique_ptr<CmdArgNode> Parser::parse_cmdArg() {
    std::unique_ptr<CmdArgNode> node(new CmdArgNode(this->parse_cmdArgSeg(true).release()));

    bool next = true;
    while(next) {
        switch(this->curTokenKind) {
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

INLINE std::unique_ptr<Node> Parser::parse_cmdArgSeg(bool expandTilde) {
    static TokenKind alters[] = {
            EACH_LA_cmdArg(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    case CMD_ARG_PART: {
        unsigned int n = LN();
        Token token(this->matchAndGetToken(CMD_ARG_PART));
        RET_NODE(new StringValueNode(n, this->lexer->toCmdArg(token, expandTilde)));
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

// expression
INLINE std::unique_ptr<Node> Parser::parse_commandOrExpression() {
#define EACH_LA_condExpr(OP) \
    OP(COMMAND) \
    EACH_LA_expression(OP)

    static TokenKind alters[] = {
            EACH_LA_condExpr(GEN_LA_ALTER)
            DUMMY
    };

#undef EACH_LA_condExpr

    switch(this->curTokenKind) {
    case COMMAND:
        return this->parse_commandListExpression();
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

INLINE std::unique_ptr<Node> Parser::parse_expression() {
    return this->parse_expression(
            this->parse_unaryExpression(), getPrecedence(ASSIGN));
}

std::unique_ptr<Node> Parser::parse_expression(std::unique_ptr<Node> &&leftNode,
                                               unsigned int basePrecedence) {
    std::unique_ptr<Node> node(std::move(leftNode));
    for(unsigned int p = PRECEDENCE();
        !HAS_NL() && p >= basePrecedence; p = PRECEDENCE()) {
        switch(this->curTokenKind) {
        case AS: {
            this->matchToken(AS, false);
            std::unique_ptr<TypeToken> type(this->parse_typeName());
            node.reset(new CastNode(node.release(), type.release()));
            break;
        }
        case IS: {
            this->matchToken(IS, false);
            std::unique_ptr<TypeToken> type(this->parse_typeName());
            node.reset(new InstanceOfNode(node.release(), type.release()));
            break;
        }
        default: {
            TokenKind op = this->consumeAndGetKind();
            std::unique_ptr<Node> rightNode(this->parse_unaryExpression());
            for(unsigned int nextP = PRECEDENCE(); nextP > p; nextP = PRECEDENCE()) {
                rightNode = this->parse_expression(std::move(rightNode), nextP);
            }
            node.reset(createBinaryOpNode(node.release(), op, rightNode.release()));
            break;
        }
        }
    }
    return node;
}

INLINE std::unique_ptr<Node> Parser::parse_unaryExpression() {
    switch(this->curTokenKind) {
    case PLUS:
    case MINUS:
    case NOT: {
        TokenKind op = this->consumeAndGetKind();
        RET_NODE(createUnaryOpNode(op,
                                   this->parse_unaryExpression().release()));
    }
    default: {
        return this->parse_suffixExpression();
    }
    }
}

INLINE std::unique_ptr<Node> Parser::parse_suffixExpression() {
    std::unique_ptr<Node> node(this->parse_memberExpression());

    switch(this->curTokenKind) {
    case INC:
    case DEC:
        RET_NODE(createSuffixNode(node.release(), this->consumeAndGetKind()));
    default:
        return node;
    }
}

INLINE std::unique_ptr<Node> Parser::parse_memberExpression() {
    std::unique_ptr<Node> node(this->parse_primaryExpression());

    bool next = true;
    while(!HAS_NL() && next) {
        switch(this->curTokenKind) {
        case ACCESSOR: {
            this->matchToken(ACCESSOR);
            this->hasNoNewLine();
            std::string name(this->lexer->toName(this->matchAndGetToken(IDENTIFIER)));
            node.reset(new AccessNode(node.release(), std::move(name)));
            break;
        }
        case LB: {
            this->matchToken(LB);
            std::unique_ptr<Node> indexNode(this->parse_expression());
            this->matchToken(RB);
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

INLINE std::unique_ptr<Node> Parser::parse_primaryExpression() {
    static TokenKind alters[] = {
            EACH_LA_primary(GEN_LA_ALTER)
            DUMMY
    };

    unsigned int n = LN();
    switch(this->curTokenKind) {
    case NEW: {
        this->matchToken(NEW, false);
        std::unique_ptr<TypeToken> type(this->parse_typeName());
        std::unique_ptr<ArgsNode> args(this->parse_arguments());
        RET_NODE(new NewNode(n, type.release(), args.release()));
    }
    case INT_LITERAL: {
        Token token(this->matchAndGetToken(INT_LITERAL));
        int value;
        CONVERT_TO_NUM(value, n, INT_LITERAL, token, this->lexer->toInt);
        RET_NODE(new IntValueNode(n, value));
    }
    case BYTE_LITERAL : {
        Token token(this->matchAndGetToken(BYTE_LITERAL));
        unsigned char value;
        CONVERT_TO_NUM(value, n, BYTE_LITERAL, token, this->lexer->toUint8);
        RET_NODE(IntValueNode::newByte(n, value));
    };
    case INT16_LITERAL : {
        Token token(this->matchAndGetToken(INT16_LITERAL));
        short value;
        CONVERT_TO_NUM(value, n, INT16_LITERAL, token, this->lexer->toInt16);
        RET_NODE(IntValueNode::newInt16(n, value));
    };
    case UINT16_LITERAL : {
        Token token(this->matchAndGetToken(UINT16_LITERAL));
        unsigned short value;
        CONVERT_TO_NUM(value, n, UINT16_LITERAL, token, this->lexer->toUint16);
        RET_NODE(IntValueNode::newUint16(n, value));
    };
    case INT32_LITERAL : {
        Token token(this->matchAndGetToken(INT32_LITERAL));
        int value;
        CONVERT_TO_NUM(value, n, INT32_LITERAL, token, this->lexer->toInt32);
        RET_NODE(IntValueNode::newInt32(n, value));
    };
    case UINT32_LITERAL : {
        Token token(this->matchAndGetToken(UINT32_LITERAL));
        unsigned int value;
        CONVERT_TO_NUM(value, n, UINT32_LITERAL, token, this->lexer->toUint32);
        RET_NODE(IntValueNode::newUint32(n, value));
    };
    case INT64_LITERAL: {
        Token token(this->matchAndGetToken(INT64_LITERAL));
        long value;
        CONVERT_TO_NUM(value, n, INT64_LITERAL, token, this->lexer->toInt64);
        RET_NODE(LongValueNode::newInt64(n, value));
    };
    case UINT64_LITERAL: {
        Token token(this->matchAndGetToken(UINT64_LITERAL));
        unsigned long value;
        CONVERT_TO_NUM(value, n, UINT64_LITERAL, token, this->lexer->toUint64);
        RET_NODE(LongValueNode::newUint64(n, value));
    };
    case FLOAT_LITERAL: {
        Token token(this->matchAndGetToken(FLOAT_LITERAL));
        double value;
        CONVERT_TO_NUM(value, n, FLOAT_LITERAL, token, this->lexer->toDouble);
        RET_NODE(new FloatValueNode(n, value));
    }
    case STRING_LITERAL: {
        return this->parse_stringLiteral();
    }
    case PATH_LITERAL: {
        Token token = this->matchAndGetToken(PATH_LITERAL);

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
        this->matchToken(LP);
        std::unique_ptr<Node> node(this->parse_expression());
        bool isTuple = false;
        if(this->curTokenKind == COMMA) {
            isTuple = true;
            this->matchToken(COMMA);
            std::unique_ptr<Node> rightNode(this->parse_expression());
            std::unique_ptr<TupleNode> tuple(
                    new TupleNode(n, node.release(), rightNode.release()));

            while(this->curTokenKind == COMMA) {
                this->matchToken(COMMA);
                tuple->addNode(this->parse_expression().release());
            }
            node.reset(tuple.release());
        }
        this->matchToken(RP);
        if(!isTuple) {
            return std::unique_ptr<Node>(new GroupNode(n, node.release()));
        }
        return node;
    }
    case LB: {
        this->matchToken(LB);
        std::unique_ptr<ArrayNode> node(
                new ArrayNode(n, this->parse_expression().release()));
        while(this->curTokenKind == COMMA) {
            this->matchToken(COMMA);
            node->addExprNode(this->parse_expression().release());
        }
        this->matchToken(RB);
        return std::move(node);
    }
    case LBC: {
        this->matchToken(LBC);
        std::unique_ptr<Node> keyNode(this->parse_expression());

        this->hasNoNewLine();
        this->matchToken(COLON);
        this->hasNoNewLine();

        std::unique_ptr<Node> valueNode(this->parse_expression());
        std::unique_ptr<MapNode> node(
                new MapNode(n, keyNode.release(), valueNode.release()));
        while(this->curTokenKind == COMMA) {
            this->matchToken(COMMA);
            keyNode = this->parse_expression();

            this->hasNoNewLine();
            this->matchToken(COLON);
            this->hasNoNewLine();

            valueNode = this->parse_expression();
            node->addEntry(keyNode.release(), valueNode.release());
        }
        this->matchToken(RBC);
        return std::move(node);
    }
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

INLINE std::unique_ptr<Node> Parser::parse_appliedName() {
    unsigned int n = LN();
    Token token(this->matchAndGetToken(APPLIED_NAME));
    RET_NODE(new VarNode(n, this->lexer->toName(token)));
}

INLINE std::unique_ptr<Node> Parser::parse_specialName() {
    unsigned int n = LN();
    Token token(this->matchAndGetToken(SPECIAL_NAME));
    RET_NODE(new VarNode(n, this->lexer->toName(token)));
}

INLINE std::unique_ptr<Node> Parser::parse_stringLiteral() {
    unsigned int n = LN();
    Token token(this->matchAndGetToken(STRING_LITERAL));
    RET_NODE(new StringValueNode(n, this->lexer->toString(token)));
}

INLINE std::unique_ptr<ArgsNode> Parser::parse_arguments() {
    this->matchToken(LP);

    std::unique_ptr<ArgsNode> node(new ArgsNode());
    switch(this->curTokenKind) {
    EACH_LA_expression(GEN_LA_CASE) {
        node->addArg(this->parse_expression().release());
        while(this->curTokenKind == COMMA) {
            this->matchToken(COMMA);
            node->addArg(this->parse_expression().release());
        }
        break;
    }
    default:  // no args
        break;
    }

    this->matchToken(RP);
    return node;
}

INLINE std::unique_ptr<Node> Parser::parse_stringExpression() {
    unsigned int n = LN();
    this->matchToken(OPEN_DQUOTE);
    std::unique_ptr<StringExprNode> node(new StringExprNode(n));

    bool next = true;
    while(next) {
        switch(this->curTokenKind) {
        case STR_ELEMENT: {
            unsigned int n = LN();
            Token token(this->matchAndGetToken(STR_ELEMENT));
            node->addExprNode(
                    new StringValueNode(n, this->lexer->toString(token, false)));
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

    this->matchToken(CLOSE_DQUOTE);
    return std::move(node);
}

INLINE std::unique_ptr<Node> Parser::parse_interpolation() {
    static TokenKind alters[] = {
            EACH_LA_interpolation(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    case APPLIED_NAME: {
        return this->parse_appliedName();
    }
    case SPECIAL_NAME: {
        return this->parse_specialName();
    }
    case START_INTERP: {
        this->matchToken(START_INTERP);
        std::unique_ptr<Node> node(this->parse_expression());
        this->matchToken(RBC);
        return node;
    }
    default: {
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
    }
}

INLINE std::unique_ptr<Node> Parser::parse_commandSubstitution() {
    this->matchToken(START_SUB_CMD);
    std::unique_ptr<Node> node(this->parse_commandListExpression());
    node->inCmdArgNode();
    this->matchToken(RP);
    return node;
}

bool parse(const char *sourceName, RootNode &rootNode) {
    FILE *fp = fopen(sourceName, "r");
    if(fp == NULL) {
        return false;
    }

    Lexer<LexerDef, TokenKind> lexer(fp);
    Parser parser;

    try {
        parser.parse(lexer, rootNode);
    } catch(const ParseError &e) {
        return false;   //FIXME: display error message.
    }
    return true;
}

} // namespace parser
} // namespace ydsh