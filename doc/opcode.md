## Specification of op code

| **Mnemonic**    | **Other bytes**                | **Stack (before -> after)**                  | **Description**                                        |
|-----------------|--------------------------------|----------------------------------------------|--------------------------------------------------------|
| HALT            |                                | [no change]                                  | stop evaluation of interpreter immediately             |
| ASSERT_ENABLED  | 2: offset1 offset2             | [no change]                                  | check if assertion enabled                             |
| ASSERT_FAIL     |                                | value ->                                     | throw AssertionError                                   |
| PRINT           | 3: byte1 ~ byte3               | value ->                                     | print specified type and value on top of the stack     |
| INSTANCE_OF     | 3: byte1 ~ byte3               | value -> value                               | check if a value is instance of a specified type       |
| CHECK_CAST      | 3: byte1 ~ byte3               | value -> value                               | check if a value is instance of a specified type       |
| PUSH_NULL       |                                | -> value                                     | push the null value onto the stack                     |
| PUSH_TRUE       |                                | -> value                                     | push the true value onto the stack                     |
| PUSH_FALSE      |                                | -> value                                     | push the false value onto the stack                    |
| PUSH_SIG        | 1: byte1                       | -> value                                     | push signal literal onto the stack                     |
| PUSH_INT        | 1: byte1                       | -> value                                     | push 8bit int value onto the stack                     |
| PUSH_STR0       |                                | -> value                                     | push the empty string value onto the stack             |
| PUSH_STR1       | 1: byte1                       | -> value                                     | push the string value onto the stack                   |
| PUSH_STR2       | 2: byte1 byte2                 | -> value                                     | push the string value onto the stack                   |
| PUSH_STR3       | 3: byte1 byte2 byte3           | -> value                                     | push the string value onto the stack                   |
| PUSH_META       | 1: byte1                       | -> value                                     | push glob meta character onto the stack                |
| PUSH_TYPE       | 3: byte1 byte2 byte3           | -> type                                      | push the type onto the stack                           |
| PUSH_INVALID    |                                | -> value                                     | push the invalid onto the stack                        |
| LOAD_CONST      | 1: byte1                       | -> value                                     | load a constant from the constant pool                 |
| LOAD_CONST_W    | 2: byte1 byte2                 | -> value                                     | load a constant from the constant pool                 |
| LOAD_CONST_T    | 3: byte1 byte2 byte3           | -> value                                     | load a constant from the constant pool                 |
| LOAD_GLOBAL     | 2: byte1 byte2                 | -> value                                     | load a value from a global variable                    |
| STORE_GLOBAL    | 2: byte1 byte2                 | value ->                                     | store a value to a global variable                     |
| LOAD_LOCAL      | 1: byte1                       | -> value                                     | load a value from a local variable                     |
| STORE_LOCAL     | 1: byte1                       | value ->                                     | store a value to a local variable                      |
| LOAD_FIELD      | 2: byte1 byte2                 | value -> value                               | load a value from a instance field                     |
| STORE_FIELD     | 2: byte1 byte2                 | value1 value2 ->                             | store a value into a instance field                    |
| IMPORT_ENV      | 1: byte1                       | value1 [value2] ->                           | import environmental variable                          |
| LOAD_ENV        |                                | value -> value                               | get environmental variable                             |
| STORE_ENV       |                                | value1 value2 ->                             | set environmental variable                             |
| NEW_ENV_CTX     |                                | -> value                                     | push new EnvCtxObject onto the stack                   |
| ADD2ENV_CTX     |                                | ctx name value -> ctx                        | set and save env                                       |
| POP             |                                | value ->                                     | pop stack top value                                    |
| DUP             |                                | value -> value value                         | duplicate top value                                    |
| DUP2            |                                | value1 value2 -> value1 value2 value1 value2 | duplicate top two value                                |
| SWAP            |                                | value1 value2 -> value2 value1               | swap top two value                                     |
| CONCAT          |                                | value1 value2 -> value3                      | concat string value1 and string value2                 |
| APPEND          |                                | value1 value2 -> value1                      | append string value2 with string value1                |
| APPEND_ARRAY    |                                | value1 value2 -> value1                      | append value2 into value1                              |
| APPEND_MAP      |                                | value1 value2 value3 -> value1               | append value2 and value3 into value1                   |
| NEW             | 3: byte1 ~ byte3               | -> value                                     | create an empty object of a specified type             |
| INIT_FIELDS     | 2: offset size                 | value -> value                               | init object fields                                     |
| CALL_METHOD     | 3: param index1 index2         | recv param1 ~ paramN -> result               | call virtual method                                    |
| CALL_FUNC       | 1: param                       | func param1 ~ paramN -> result               | apply function object                                  |
| CALL_BUILTIN    | 1: index                       | -> value                                     | call builtin function                                  |
| CALL_BUILTIN2   | 2: param index                 | param1 ~ paramN -> result                    | call builtin function                                  |
| RETURN          |                                | value -> [empty]                             | return value from callable                             |
| RETURN_UDC      |                                | value -> [empty]                             | return from user-defined command                       |
| RETURN_SIG      |                                | [no change]                                  | return from signal handler                             |
| BRANCH          | 2: offset1 offset2             | value ->                                     | if value is false, branch to instruction at offset     |
| BRANCH_NOT      | 2: offset1 offset2             | value ->                                     | if value is not false, branch to instruction at offset | 
| GOTO            | 4: byte1 ~ byte4               | [no change]                                  | go to instruction at a specified index                 |
| GOTO_UNWIND     | 4: byte1 ~ byte4               | [unwind to guard]                            | unwind stack top to guard before go to instruction     |
| GOTO_UNWIND_V   | 4: byte1 ~ byte4               | [no change]                                  | save and restore stack top during unwinding            |
| STACK_GUARD     |                                | -> value                                     | push guard value onto the stack                        |
| THROW           |                                | value -> [empty]                             | throw exception                                        |
| ENTER_FINALLY   | 2: offset1 offset2             | -> value                                     | save current pc and go to instruction                  |
| EXIT_FINALLY    |                                | value ->                                     | pop stack top and go to instruction                    |
| LOOKUP_HASH     |                                | hashmap key ->                               | jump to the offset from stack top hashmap              |
| REF_EQ          |                                | value1 value2 -> value                       | check referencial equality                             |
| REF_NE          |                                | value1 value2 -> value                       | check referencial un-equality                          |
| FORK            | 1: byte1 2: offset1 offset2    | -> value                                     | evaluate code in child shell                           |
| PIPELINE        | 1: len 2: offset1 offset2 ...  | -> value                                     | call pipeline                                          |
| PIPELINE_LP     | 1: len 2: offset1 offset2 ...  | -> value                                     | call pipeline (lastPipe is true)                       |
| PIPELINE_ASYNC  | 1: k 1: len 2: offset1 offset2 | -> value                                     | call pipeline asynchronously                           | 
| EXPAND_TILDE    |                                | value -> value                               | perform tilde expansion                                |
| NEW_CMD         |                                | value -> value                               | pop stack top and store it to new argv                 |
| ADD_CMD_ARG     | 1: byte1                       | argv redir value -> argv redir               | add stack top value as command argument                |
| ADD_GLOBBING    | 1: len 2: option               | argv redir value1 ~ valueN+1 -> argv redir   | apply glob expansion and add results to value0         |
| CALL_CMD        |                                | argv redir -> value                          | call builtin or external command.                      |
| CALL_CMD_NOFORK |                                | argv redir -> value                          | call builtin or external command without fork          |
| CALL_UDC        | 2: byte1 byte2                 | argv redir -> value                          | call user-defined command                              |
| CALL_UDC_NOFORK | 2: byte1 byte2                 | argv redir -> value                          | call user-defined command without fork                 |
| CALL_CMD_COMMON |                                | argv redir -> value                          | call command (user-defined, builtin, external)         |
| BUILTIN_CMD     |                                | -> value                                     | call builtin command command                           |
| BUILTIN_EVAL    |                                | -> value                                     | call builtin eval command                              |
| BUILTIN_EXEC    |                                | -> value / [terminate]                       | call builtin exec command                              |
| NEW_REDIR       |                                | -> value                                     | create new RedireConfig                                |
| ADD_REDIR_OP    | 1: byte1                       | value1 value2 -> value1                      | add stack top value as redirection target              |
| DO_REDIR        |                                | value -> value                               | perform redirection                                    |
| LOAD_CUR_MOD    |                                | -> value                                     | load a current module                                  |
| RAND            |                                | -> value                                     | generate random number and push stack top              |
| GET_SECOND      |                                | -> value                                     | get differential time between current and base         |
| SET_SECOND      |                                | value ->                                     | set base time                                          |
| UNWRAP          |                                | value -> value                               | unwrap option value                                    |
| CHECK_UNWRAP    |                                | value -> value                               | check if option value has a value                      |
| TRY_UNWRAP      | 2: offset1 offset2             | value -> / [no change]                       | try to unwrap option value                             |
| RECLAIM_LOCAL   | 2: offset1 size1               | [no change]                                  | reclaim local variables specified range                |
