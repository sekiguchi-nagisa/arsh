## Specification of op code

| **Mnemonic**  | **OpCode** | **Other bytes**                | **Stack (before -> after)**                  | **Description**                                    |
|---------------|------------|--------------------------------|----------------------------------------------|----------------------------------------------------|
| NOP           | 0x00       |                                | [no change]                                  | do nothing                                         |
| STOP_EVAL     | 0x01       |                                | [no change]                                  | stop evaluation of interpreter                     |
| ASSERT        | 0x02       |                                | value ->                                     | check if a value is true or not                    |
| PRINT         | 0x03       | 8: ptr1 ~ ptr8                 | value ->                                     | print specified type and value on top of the stack |
| INSTANCE_OF   | 0x04       | 8: ptr1 ~ ptr8                 | value -> value                               | check if a value is instance of a specified type   |
| CHECK_CAST    | 0x05       | 8: ptr1 ~ ptr8                 | value -> value                               | check if a value is instance of a specified type   |
| PUSH_TRUE     | 0x06       |                                | -> value                                     | push the true value onto the stack                 |
| PUSH_FALSE    | 0x07       |                                | -> value                                     | push the false value onto the stack                |
| PUSH_ESTRING  | 0x08       |                                | ->                                           | push the empty string value onto the stack         |
| LOAD_CONST    | 0x09       | 2: byte1 byte2                 | -> value                                     | load a constant from the constant pool             |
| LOAD_FUNC     | 0x0A       | 2: byte1 byte2                 | -> value                                     | load a function from a global variable             |
| LOAD_GLOBAL   | 0x0B       | 2: byte1 byte2                 | -> value                                     | load a value from a global variable                |
| STORE_GLOBAL  | 0x0C       | 2: byte1 byte2                 | value ->                                     | store a value to a global variable                 |
| LOAD_LOCAL    | 0x0D       | 2: byte1 byte2                 | -> value                                     | load a value from a local variable                 |
| STORE_LOCAL   | 0x0E       | 2: byte1 byte2                 | value ->                                     | store a value to a local variable                  |
| LOAD_FIELD    | 0x0F       | 2: byte1 byte2                 | value -> value                               | load a value from a instance field                 |
| STORE_FIELD   | 0x10       | 2: byte1 byte2                 | value1 value2 ->                             | store a value into a instance field                |
| IMPORT_ENV    | 0x11       | 1: byte1                       | value1 [value2] ->                           | import environmental variable                      |
| LOAD_ENV      | 0x12       |                                | value -> value                               | get environmental variable                         |
| STORE_ENV     | 0x13       |                                | value1 value2 ->                             | set environmental variable                         |
| POP           | 0x14       |                                | value ->                                     | pop stack top value                                |
| DUP           | 0x15       |                                | value -> value value                         | duplicate top value                                |
| DUP2          | 0x16       |                                | value1 value2 -> value1 value2 value1 value2 | duplicate top two value                            |
| SWAP          | 0x17       |                                | value1 value2 -> value2 value1               | swap top two value                                 |
| NEW_STRING    | 0x18       |                                | -> value                                     | create an empty string value                       |
| APPEND_STRING | 0x19       |                                | value1 value2 -> value1                      | append value2 into value1                          |
| NEW_ARRAY     | 0x1A       | 8 ptr1 ~ ptr8                  | -> value                                     | create an empty array value                        |
| APPEND_ARRAY  | 0x1B       |                                | value1 value2 -> value1                      | append value2 into value1                          |
| NEW_MAP       | 0x1C       | 8: ptr1 ~ ptr8                 | -> value                                     | create an empty map value                          |
| APPEND_MAP    | 0x1D       |                                | value1 value2 value3 -> value1               | append value2 and value3 into value1               |
| NEW_TUPLE     | 0x1E       | 8: ptr1 ~ ptr8                 | -> value                                     | create an empty tuple value                        |
| NEW           | 0x1F       | 8: ptr1 ~ ptr8                 | -> value                                     | create an empty object of a specified type         |
| CALL_INIT     | 0x20       | 2: byte1 byte2                 | value -> value                               | call constructor                                   |
| CALL_METHOD   | 0x21       | 4: index1 index2 param1 param2 | recv param1 ~ paramN -> result               | call virtual method                                |
| CALL_FUNC     | 0x22       | 2: param1 param2               | func param1 ~ paramN -> result               | apply function object                              |
| RETURN        | 0x23       |                                | -> [empty]                                   | return from callable                               |
| RETURN_V      | 0x24       |                                | value -> [empty]                             | return value from callable                         |
| BRANCH        | 0x25       | 2: offset1 offset2             | value ->                                     | if value is false, branch to instruction at offset |
| GOTO          | 0x26       | 4: byte1 ~ byte4               | [no change]                                  | go to instruction at a specified index             |
| THROW         | 0x27       |                                | value -> [empty]                             | throw exception                                    |
| ENTER_FINALLY | 0x28       | 4: byte1 ~ byte4               | -> value                                     | save current pc and go to instruction              |
| EXIT_FINALLY  | 0x29       |                                | value ->                                     | pop stack top and go to instruction                |
| COPY_INT      | 0x2A       | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| TO_BYTE       | 0x2B       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| TO_U16        | 0x2C       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| TO_I16        | 0x2D       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| NEW_LONG      | 0x2E       | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| COPY_LONG     | 0x2F       | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| I_NEW_LONG    | 0x30       | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| NEW_INT       | 0x31       | 1: byte1                       | value -> value                               | convert number (see. int-cast.md)                  |
| U32_TO_D      | 0x32       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| I32_TO_D      | 0x33       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| U64_TO_D      | 0x34       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| I64_TO_D      | 0x35       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_U32      | 0x36       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_I32      | 0x37       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_U64      | 0x38       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| D_TO_I64      | 0x39       |                                | value -> value                               | convert number (see. int-cast.md)                  |
| SUCCESS_CHILD | 0x3A       |                                | -> [terminate]                               | terminate current process                          |
| FAILURE_CHILD | 0x3B       |                                | -> [terminate]                               | terminate current process                          |
| CAPTURE_STR   | 0x3C       | 2: offset1 offset2             | -> value                                     | capture stdout as string                           |
| CAPTURE_ARRAY | 0x3D       | 2: offset1 offset2             | -> value                                     | capture stdout as string array                     |
| NEW_PIPELINE  | 0x3E       |                                | -> value                                     | create new pipeline                                |
| CALL_PIPELINE | 0x3F       | 1: len ...                     | [no change]                                  | call pipeline                                      |
| OPEN_PROC     | 0x40       |                                | value1 value2 -> value1                      | initialize process                                 |
| CLOSE_PROC    | 0x41       |                                | [no change]                                  | finalize process                                   |
| ADD_CMD_ARG   | 0x42       | 1: byte1                       | value1 value2 -> value1                      | add stack top value as command argument            |
| ADD_REDIR_OP  | 0x43       | 1: byte1                       | value1 value2 -> value1                      | add stack top value as redirection op              |
| EXPAND_TILDE  | 0x44       |                                | value -> value                               | perform tilde expansion                            |
| CALL_CMD      | 0x45       | 1: byte1                       | [no change]                                  | call command                                       |
| POP_PIPELINE  | 0x46       |                                | value -> value                               | if last exit status is 0, push true value          |