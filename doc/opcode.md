## Specification of op code

| **Mnemonic**  | **Other bytes**                | **Stack (before -> after)**                  | **Description**                                    |
|---------------|--------------------------------|----------------------------------------------|----------------------------------------------------|
| HALT          |                                | [no change]                                  | stop evaluation of interpreter                     |
| ASSERT        |                                | value1 value2 ->                             | assertion that value1 is true.                     |
| PRINT         | 8: ptr1 ~ ptr8                 | value ->                                     | print specified type and value on top of the stack |
| INSTANCE_OF   | 8: ptr1 ~ ptr8                 | value -> value                               | check if a value is instance of a specified type   |
| CHECK_CAST    | 8: ptr1 ~ ptr8                 | value -> value                               | check if a value is instance of a specified type   |
| PUSH_NULL     |                                | -> value                                     | push the null value onto the stack                 |
| PUSH_TRUE     |                                | -> value                                     | push the true value onto the stack                 |
| PUSH_FALSE    |                                | -> value                                     | push the false value onto the stack                |
| PUSH_ESTRING  |                                | -> value                                     | push the empty string value onto the stack         |
| LOAD_CONST    | 1: byte1                       | -> value                                     | load a constant from the constant pool             |
| LOAD_CONST_W  | 2: byte1 byte2                 | -> value                                     | load a constant from the constant pool             |
| LOAD_CONST_T  | 3: byte1 byte2 byte3           | -> value                                     | load a constant from the constant pool             |
| LOAD_GLOBAL   | 2: byte1 byte2                 | -> value                                     | load a value from a global variable                |
| STORE_GLOBAL  | 2: byte1 byte2                 | value ->                                     | store a value to a global variable                 |
| LOAD_LOCAL    | 1: byte1                       | -> value                                     | load a value from a local variable                 |
| STORE_LOCAL   | 1: byte1                       | value ->                                     | store a value to a local variable                  |
| LOAD_FIELD    | 2: byte1 byte2                 | value -> value                               | load a value from a instance field                 |
| STORE_FIELD   | 2: byte1 byte2                 | value1 value2 ->                             | store a value into a instance field                |
| IMPORT_ENV    | 1: byte1                       | value1 [value2] ->                           | import environmental variable                      |
| LOAD_ENV      |                                | value -> value                               | get environmental variable                         |
| STORE_ENV     |                                | value1 value2 ->                             | set environmental variable                         |
| POP           |                                | value ->                                     | pop stack top value                                |
| DUP           |                                | value -> value value                         | duplicate top value                                |
| DUP2          |                                | value1 value2 -> value1 value2 value1 value2 | duplicate top two value                            |
| SWAP          |                                | value1 value2 -> value2 value1               | swap top two value                                 |
| NEW_STRING    |                                | -> value                                     | create an empty string value                       |
| APPEND_STRING |                                | value1 value2 -> value1                      | append value2 into value1                          |
| NEW_ARRAY     | 8 ptr1 ~ ptr8                  | -> value                                     | create an empty array value                        |
| APPEND_ARRAY  |                                | value1 value2 -> value1                      | append value2 into value1                          |
| NEW_MAP       | 8: ptr1 ~ ptr8                 | -> value                                     | create an empty map value                          |
| APPEND_MAP    |                                | value1 value2 value3 -> value1               | append value2 and value3 into value1               |
| NEW_TUPLE     | 8: ptr1 ~ ptr8                 | -> value                                     | create an empty tuple value                        |
| NEW           | 8: ptr1 ~ ptr8                 | -> value                                     | create an empty object of a specified type         |
| CALL_INIT     | 2: param1 param2               | recv param1 ~ paramN -> value                               | call constructor                                   |
| CALL_METHOD   | 4: param1 param2 index1 index2 | recv param1 ~ paramN -> result               | call virtual method                                |
| CALL_FUNC     | 2: param1 param2               | func param1 ~ paramN -> result               | apply function object                              |
| CALL_NATIVE   | 8: ptr1 ~ ptr8                 | -> value                                     | call native function                               |
| INVOKE_METHOD | 2: byte1 byte2                 | recv param1 ~ paramN -> result               | invoke interface method                            |
| INVOKE_GETTER | 2: byte1 byte2                 | recv -> value                                | invoke interface getter                            |
| INVOKE_SETTER | 2: byte1 byte2                 | recv value ->                                | invoke interface setter                            |
| INIT_MODULE   |                                | value -> value                               | initialize module                                  |
| RETURN        |                                | -> [empty]                                   | return from callable                               |
| RETURN_V      |                                | value -> [empty]                             | return value from callable                         |
| RETURN_UDC    |                                | value -> [empty]                             | return from user-defined command                   |
| RETURN_SIG    |                                | -> [empty]                                   | return form signal handler                         |
| BRANCH        | 2: offset1 offset2             | value ->                                     | if value is false, branch to instruction at offset |
| GOTO          | 4: byte1 ~ byte4               | [no change]                                  | go to instruction at a specified index             |
| THROW         |                                | value -> [empty]                             | throw exception                                    |
| EXIT_SHELL    |                                | value -> [terminate]                         | exit current shell                                 |
| ENTER_FINALLY | 2: offset1 offset2             | -> value                                     | save current pc and go to instruction              |
| EXIT_FINALLY  |                                | value ->                                     | pop stack top and go to instruction                |
| COPY_INT      | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| TO_BYTE       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| TO_U16        |                                | value -> value                               | convert number (see. int-cast.md)                  |
| TO_I16        |                                | value -> value                               | convert number (see. int-cast.md)                  |
| NEW_LONG      | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| COPY_LONG     | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| I_NEW_LONG    | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| NEW_INT       | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| U32_TO_D      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| I32_TO_D      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| U64_TO_D      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| I64_TO_D      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_U32      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_I32      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_U64      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_I64      |                                | value -> value                               | convert number (see. int-cast.md)                  |
| REF_EQ        |                                | value1 value2 -> value                       | check referencial equality                         |
| REF_NE        |                                | value1 value2 -> value                       | check referencial un-equality                      |
| FORK          | 1: byte1 2: offset1 offset2    | -> value                                     | evaluate code in child shell                       |
| PIPELINE      | 1: len 2: offset1 offset2 ...  | -> value                                     | call pipeline                                      |
| EXPAND_TILDE  |                                | value -> value                               | perform tilde expansion                            |
| NEW_CMD       |                                | value -> value                               | pop stack top and store it to new argv             |
| ADD_CMD_ARG   | 1: byte1                       | value1 value2 value3 -> value1 value2        | add stack top value as command argument            |
| CALL_CMD      |                                | value1 value2 -> value                       | call command. value1 is parameter, value2 is redir |
| CALL_CMD_P    |                                | value1 value2 -> value                       | call command in child                              |
| CALL_CMD_LP   |                                | value1 value2 -> value                       | call command in last pipeline                      |
| BUILTIN_CMD   |                                | -> value                                     | call builtin command command                       |
| BUILTIN_EVAL  |                                | -> value                                     | call builtin eval command                          |
| BUILTIN_EXEC  |                                | -> value / [terminate]                       | call builtin exec command                          |
| NEW_REDIR     |                                | -> value                                     | create new RedireConfig                            |
| ADD_REDIR_OP  | 1: byte1                       | value1 value2 -> value1                      | add stack top value as redirection target          |
| DO_REDIR      |                                | value -> value                               | perform redirection                                |
| DBUS_INIT_SIG |                                | [no change]                                  | init DBus signal match rule                        |
| DBUS_WAIT_SIG |                                | -> func param1 ~ paramN                      | wait DBus signal, then dispatched handler          |
| RAND          |                                | -> value                                     | generate random number and push stack top          |
| GET_SECOND    |                                | -> value                                     | get differential time between current and base     |
| SET_SECOND    |                                | value ->                                     | set base time                                      |
| UNWRAP        |                                | value -> value                               | unwrap option value                                |
| CHECK_UNWRAP  |                                | value -> value                               | check if option value has a value                  |
| TRY_UNWRAP    | 2: offset1 offset2             | value -> / [no change]                       | try to unwrap option value                         |
| NEW_INVALID   |                                | -> value                                     | create then invalid value                          |
| RECLAIM_LOCAL | 2: offset1 size1               | [no change]                                  | reclaim local variables specified range            |