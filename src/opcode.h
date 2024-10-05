/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
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

#ifndef ARSH_OPCODE_H
#define ARSH_OPCODE_H

namespace arsh {

/**
 * see (doc/opcode.md)
 *
 * OP(C, L, S)
 * C: opcode name
 * L: operand length
 * S: stack consumption
 *    if positive number, increase stack top index
 *    if negative number, decrease stack top index
 */
#define OPCODE_LIST(OP)                                                                            \
  OP(HALT, 0, 0)                                                                                   \
  OP(ASSERT_ENABLED, 2, 0)                                                                         \
  OP(ASSERT_FAIL, 0, -1)                                                                           \
  OP(ASSERT_FAIL2, 1, -3)                                                                          \
  OP(PRINT, 3, -1)                                                                                 \
  OP(INSTANCE_OF, 0, -1)                                                                           \
  OP(CHECK_CAST, 3, 0)                                                                             \
  OP(CHECK_CAST_OPT, 3, 0)                                                                         \
  OP(PUSH_TYPE, 3, 1)                                                                              \
  OP(PUSH_NULL, 0, 1)                                                                              \
  OP(PUSH_TRUE, 0, 1)                                                                              \
  OP(PUSH_FALSE, 0, 1)                                                                             \
  OP(PUSH_SIG, 1, 1)                                                                               \
  OP(PUSH_INT, 1, 1)                                                                               \
  OP(PUSH_STR0, 0, 1)                                                                              \
  OP(PUSH_STR1, 1, 1)                                                                              \
  OP(PUSH_STR2, 2, 1)                                                                              \
  OP(PUSH_STR3, 3, 1)                                                                              \
  OP(PUSH_META, 2, 1)                                                                              \
  OP(PUSH_INVALID, 0, 1)                                                                           \
  OP(LOAD_CONST, 1, 1)                                                                             \
  OP(LOAD_CONST2, 2, 1)                                                                            \
  OP(LOAD_CONST4, 4, 1)                                                                            \
  OP(LOAD_GLOBAL, 2, 1)                                                                            \
  OP(STORE_GLOBAL, 2, -1)                                                                          \
  OP(LOAD_LOCAL, 1, 1)                                                                             \
  OP(LOAD_LOCAL2, 1, 2)                                                                            \
  OP(STORE_LOCAL, 1, -1)                                                                           \
  OP(LOAD_FIELD, 2, 0)                                                                             \
  OP(STORE_FIELD, 2, -2)                                                                           \
  OP(IMPORT_ENV, 1, -1)                                                                            \
  OP(LOAD_ENV, 0, 0)                                                                               \
  OP(STORE_ENV, 0, -2)                                                                             \
  OP(NEW_ENV_CTX, 0, 1)                                                                            \
  OP(ADD2ENV_CTX, 0, -2)                                                                           \
  OP(NEW_TIMER, 0, 1)                                                                              \
  OP(BOX_LOCAL, 1, 0)                                                                              \
  OP(LOAD_BOXED, 1, 1)                                                                             \
  OP(STORE_BOXED, 1, -1)                                                                           \
  OP(NEW_CLOSURE, 1, 0)                                                                            \
  OP(LOAD_UPVAR, 1, 1)                                                                             \
  OP(LOAD_RAW_UPVAR, 1, 1)                                                                         \
  OP(STORE_UPVAR, 1, -1)                                                                           \
  OP(POP, 0, -1)                                                                                   \
  OP(DUP, 0, 1)                                                                                    \
  OP(DUP2, 0, 2)                                                                                   \
  OP(SWAP, 0, 0)                                                                                   \
  OP(STORE_BY_OFFSET, 1, -1)                                                                       \
  OP(CONCAT, 0, -1)                                                                                \
  OP(APPEND, 0, -1)                                                                                \
  OP(APPEND_ARRAY, 0, -1)                                                                          \
  OP(APPEND_MAP, 0, -2)                                                                            \
  OP(ITER_HAS_NEXT, 2, 0)                                                                          \
  OP(MAP_ITER_NEXT, 2, 1)                                                                          \
  OP(NEW, 3, 1)                                                                                    \
  OP(INIT_FIELDS, 2, 0)                                                                            \
  OP(CALL_FUNC, 1, 0)                                                                              \
  OP(CALL_METHOD, 3, 0)                                                                            \
  OP(CALL_BUILTIN, 2, 0)                                                                           \
  OP(CALL_BUILTIN2, 1, 1)                                                                          \
  OP(RETURN, 0, -1)                                                                                \
  OP(RETURN_UDC, 0, -1)                                                                            \
  OP(RETURN_SIG, 0, 0)                                                                             \
  OP(RESTORE_STATUS, 0, 0)                                                                         \
  OP(BRANCH, 2, -1)                                                                                \
  OP(BRANCH_NOT, 2, -1)                                                                            \
  OP(IF_INVALID, 2, 0)                                                                             \
  OP(IF_NOT_INVALID, 2, 0)                                                                         \
  OP(GOTO, 4, 0)                                                                                   \
  OP(JUMP_LOOP, 4, 0)                                                                              \
  OP(JUMP_LOOP_V, 4, 0)                                                                            \
  OP(LOOP_GUARD, 0, 1)                                                                             \
  OP(JUMP_TRY, 4, 0)                                                                               \
  OP(JUMP_TRY_V, 4, 0)                                                                             \
  OP(TRY_GUARD, 4, 1)                                                                              \
  OP(TRY_GUARD0, 0, 1)                                                                             \
  OP(TRY_GUARD1, 1, 1)                                                                             \
  OP(THROW, 0, -1)                                                                                 \
  OP(ENTER_FINALLY, 4, 0)                                                                          \
  OP(EXIT_FINALLY, 0, 0)                                                                           \
  OP(LOOKUP_HASH, 0, -2)                                                                           \
  OP(REF_EQ, 0, -1)                                                                                \
  OP(REF_NE, 0, -1)                                                                                \
  OP(SYNC_PIPESTATUS, 1, 0)                                                                        \
  OP(FORK, 3, 0)                                                                                   \
  OP(PIPELINE, -1, 0)                                                                              \
  OP(PIPELINE_SILENT, -1, 0)                                                                       \
  OP(PIPELINE_LP, -1, 0)                                                                           \
  OP(PIPELINE_ASYNC, -1, 0)                                                                        \
  OP(EXPAND_TILDE, 0, 0)                                                                           \
  OP(APPEND_TILDE, 0, -1)                                                                          \
  OP(PARSE_CLI, 0, -1)                                                                             \
  OP(NEW_CMD, 0, 0)                                                                                \
  OP(ADD_CMD_ARG, 0, -1)                                                                           \
  OP(ADD_EXPANDING, 2, 0)                                                                          \
  OP(CALL_CMD, 0, -1)                                                                              \
  OP(CALL_CMD_NOFORK, 0, -1)                                                                       \
  OP(CALL_CMD_SILENT, 0, -1)                                                                       \
  OP(CALL_UDC, 2, -1)                                                                              \
  OP(CALL_UDC_SILENT, 2, -1)                                                                       \
  OP(CALL_CMD_COMMON, 0, -1)                                                                       \
  OP(CALL_CMD_OBJ, 0, -2)                                                                          \
  OP(BUILTIN_CMD, 0, 1)                                                                            \
  OP(BUILTIN_CALL, 0, 1)                                                                           \
  OP(BUILTIN_EXEC, 0, 1)                                                                           \
  OP(NEW_REDIR, 0, 1)                                                                              \
  OP(ADD_REDIR_OP, 2, -1)                                                                          \
  OP(ADD_REDIR_OP0, 1, -1)                                                                         \
  OP(ADD_REDIR_OP1, 1, -1)                                                                         \
  OP(ADD_REDIR_OP2, 1, -1)                                                                         \
  OP(DO_REDIR, 0, 0)                                                                               \
  OP(LOAD_CUR_MOD, 0, 1)                                                                           \
  OP(LOAD_CUR_ARG0, 0, 1)                                                                          \
  OP(RAND, 0, 1)                                                                                   \
  OP(GET_SECOND, 0, 1)                                                                             \
  OP(SET_SECOND, 0, -1)                                                                            \
  OP(GET_POS_ARG, 0, -1)                                                                           \
  OP(UNWRAP, 0, 0)                                                                                 \
  OP(CHECK_INVALID, 0, 0)                                                                          \
  OP(RECLAIM_LOCAL, 2, 0)

enum class OpCode : unsigned char {
#define GEN_OPCODE(CODE, N, S) CODE,
  OPCODE_LIST(GEN_OPCODE)
#undef GEN_OPCODE
};

int getByteSize(OpCode code);

bool isTypeOp(OpCode code);

} // namespace arsh

#endif // ARSH_OPCODE_H
