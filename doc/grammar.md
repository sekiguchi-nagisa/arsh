# Grammar specification of arsh
## Lexer specification
```
NUM = "0" | [1-9] [0-9]*
OCTAL = "0o" [0-7]+
HEX = "0x" [0-9a-fA-F]+
INTEGER = NUM | OCTAL | HEX
INTEGER_ = NUM | OCTAL "_" | HEX "_"
DIGITS = [0-9]+
FLOAT_SUFFIX =  [eE] [+-]? NUM
FLOAT = NUM "." DIGITS FLOAT_SUFFIX?

SQUOTE_CHAR = "\\" ['] | [^'\000]
DQUOTE_CHAR = "\\" [^\000] | [^$\\"\000]
VAR_NAME = [_a-zA-Z] [_0-9a-zA-Z]* 
SPECIAL_NAMES = [@#?$0-9]

STRING_LITERAL = ['] [^'\000]* [']
ESTRING_LITERAL = "$" ['] SQUOTE_CHAR* [']
APPLIED_NAME = "$" VAR_NAME
SPECIAL_NAME = "$" SPECIAL_NAMES

INNER_NAME = APPLIED_NAME | '${' VAR_NAME '}'
INNER_SPECIAL_NAME = SPECIAL_NAME | '${' SPECIAL_NAMES '}'

CMD_START_CHAR     = "\\" [^\r\n\000] | [^ \t\r\n\\;'"`|&<>(){}$#[\]!+\-0-9\000]
CMD_CHAR           = "\\" [^\000]     | [^ \t\r\n\\;'"`|&<>(){}$\000]
CMD_ARG_START_CHAR = "\\" [^\r\n\000] | [^ \t\r\n\\;'"`|&<>()$#\000]
CMD_ARG_CHAR       = "\\" [^\000]     | [^ \t\r\n\\;'"`|&<>()$\000]

REGEX_CHAR = "\\/" | [^\r\n/]
REGEX = "$/" REGEX_CHAR* "/" [im]{0,2}

LINE_END = ";"
NEW_LINE = [\r\n][ \t\r\n]*
COMMENT = "#" [^\r\n\000]*


ALIAS = "alias"
ASSERT = "assert"
BREAK = "break"
CASE = "case"
CATCH = "catch"
CLASS = "class"
CONTINUE = "continue"
COPROC = "coproc"
DO = "do"
ELIF = "elif"
ELSE = "else"
EXPORT_ENV = "export-env"
FINALLY = "finally"
FOR = "for"
FUNCTION = "function"
IF = "if"
IMPORT_ENV = "import-env"
INTERFACE = "interface"
LET = "let"
NEW = "new"
RETURN = "return"
TRY = "try"
THROW = "throw"
VAR = "var"
WHILE = "while"

PLUS = "+"
MINUS = "-"
NOT = "!"

INT32_LITERAL  = INTEGER
               | INTEGER_ "i32"
UINT32_LITERAL = INTEGER_ "u" 
BYTE_LITERAL   = INTEGER_ "b"
INT16_LITERAL  = INTEGER_ "i16"
INT64_LITERAL  = INTEGER_ "i64" 
UINT16_LITERAL = INTEGER_ "u16"
UINT32_LITERAL = INTEGER_ "u32" 
UINT64_LITERAL = INTEGER_ "u64"
FLOAT_LITERAL  = FLOAT
SIGNAL_LITERAL = "%" ['] VAR_NAME [']

OPEN_DQUOTE = ["]
START_SUB_CMD = "$("
START_IN_SUB  = ">("
START_OUT_SUB = "<("

LP = "("
RP = ")"
LB = "["
RB = "]"
LBC = "{"
RBC = "}"

COMMAND = CMD_START_CHAR CMD_CHAR*

COLON = ":"
COMMA = ","

ADD = "+"
SUB = "-"
MUL = "*"
DIV = "/"
MOD = "%"
LT = "<"
GT = ">"
LE = "<="
GE = ">="
EQ = "=="
NE = "!="
AND = "and"
OR = "or"
XOR = "xor"
COND_AND = "&&"
COND_OR = "||"
MATCH = "=~"
UNMATCH = "!~"
TERNARY = "?
NULL_COALE = "??"
PIPE = "|"

INC = "++"
DEC = "--"
UNWRAP = "!"

ASSIGN = "="
ADD_ASSIGN = "+="
SUB_ASSIGN = "-="
MUL_ASSIGN = "*="
DIV_ASSIGN = "/="
MOD_ASSIGN = "%="
CASE_ARM = "=>"

AS = "as"
IS = "is"
IN = "in"
WITH = "with"
BACKGROUND = "&"
DISOWN_BG = ("&!" | "&|") 

IDENTIFIER = VAR_NAME
ACCESSOR = "."

CLOSE_DQUOTE = ["]
STR_ELEMENT = DQUOTE_CHAR+ 
START_INTERP = "${"

CMD_ARG_PART = CMD_ARG_START_CHAR CMD_ARG_CHAR*
APPLIED_NAME_WITH_BRACKET = APPLIED_NAME "["
SPECIAL_NAME_WITH_BRACKET = SPECIAL_NAME "[" 

REDIR_IN_2_FILE = "<"
REDIR_OUT_2_FILE = (">" | "1>") 
REDIR_OUT_2_FILE_APPEND = ("1>>" | ">>")
REDIR_ERR_2_FILE = "2>" 
REDIR_ERR_2_FILE_APPEND = "2>>"
REDIR_MERGE_ERR_2_OUT_2_FILE = (">&" | "&>")
REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND = "&>>"
REDIR_MERGE_ERR_2_OUT = "2>&1"
REDIR_MERGE_OUT_2_ERR = "1>&2"
REDIR_HERE_STR = "<<<"

FUNC = "Func"
TYPEOF = "typeof"
TYPE_OPEN = "<"
TYPE_CLOSE = ">"
TYPE_SEP = ","
ATYPE_OPEN = "["
ATYPE_CLOSE = "]"
PTYPE_OPEN = "("
PTYPE_CLOSE = ")"
TYPE_MSEP = ":"
TYPE_OPT = "!"


```


## Parser specification

```
function = funcDecl block

funcDecl = FUNCTION IDENTIFIER LP 
           (APPLIED_NAME COLON typeName (COMMA APPLIED_NAME COLON typeName)*)? 
           RP (COLON typeName (COMMA typeName)*)?

interface = INTERFACE TYPE_PATH LBC 
            ( VAR IDENTIFIER COLON typeName statementEnd
            | LET IDENTIFIER COLON typeName statementEnd
            | funcDecl statementEnd
            )+ RBC

typeAlias = ALIAS IDENTIFIER typeName

basicOrReifiedType = (IDENTIFIER | TYPEOF) 
                     ({!HAS_NL} TYPE_OPEN typeName (TYPE_SEP typeName)* TYPE_CLOSE)?     

typeNameImpl = basicOrReifiedType
             | PTYPE_OPEN typeName (TYPE_SEP typeName)* PTYPE_CLOSE
             | ATYPE_OPEN typeName (TYPE_MSEP typeName)? ATYPE_CLOSE
             | TYPEOF PTYPE_OPEN expression RP
             | FUNC ({!HAS_NL} TYPE_OPEN typeName 
                (TYPE_SEP ATYPE_OPEN typeName (TYPE_SEP typeName)* ATYPE_CLOSE)? 
                TYPE_CLOSE)?   
             | TYPE_PATH

typeName = typeNameImpl ({!HAS_NL} TYPE_OPT)?

statementImpl = LINE_END
              | function
              | interface
              | typeAlias
              | ASSERT expression ({!HAS_NL} COLON expression)?
              | BREAK ({!HAS_NL} expression)?
              | CONTINUE
              | EXPORT_ENV IDENTIFIER ASSIGN epxression
              | IMPORT_ENV IDENTIFIER ({!HAS_NL} COLON expression)?
              | RETURN ({!HAS_NL} expression)?
              | variableDeclaration
              | expression

statement = statementImpl statementEnd

statementEnd = EOS | RBC | LINE_END | {!HAS_NL}     # FIXME:

block = LBC statement* RBC

variableDeclaration = (VAR | LET) IDENTIFIER ASSIGN expression

ifExpression = IF expression block (ELIF expression block)* (ELSE block)?

forExpression = FOR LP forInit LINE_END forCond LINE_END forIter RP block
              | FOR APPLIED_NAME IN expression block

forInit = (variableDeclaration | expression)?

forCond = expression?

forIter = expression?

catchStatement = CATCH LP APPLIED_NAME (COLON typeName)? RP block
               | CATCH APPLIED_NAME (COLON typeName)? block

command = COMMAND ({HAS_SPACE} (cmdArg | redirOption))*
        | COMMAND LR RP block

redirOption = (REDIR_MERGE_ERR_2_OUT | REDIR_MERGE_OUT_2_ERR)
            | (REDIR_IN_2_FILE 
               | REDIR_OUT_2_FILE 
               | REDIR_OUT_2_FILE_APPEND
               | REDIR_ERR_2_FILE
               | REDIR_ERR_2_FILE_APPEND
               | REDIR_MERGE_ERR_2_OUT_2_FILE
               | REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND
               | REDIR_HERE_STR) {HAS_SPACE} cmdArg

cmdArg = cmdArgSeg ({!HAS_SPACE} cmdArgSeg)*

cmdArgSeg = CMD_ARG_PART
          | stringLiteral
          | stringExpression
          | substitution
          | paramExpansion
          | procSubstitution

expression = THROW expression
           | binaryExpression

binaryExpression = unaryExpression {!HAS_NL} TERNARY expression COLON expression
                 | unaryExpression {!HAS_NL}
                   (ASSIGN | ADD_ASSIGN | SUB_ASSIGN | MUL_ASSIGN | DIV_ASSIGN | MOD_ASSIGN) expression
                 | condOrExpression

condOrExpression = condAndExpression ({!HAS_NL} COND_OR condAndExpression)*

condAndExpression = pipedExpression ({!HAS_NL} COND_AND pipedExpression)*

pipedExpression = withExpression ({!HAS_NL} PIPE withExpression)*

withExpression = equalityExpression ({!HAS_NL} WITH ({HAS_SPACE} redirOption)*)?

equalityExpression = nullCoalescingExpression
                     ({!HAS_NL} (EQ | NE | LT | GT | LE | GE | MATCH | UNMATCH) nullCoalescingExpression)*

nullCoalescingExpression = orExpression ({!HAS_NL} NULL_COALE orExpression)*

orExpression = xorExpression ({!HAS_NL} OR xorExpression)*

xorExpression = andExpression ({!HAS_NL} XOR andExpression)*

andExpression = addExpression ({!HAS_NL} AND addExpression)*

addExpression = mulExpression ({!HAS_NL} (ADD | SUB) mulExpression)*

mulExpression = typeExpression ({!HAS_NL} (MUL | DIV | MOD) typeExpression)*

typeExpression = unaryExpression ({!HAS_NL} (AS | IS) typeName)*

unaryExpression = (PLUS | MINUS | NOT) unaryExpression
                | suffixExpression

suffixExpression = suffixExpression {!HAS_NL} ACCESSOR IDENTIFIER
                 | suffixExpression {!HAS_NL} ACCESSOR IDENTIFIER {!HAS_NL} arguments
                 | suffixExpression {!HAS_NL} LB expression RB
                 | suffixExpression {!HAS_NL} arguments
                 | suffixExpression {!HAS_NL} (INC | DEC | UNWRAP)
                 | primaryExpression

primaryExpression = command
                  | NEW typeName arguments
                  | BYTE_LITERAL
                  | INT16_LITERAL
                  | UINT16_LITERAL
                  | INT32_LITERAL
                  | UINT32_LITERAL
                  | INT64_LITERAL
                  | UINT64_LITERAL
                  | FLOAT_LITERAL
                  | STRING_LITERAL
                  | PATH_LITERAL
                  | REGEX_LITERAL
                  | stringLiteral
                  | stringExpression
                  | substitution
                  | procSubstitution
                  | appliedName
                  | LP expression RP
                  | LP expression COMMA RP
                  | LP expression (COMMA expression)* RP
                  | LB expression (COMMA expression)* COMMA? RB
                  | LB expression COLON expression (COMMA expression COLON expression)* COMMA? RB
                  | block
                  | forExpression
                  | ifExpression
                  | WHILE expression block
                  | DO block expression WHILE
                  | TRY block (catchStatment)* (FINALLY block)?

appliedName = SPECIAL_NAME | APPLIED_NAME

stringLiteral = STRING_LITERAL

arguments = LP RP
          | LP expression (COMMA expression)* RP

stringExpression = OPEN_DQUOTE (STR_ELEMENT | interpolation | substitution)* CLOSE_DQUOTE

interpolation = APPLIED_NAME
              | SPECIAL_NAME
              | START_INTERP expression RP

paramExpansion = (APPLIED_NAME_WITH_BRACKET | SPECIAL_NAME_WITH_BRACKET) expression RB
               | interpolation

substitution = START_SUB_CMD expression RP

procSubstitution = (START_IN_SUB | START_OUT_SUB) expression RP

```

