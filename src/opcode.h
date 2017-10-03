/*
 * Copyright (C) 2016-2017 Nagisa Sekiguchi
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

#ifndef YDSH_OPCODE_H
#define YDSH_OPCODE_H

namespace ydsh {

/**
 * see (doc/opcode.md)
 */
#define OPCODE_LIST(OP) \
    OP(HALT, 0) \
    OP(ASSERT, 0) \
    OP(PRINT, 8) \
    OP(INSTANCE_OF, 8) \
    OP(CHECK_CAST, 8) \
    OP(PUSH_NULL, 0) \
    OP(PUSH_TRUE, 0) \
    OP(PUSH_FALSE, 0) \
    OP(PUSH_ESTRING, 0) \
    OP(LOAD_CONST, 1) \
    OP(LOAD_CONST_W, 2) \
    OP(LOAD_CONST_T, 3) \
    OP(LOAD_FUNC, 2) \
    OP(LOAD_GLOBAL, 2) \
    OP(STORE_GLOBAL, 2) \
    OP(LOAD_LOCAL, 1) \
    OP(STORE_LOCAL, 1) \
    OP(LOAD_FIELD, 2) \
    OP(STORE_FIELD, 2) \
    OP(IMPORT_ENV, 1) \
    OP(LOAD_ENV, 0) \
    OP(STORE_ENV, 0) \
    OP(POP, 0) \
    OP(DUP, 0) \
    OP(DUP2, 0) \
    OP(SWAP, 0) \
    OP(NEW_STRING, 0) \
    OP(APPEND_STRING, 0) \
    OP(NEW_ARRAY, 8) \
    OP(APPEND_ARRAY, 0) \
    OP(NEW_MAP, 8) \
    OP(APPEND_MAP, 0) \
    OP(NEW_TUPLE, 8) \
    OP(NEW, 8) \
    OP(CALL_INIT, 2) \
    OP(CALL_METHOD, 4) \
    OP(CALL_FUNC, 2) \
    OP(CALL_NATIVE, 8) \
    OP(INVOKE_METHOD, 2) \
    OP(INVOKE_GETTER, 2) \
    OP(INVOKE_SETTER, 2) \
    OP(RETURN, 0) \
    OP(RETURN_V, 0) \
    OP(RETURN_UDC, 0) \
    OP(RETURN_SIG, 0) \
    OP(BRANCH, 2) \
    OP(GOTO, 4) \
    OP(THROW, 0) \
    OP(EXIT_SHELL, 0) \
    OP(ENTER_FINALLY, 2) \
    OP(EXIT_FINALLY, 0) \
    OP(COPY_INT, 1) \
    OP(TO_BYTE, 0) \
    OP(TO_U16, 0) \
    OP(TO_I16, 0) \
    OP(NEW_LONG, 1) \
    OP(COPY_LONG, 1) \
    OP(I_NEW_LONG, 1) \
    OP(NEW_INT, 1) \
    OP(U32_TO_D, 0) \
    OP(I32_TO_D, 0) \
    OP(U64_TO_D, 0) \
    OP(I64_TO_D, 0) \
    OP(D_TO_U32, 0) \
    OP(D_TO_I32, 0) \
    OP(D_TO_U64, 0) \
    OP(D_TO_I64, 0) \
    OP(SUCCESS_CHILD, 0) \
    OP(FAILURE_CHILD, 0) \
    OP(CAPTURE_STR, 2) \
    OP(CAPTURE_ARRAY, 2) \
    OP(PIPELINE, -1) \
    OP(PIPELINE2, -1) \
    OP(EXPAND_TILDE, 0) \
    OP(NEW_CMD, 0) \
    OP(ADD_CMD_ARG, 1) \
    OP(CALL_CMD, 0) \
    OP(CALL_CMD_P, 0) \
    OP(BUILTIN_CMD, 0) \
    OP(BUILTIN_EVAL, 0) \
    OP(BUILTIN_EXEC, 0) \
    OP(NEW_REDIR, 0) \
    OP(ADD_REDIR_OP, 1) \
    OP(DO_REDIR, 0) \
    OP(DBUS_INIT_SIG, 0) \
    OP(DBUS_WAIT_SIG, 0) \
    OP(RAND, 0) \
    OP(GET_SECOND, 0) \
    OP(SET_SECOND, 0) \
    OP(UNWRAP, 0) \
    OP(CHECK_UNWRAP, 0) \
    OP(TRY_UNWRAP, 2) \
    OP(NEW_INVALID, 0) \
    OP(RECLAIM_LOCAL, 2)

enum class OpCode : unsigned char {
#define GEN_OPCODE(CODE, N) CODE,
    OPCODE_LIST(GEN_OPCODE)
#undef GEN_OPCODE
};

int getByteSize(OpCode code);

bool isTypeOp(OpCode code);

} // namespace ydsh

#endif //YDSH_OPCODE_H
