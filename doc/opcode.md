## Specification of op code

| **Mnemonic**      | **Other bytes**                | **Stack (before -> after)**                  | **Description**                                                 |
|-------------------|--------------------------------|----------------------------------------------|-----------------------------------------------------------------|
| SUBSHELL_EXIT     |                                | [terminate]                                  | terminate subshell                                              |
| TERM_HOOK         |                                |                                              | call termination handler                                        |
| ASSERT_ENABLED    | 2: offset1 offset2             | [no change]                                  | check if assertion enabled                                      |
| ASSERT_FAIL       |                                | value ->                                     | throw AssertionError                                            |
| ASSERT_FAIL2      | 1: op                          | left right value ->                          | throw AssertionError with LSH, RHS                              |
| PRINT             | 3: byte1 ~ byte3               | value ->                                     | print specified type and value on top of the stack              |
| INSTANCE_OF       |                                | value type -> value                          | check if a value is instance of a specified type                |
| CHECK_CAST        | 3: byte1 ~ byte3               | value -> value                               | check if a value is instance of a specified type                |
| CHECK_CAST_OPT    | 3: byte1 ~ byte3               | value -> value                               | check if a value is instance of a specified type                |
| PUSH_TYPE         | 3: byte1 ~ byte3               | -> type                                      | push type (dummy object) onto the stack                         |
| PUSH_NULL         |                                | -> value                                     | push the null value onto the stack                              |
| PUSH_TRUE         |                                | -> value                                     | push the true value onto the stack                              |
| PUSH_FALSE        |                                | -> value                                     | push the false value onto the stack                             |
| PUSH_SIG          | 1: byte1                       | -> value                                     | push signal literal onto the stack                              |
| PUSH_INT          | 1: byte1                       | -> value                                     | push 8bit int value onto the stack                              |
| PUSH_STR0         |                                | -> value                                     | push the empty string value onto the stack                      |
| PUSH_STR1         | 1: byte1                       | -> value                                     | push the string value onto the stack                            |
| PUSH_STR2         | 2: byte1 byte2                 | -> value                                     | push the string value onto the stack                            |
| PUSH_STR3         | 3: byte1 byte2 byte3           | -> value                                     | push the string value onto the stack                            |
| PUSH_META         | 2: meta option                 | -> value                                     | push expansion meta character onto the stack                    |
| PUSH_INVALID      |                                | -> value                                     | push the invalid onto the stack                                 |
| LOAD_CONST        | 1: byte1                       | -> value                                     | load a constant from the constant pool                          |
| LOAD_CONST2       | 2: byte1 byte2                 | -> value                                     | load a constant from the constant pool                          |
| LOAD_CONST4       | 4: byte1 byte2 byte3 byte4     | -> value                                     | load a constant from the constant pool                          |
| LOAD_GLOBAL       | 2: byte1 byte2                 | -> value                                     | load a value from a global variable                             |
| STORE_GLOBAL      | 2: byte1 byte2                 | value ->                                     | store a value to a global variable                              |
| LOAD_LOCAL        | 1: byte1                       | -> value                                     | load a value from a local variable                              |
| STORE_LOCAL       | 1: byte1                       | value ->                                     | store a value to a local variable                               |
| LOAD_FIELD        | 2: byte1 byte2                 | value -> value                               | load a value from a instance field                              |
| STORE_FIELD       | 2: byte1 byte2                 | value1 value2 ->                             | store a value into a instance field                             |
| IMPORT_ENV        | 1: byte1                       | value1 [value2] ->                           | import environmental variable                                   |
| LOAD_ENV          |                                | value -> value                               | get environmental variable                                      |
| STORE_ENV         |                                | value1 value2 ->                             | set environmental variable                                      |
| NEW_ENV_CTX       |                                | -> value                                     | push new EnvCtxObject onto the stack                            |
| ADD2ENV_CTX       |                                | ctx name value -> ctx                        | set and save env                                                |
| NEW_TIMER         |                                | -> value                                     | push new TimerObject onto the stack                             |
| BOX_LOCAL         | 1: byte1                       | [no change]                                  | box local variable                                              |
| LOAD_BOXED        | 1: byte1                       | -> value                                     | load a value from a local variable and unbox                    |
| STORE_BOXED       | 1: byte1                       | value ->                                     | box a value and store it to a local variable                    |
| NEW_CLOSURE       | 1: param                       | func value1 ~ valueN -> result               | create new closure                                              |
| LOAD_UPVAR        | 1: byte1                       | -> value                                     | load a value from an upvalue                                    |
| LOAD_RAW_UPVAR    | 1: byte1                       | -> value                                     | load a value from an upvalue (not perform unboxing)             |
| STORE_UPVAR       | 1: byte1                       | value ->                                     | store a value to an upvalue                                     |
| POP               |                                | value ->                                     | pop stack top value                                             |
| DUP               |                                | value -> value value                         | duplicate top value                                             |
| DUP2              |                                | value1 value2 -> value1 value2 value1 value2 | duplicate top two value                                         |
| SWAP              |                                | value1 value2 -> value2 value1               | swap top two value                                              |
| STORE_BY_OFFSET   | 1: offset                      | value1 ~ valueN valueN+1 -> value1 ~valueN   | store stack top value onto (top - offset)                       |
| CONCAT            |                                | value1 value2 -> value3                      | concat string value1 and string value2                          |
| APPEND            |                                | value1 value2 -> value1                      | append string value2 with string value1                         |
| APPEND_ARRAY      |                                | value1 value2 -> value1                      | append value2 into value1                                       |
| APPEND_MAP        |                                | value1 value2 value3 -> value1               | append value2 and value3 into value1                            |
| ITER_HAS_NEXT     | 2: offset1 offset2             | value -> value                               | check iterator has next element                                 |
| MAP_ITER_NEXT     | 2: offset1 offset2             | recv -> value key                            | get next map entry                                              |
| NEW               | 3: byte1 ~ byte3               | -> value                                     | create an empty object of a specified type                      |
| INIT_FIELDS       | 2: offset size                 | value -> value                               | init object fields                                              |
| CALL_FUNC         | 1: param                       | func param1 ~ paramN -> result               | apply function object                                           |
| CALL_METHOD       | 3: param byte1 byte2           | recv param1 ~ paramN -> result               | call method                                                     |
| CALL_BUILTIN      | 2: param index                 | param1 ~ paramN -> result                    | call builtin function                                           |
| CALL_BUILTIN2     | 1: index                       | -> value                                     | call builtin function (not wind stack)                          |
| RETURN            |                                | value -> [empty]                             | return value from callable                                      |
| RETURN_UDC        |                                | value -> [empty]                             | return from user-defined command                                |
| RETURN_SIG        |                                | [no change]                                  | return from signal handler                                      |
| RETURN_TERM       |                                | [no change]                                  | return from termination handler                                 |
| BRANCH            | 2: offset1 offset2             | value ->                                     | if value is false, branch to instruction at offset              |
| BRANCH_NOT        | 2: offset1 offset2             | value ->                                     | if value is not false, branch to instruction at offset          | 
| IF_INVALID        | 2: offset1 offset2             | value -> / [no change]                       | if stack top is invalid, branch to instruction at offset        |
| IF_NOT_INVALID    | 2: offset1 offset2             | value -> / [no change]                       | if stack top is not invalid, branch to instruction at offset    |
| GOTO              | 4: byte1 ~ byte4               | [no change]                                  | go to instruction at a specified index                          |
| JUMP_LOOP         | 4: byte1 ~ byte4               | [unwind until loop guard]                    | unwind stack top to guard before go to instruction              |
| JUMP_LOOP_V       | 4: byte1 ~ byte4               | [unwind until loop guard]                    | save and restore stack top during unwinding                     |
| LOOP_GUARD        |                                | -> value                                     | push guard value onto the stack                                 |
| JUMP_TRY          | 4: byte1 ~ byte4               | [unwind until try guard]                     | unwind stack top to guard before go to instruction              |
| JUMP_TRY_V        | 4: byte1 ~ byte4               | [unwind until try guard]                     | save and restore stack top during unwinding                     |
| TRY_GUARD         | 4: byte1 ~ byte4               | -> value                                     | push guard value onto the stack                                 |
| TRY_GUARD0        |                                | -> value                                     | push guard value onto the stack                                 |
| TRY_GUARD1        | 1: byte                        | -> value                                     | push guard value onto the stack                                 |
| THROW             |                                | value -> [empty]                             | throw exception                                                 |
| ENTER_FINALLY     | 4: byte1 ~ byte4               | [no change]                                  | save current pc and go to instruction                           |
| EXIT_FINALLY      |                                | [no change]                                  | pop stack top and go to instruction                             |
| LOOKUP_HASH       |                                | hashmap key ->                               | jump to the offset from stack top hashmap                       |
| REF_EQ            |                                | value1 value2 -> value                       | check referencial equality                                      |
| REF_NE            |                                | value1 value2 -> value                       | check referencial un-equality                                   |
| SYNC_PIPESTATUS   | 1: offset                      |                                              | update pipeline status and check error                          |
| FORK              | 3: byte1 offset1 offset2       | desc -> value                                | evaluate code in child shell                                    |
| PIPELINE          | 1: len 2: offset1 offset2 ...  | desc -> value                                | call pipeline                                                   |
| PIPELINE_SILENT   | 1: len 2: offset1 offset2 ...  | desc -> value                                | call pipeline without status check                              |
| PIPELINE_LP       | 1: len 2: offset1 offset2 ...  | desc -> value                                | call pipeline (lastPipe is true)                                |
| PIPELINE_ASYNC    | 1: k 1: len 2: offset1 offset2 | desc -> value                                | call pipeline asynchronously                                    | 
| EXPAND_TILDE      |                                | value -> value                               | perform tilde expansion                                         |
| APPEND_TILDE      |                                | value1 value2 -> value                       | perform tilde expansion and append                              | 
| PARSE_CLI         |                                | value ->                                     | parse command line arguments or return                          |
| NEW_CMD           |                                | value -> value                               | pop stack top and store it to new argv                          |
| ADD_CMD_ARG       |                                | argv redir value -> argv redir               | add stack top value as command argument                         |
| ADD_EXPANDING     | 2: len option                  | argv redir value1 ~ valueN+1 -> argv redir   | apply brace/glob expansion and add results to argv              |
| CALL_CMD          |                                | argv redir -> value                          | call builtin or external command.                               |
| CALL_CMD_NOFORK   |                                | argv redir -> value                          | call builtin or external command without fork                   |
| CALL_CMD_SILENT   |                                | argv redir -> value                          | call builtin or external command without status check           |
| CALL_UDC          | 2: byte1 byte2                 | argv redir -> value                          | call user-defined command                                       |
| CALL_UDC_SILENT   | 2: byte1 byte2                 | argv redir -> value                          | call user-defined command without status check                  |
| CALL_CMD_COMMON   |                                | argv redir -> value                          | call command (user-defined, builtin, external)                  |
| CALL_CMD_OBJ      |                                | obj argv redir -> value                      | call command object                                             |
| BUILTIN_CMD       |                                | -> value                                     | call builtin command command                                    |
| BUILTIN_CALL      |                                | -> value                                     | call builtin call command                                       |
| BUILTIN_EXEC      |                                | -> value / [terminate]                       | call builtin exec command                                       |
| BUILTIN_EVAL      |                                |                                              | call builtin eval command                                       |
| NEW_REDIR         |                                | -> value                                     | create new RedireConfig                                         |
| ADD_REDIR_OP      | 1: byte1 1: fd                 | redir value -> redir                         | add stack top value as redirection src and target               |
| ADD_REDIR_OP0     | 1: byte1                       | redir value -> redir                         | add stack top value as redirection src (for stdin)              |
| ADD_REDIR_OP1     | 1: byte1                       | redir value -> redir                         | add stack top value as redirection src (for stdout)             |
| ADD_REDIR_OP2     | 1: byte1                       | redir value -> redir                         | add stack top value as redirection src (for stderr)             |
| DO_REDIR          |                                | value -> value                               | perform redirection                                             |
| LOAD_CUR_MOD      |                                | -> value                                     | load a current module                                           |
| LOAD_CUR_ARG0     |                                | -> value                                     | load a current function arg0                                    |
| LOAD_CUR_THROWN   |                                | -> value                                     | load a current thrown object                                    |
| LOAD_STATUS       |                                | -> value                                     | load exit status onto the stack                                 |
| RAND              |                                | -> value                                     | generate random number and push stack top                       |
| GET_SECOND        |                                | -> value                                     | get differential time between current and base                  |
| SET_SECOND        |                                | value ->                                     | set base time                                                   |
| GET_POS_ARG       |                                | args pos -> value                            | get positional argument                                         |
| UNWRAP            |                                | value -> value                               | unwrap option value                                             |
| CHECK_INVALID     |                                | value -> value                               | check if option value has a value                               |
| RECLAIM_LOCAL     | 2: offset1 size1               | [no change]                                  | reclaim local variables specified range                         |
