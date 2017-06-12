# Grammar specification of ydsh
## Lexer specification



## Parser specification

```
toplevel = statement* EOF
         ;

function = funcDecl block statementEnd
         ;

funcDecl = FUNCTION IDENTIFIER LP 
           (APPLIED_NAME COLON typeName (COMMA APPLIED_NAME COLON typeName)*)? 
           RP (COLON typeName (COMMA typeName)*)?
         ;

interface = INTERFACE TYPE_PATH LBC 
            ( VAR IDENTIFIER COLON typeName statementEnd
            | LET IDENTIFIER COLON typeName statementEnd
            | funcDecl statementEnd
            )+ RBC statementEnd
          ;

typeAlias = TYPE_ALIAS IDENTIFIER typeName statementEnd
          ;

basicOrReifiedType = (IDENTIFIER | TYPEOF) 
                     ({!HAS_NL} TYPE_OPEN typeName (TYPE_SEP typeName)* TYPE_CLOSE)?     
                   ;

typeNameImpl = basicOrReifiedType
             | PTYPE_OPEN typeName (TYPE_SEP typeName)* PTYPE_CLOSE
             | ATYPE_OPEN typeName (TYPE_MSEP typeName)? ATYPE_CLOSE
             | TYPEOF PTYPE_OPEN expression RP
             | FUNC ({!HAS_NL} TYPE_OPEN typeName 
                (TYPE_SEP ATYPE_OPEN typeName (TYPE_SEP typeName)* ATYPE_CLOSE)? 
                TYPE_CLOSE)?   
             | TYPE_PATH
             ;

typeName = typeNameImpl ({!HAS_NL} TYPE_OPT)?
         ;

statement = LINE_END
          | function
          | interface
          | typeAlias
          | ASSERT expression ({!HAS_NL} COLON expression)? statementEnd
          | block statementEnd
          | BREAK statementEnd
          | CONTINUE statementEnd
          | EXPORT_ENV IDENTIFIER ASSIGN epxression statementEnd
          | forStatement
          | ifStatement statementEnd
          | IMPORT_ENV IDENTIFIER ({!HAS_NL} COLON expression)? statementEnd 
          | RETURN ({!HAS_NL} expression)? statementEnd
          | WHILE expression block statementEnd
          | DO block WHILE expression statementEnd
          | TRY block catchStatement* (FINALLY block)? statementEnd
          | variableDeclaration statementEnd
          | assignmentExpression statementEnd
          ;

statementEnd = EOS | RBC | LINE_END | {!HAS_NL}
             ;

block = LBC statement* RBC
      ;

variableDeclaration = (VAR | LET) IDENTIFIER ASSIGN expression
                    ;

ifStatement = IF expression block (ELIF expression block)* (ELSE block)?
            ;

forStatement = FOR LP forInit LINE_END forCond LINE_END forIter RP block statementEnd
             | FOR APPLIED_NAME IN expression block statementEnd
             ;

forInit = (variableDeclaration | assignmentExpression)?
        ;

forCond = expression?
        ;

forIter = assignmentExpression?
        ;

catchStatement = CATCH LP APPLIED_NAME (COLON typeName)? RP block
               | CATCH APPLIED_NAME (COLON typeName)? block
               ;

pipedCommand = command (PIPE command)*
             | COMMAND LP RP blocl
             ;

command = COMMAND ({HAS_SPACE} (cmdArg | redirOption))*
        ;

redirOption = (REDIR_MERGE_ERR_2_OUT | REDIR_MERGE_OUT_2_ERR)
            | (REDIR_IN_2_FILE 
               | REDIR_OUT_2_FILE 
               | REDIR_OUT_2_FILE_APPEND
               | REDIR_ERR_2_FILE
               | REDIR_ERR_2_FILE_APPEND
               | REDIR_MERGE_ERR_2_OUT_2_FILE
               | REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND
               | REDIR_HERE_STR) cmdArg
            ;

cmdArg = cmdArgSeg ({!HAS_SPACE} cmdArgSeg)*
       ;

cmdArgSeg = CMD_ARG_PART
          | stringLiteral
          | stringExpression
          | substitution
          | paramExpansion
          ;

assignmentExpression = 













```

