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
#include <util/debug.h>

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
    OP(LINE_END)

#define EACH_LA_expression(OP) \
    OP(NEW) \
    OP(NOT) \
    OP(PLUS) \
    OP(MINUS) \
    OP(INT_LITERAL) \
    OP(FLOAT_LITERAL) \
    OP(STRING_LITERAL) \
    OP(OPEN_DQUOTE) \
    OP(START_SUB_CMD) \
    OP(APPLIED_NAME) \
    OP(SPECIAL_NAME) \
    OP(LP) \
    OP(LB) \
    OP(LBC)

#define EACH_LA_toplevelStatement(OP) \
    OP(CLASS) \
    OP(FUNCTION) \
    EACH_LA_statement(OP) \
    EACH_LA_expression(OP)

#define EACH_LA_typeName(OP) \
    OP(EXTENDS) \
    OP(NEW) \
    OP(NOT) \
    OP(AS) \
    OP(IN) \
    OP(IS) \
    OP(IDENTIFIER)

#define EACH_LA_name(OP) \
    OP(FUNC) \
    EACH_LA_typeName(OP)

#define EACH_LA_varDecl(OP) \
    OP(VAR) \
    OP(LET)


#define E_ALTER(alt) \
    do { throw NoViableAlterError(LN(), this->curTokenKind, alt); } while(0)

#define RET_NODE(node) return std::unique_ptr<Node>(node)


// ####################
// ##     Parser     ##
// ####################

Parser::Parser(Lexer *lexer) :
        lexer(lexer), curTokenKind(), curToken() {
}

Parser::~Parser() {
}

void Parser::setLexer(Lexer *lexer) {
    this->lexer = lexer;
}

RootNode *Parser::parse() {
    // first, fetch token.
    NEXT_TOKEN();

    int a = 12;
    switch(a) {
    case 0: case 1: case 2:
        break;
    }
    // start parsing
    std::unique_ptr<RootNode> rootNode = this->parse_toplevel();
    return rootNode.release();
}

inline void Parser::matchToken(TokenKind expected) {
    if(this->curTokenKind != expected) {
        throw TokenMismatchError(LN(), this->curTokenKind, expected);
    }
    NEXT_TOKEN();
}

inline Token Parser::matchAndGetToken(TokenKind expected) {
    if(this->curTokenKind != expected) {
        throw TokenMismatchError(LN(), this->curTokenKind, expected);
    }
    Token token = this->curToken;
    NEXT_TOKEN();
    return token;
}

inline void Parser::hasNoNewLine() {
    if(!HAS_NL()) {
        throw UnexpectedNewLineError(LN());
    }
}

// parse rule definition

std::unique_ptr<RootNode> Parser::parse_toplevel() {
    std::unique_ptr<RootNode> rootNode(new RootNode());

    bool next = true;
    while(next) {
        switch(this->curTokenKind) {
        EACH_LA_toplevelStatement(GEN_LA_CASE) {
            // parse
            std::unique_ptr<Node> node = this->parse_toplevelStatement();

            rootNode->addNode(node.release());
            break;
        }
        default: {
            next = false;
            break;
        }
        }
    }

    this->matchToken(EOS);
    return rootNode;
}

std::unique_ptr<Node> Parser::parse_toplevelStatement() {
    static TokenKind alters[] = {
            EACH_LA_toplevelStatement(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    case CLASS:
    case FUNCTION:
        fatal("not implemented rule: %s\n", getTokenName(this->curTokenKind)); //FIXME:
        break;
    EACH_LA_statement(GEN_LA_CASE)
        break; //FIXME:
    default:
        E_ALTER(alters);
    }
    return std::unique_ptr<Node>(nullptr);    //FIXME:
}

std::unique_ptr<Node> Parser::parse_statement() {
    switch(this->curTokenKind) {
    case LINE_END: {
        this->matchToken(LINE_END);
        RET_NODE(new EmptyNode());
    }
    case ASSERT: {
        this->matchToken(ASSERT);
        RET_NODE(new AssertNode(LN(), this->parse_expression().release()));
    }
    case BREAK: {
        this->matchToken(BREAK);
        RET_NODE(new BreakNode(LN()));
    }
    case CONTINUE: {
        this->matchToken(BREAK);
        RET_NODE(new ContinueNode(LN()));
    }
    case EXPORT_ENV: {
        this->matchToken(EXPORT_ENV);
        unsigned int n = LN();
        std::string name = this->parse_name();
        this->hasNoNewLine();
        this->matchToken(ASSIGN);
        RET_NODE(new ExportEnvNode(n, std::move(name),
                this->parse_expression().release()));
    }
    case FOR:
        break; //FIXME:
    case IF:
        break; //FIXME:
    case IMPORT_ENV: {
        this->matchToken(IMPORT_ENV);
        RET_NODE(new ImportEnvNode(LN(), this->parse_name()));
    }
    case RETURN: {
        this->matchToken(RETURN);
        unsigned int n = LN();
        if(!HAS_NL()) {
            RET_NODE(new ReturnNode(n));
        } else {
            RET_NODE(new ReturnNode(n, this->parse_expression().release()));
        }
    }
    case THROW: {
        this->matchToken(THROW);
        RET_NODE(new ThrowNode(LN(), this->parse_expression().release()));
    }
    case WHILE:
        break; // FIXME:
    case DO:
        break; // FIXME:
    case TRY:
        break; // FIXME:
    case VAR:
    case LET:
        return this->parse_variableDeclaration();
    case COMMAND:
        break; // FIXME:
    default:
        return this->parse_expression();
    }
}

std::unique_ptr<Node> Parser::parse_expression() {
    return std::unique_ptr<Node>(nullptr);    //FIXME:
}

inline std::string Parser::parse_name() {
    static TokenKind alters[] = {
            EACH_LA_name(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    EACH_LA_name(GEN_LA_CASE) {
        Token token = this->curToken;
        NEXT_TOKEN();
        return this->lexer->toName(token);
    }
    default:
        E_ALTER(alters);
    }
    return "";
}

std::unique_ptr<Node> Parser::parse_variableDeclaration() {
    static TokenKind alters[] = {
            EACH_LA_varDecl(GEN_LA_ALTER)
            DUMMY
    };

    switch(this->curTokenKind) {
    EACH_LA_varDecl(GEN_LA_CASE) {
        bool readOnly = this->curTokenKind != VAR;
        NEXT_TOKEN();
        unsigned int n = LN();
        std::string name = this->parse_name();
        this->hasNoNewLine();
        this->matchToken(ASSIGN);
        RET_NODE(new VarDeclNode(n, std::move(name),
                this->parse_rightHandleSide().release(), readOnly));
    }
    default:
        E_ALTER(alters);
        return std::unique_ptr<Node>(nullptr);
    }
}

inline std::unique_ptr<Node> Parser::parse_rightHandleSide() {
#define EACH_LA_RHS(OP) \
    OP(COMMAND) \
    EACH_LA_expression(OP)

    static TokenKind alters[] = {
            EACH_LA_RHS(GEN_LA_ALTER)
            DUMMY
    };

#undef EACH_LA_RHS

    switch(this->curTokenKind) {
    case COMMAND:
        break; //FIXME:
    EACH_LA_expression(GEN_LA_CASE)
        return this->parse_expression();
    default:
        E_ALTER(alters);
    }
    return std::unique_ptr<Node>(nullptr);
}
