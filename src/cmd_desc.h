/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef ARSH_CMD_DESC_H
#define ARSH_CMD_DESC_H

#include "misc/array_ref.hpp"

namespace arsh {

struct BuiltinCmdDesc {
  const char *name;
  const char *usage;
  const char *detail;
};

ArrayRef<BuiltinCmdDesc> getBuiltinCmdDescRange();

#define EACH_SHCTL_SUBCMD(OP)                                                                      \
  OP(INTERACTIVE, "is-interactive")                                                                \
  OP(SOURCED, "is-sourced")                                                                        \
  OP(BACKTRACE, "backtrace")                                                                       \
  OP(FUNCTION, "function")                                                                         \
  OP(MODULE, "module")                                                                             \
  OP(SET, "set")                                                                                   \
  OP(UNSET, "unset")                                                                               \
  OP(INFO, "info")                                                                                 \
  OP(WINSIZE, "winsize")

struct SHCTLSubCmdEntry {
  enum class Kind : unsigned char {
#define GEN_ENUM(O, S) O,
    EACH_SHCTL_SUBCMD(GEN_ENUM)
#undef GEN_ENUM
  };

  const Kind kind;
  const char name[15];
};

ArrayRef<SHCTLSubCmdEntry> getSHCTLSubCmdEntries();

#define EACH_RUNTIME_OPTION(OP)                                                                    \
  OP(ASSERT, (1u << 0u), "assert")                                                                 \
  OP(CLOBBER, (1u << 1u), "clobber")                                                               \
  OP(DOTGLOB, (1u << 2u), "dotglob")                                                               \
  OP(ERR_RAISE, (1u << 3u), "errraise")                                                            \
  OP(FAIL_GLOB, (1u << 4u), "failglob")                                                            \
  OP(FAIL_SIGPIPE, (1u << 5u), "failsigpipe")                                                      \
  OP(FAIL_TILDE, (1u << 6u), "failtilde")                                                          \
  OP(FASTGLOB, (1u << 7u), "fastglob")                                                             \
  OP(GLOBSTAR, (1u << 8u), "globstar")                                                             \
  OP(HUP_EXIT, (1u << 9u), "huponexit")                                                            \
  OP(MONITOR, (1u << 10u), "monitor")                                                              \
  OP(NULLGLOB, (1u << 11u), "nullglob")                                                            \
  OP(TRACE_EXIT, (1u << 12u), "traceonexit")                                                       \
  OP(XTRACE, (1u << 13u), "xtrace")

// set/unset via 'shctl' command
enum class RuntimeOption : unsigned short {
#define GEN_ENUM(E, V, N) E = (V),
  EACH_RUNTIME_OPTION(GEN_ENUM)
#undef GEN_ENUM
};

struct RuntimeOptionEntry {
  const RuntimeOption option;
  const char name[14];
};

ArrayRef<RuntimeOptionEntry> getRuntimeOptionEntries();

} // namespace arsh

#endif // ARSH_CMD_DESC_H
