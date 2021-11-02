/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_SYMBOL_H
#define YDSH_SYMBOL_H

#include <cstddef>
#include <cstdint>

#include "misc/flag_util.hpp"
#include "misc/string_ref.hpp"
#include <config.h>

namespace ydsh {

/**
 * enum order is corresponding to builtin variable declaration order.
 */
enum class BuiltinVarOffset : unsigned int {
  SCRIPT_NAME, // SCRIPT_NAME
  SCRIPT_DIR,  // SCRIPT_DIR
  REPLY,       // REPLY (for read command)
  REPLY_VAR,   // reply (fo read command)
  PID,         // PID (current process)
  PPID,        // PPID
  MODULE,      // MODULE
  RANDOM,      // RANDOM
  SECONDS,     // SECONDS
  IFS,         // IFS
  COMPREPLY,   // COMPREPLY
  PIPESTATUS,  // PIPESTATUS
  EXIT_STATUS, // ?
  SHELL_PID,   // $
  ARGS,        // @
  ARGS_SIZE,   // #
  POS_0,       // 0 (for script name)
  POS_1,       // 1 (for argument)
               /*POS_2, POS_3, POS_4, POS_5, POS_6, POS_7, POS_8, POS_9, */
};

inline unsigned int toIndex(BuiltinVarOffset offset) { return static_cast<unsigned int>(offset); }

/**
 * built-in symbol(builtin variable, magic method) definition
 */

// =====  builtin variable  =====

// io
constexpr const char *VAR_STDIN = "STDIN";
constexpr const char *VAR_STDOUT = "STDOUT";
constexpr const char *VAR_STDERR = "STDERR";

// shell builtin
constexpr const char *VAR_EIDT_HOOK = "EDIT_HOOK";
constexpr const char *VAR_CMD_FALLBACK = "CMD_FALLBACK";
constexpr const char *VAR_DEF_SIGINT = "_DEF_SIGINT";
constexpr const char *CMD_FALLBACK_HANDLER = "_cmd_fallback_handler";

constexpr const char *VAR_HISTFILE = "HISTFILE";
constexpr const char *VAR_HISTSIZE = "HISTSIZE";
constexpr const char *VAR_HISTFILESIZE = "HISTFILESIZE";

constexpr const char *VAR_SIG_DFL = "SIG_DFL";
constexpr const char *VAR_SIG_IGN = "SIG_IGN";

constexpr const char *VAR_TERM_HOOK = "TERM_HOOK";
constexpr const char *VAR_COMP_HOOK = "COMP_HOOK";

constexpr const char *VAR_YDSH_BIN = "YDSH_BIN";
constexpr const char *VAR_IFS = "IFS";

// used in constant expression
constexpr const char *CVAR_SCRIPT_DIR = "SCRIPT_DIR";
constexpr const char *CVAR_SCRIPT_NAME = "SCRIPT_NAME";
constexpr const char *CVAR_VERSION = "YDSH_VERSION";
constexpr const char *CVAR_CONFIG_DIR = "CONFIG_DIR";
constexpr const char *CVAR_DATA_DIR = "DATA_DIR";
constexpr const char *CVAR_MODULE_DIR = "MODULE_DIR";
constexpr const char *CVAR_OSTYPE = "OSTYPE";
constexpr const char *CVAR_MACHTYPE = "MACHTYPE";

constexpr const unsigned int CVAR_OFFSET_SCRIPT_NAME = 0;
constexpr const unsigned int CVAR_OFFSET_SCRIPT_DIR = 1;

// =====  magic method  =====

// constructor
constexpr const char *OP_INIT = "";

// unary op definition
constexpr const char *OP_PLUS = "%plus";   // +
constexpr const char *OP_MINUS = "%minus"; // -
constexpr const char *OP_NOT = "%not";     // not

// binary op definition
constexpr const char *OP_ADD = "%add"; // +
constexpr const char *OP_SUB = "%sub"; // -
constexpr const char *OP_MUL = "%mul"; // *
constexpr const char *OP_DIV = "%div"; // /
constexpr const char *OP_MOD = "%mod"; // %

constexpr const char *OP_EQ = "%eq"; // ==
constexpr const char *OP_NE = "%ne"; // !=

constexpr const char *OP_LT = "%lt"; // <
constexpr const char *OP_GT = "%gt"; // >
constexpr const char *OP_LE = "%le"; // <=
constexpr const char *OP_GE = "%ge"; // >=

constexpr const char *OP_AND = "%and"; // &
constexpr const char *OP_OR = "%or";   // |
constexpr const char *OP_XOR = "%xor"; // ^

constexpr const char *OP_MATCH = "%match";     // =~
constexpr const char *OP_UNMATCH = "%unmatch"; // !~

// indexer op
constexpr const char *OP_GET = "%get"; // []
constexpr const char *OP_SET = "%set"; // [] =

// iterator (for-in)
constexpr const char *OP_ITER = "%iter";
constexpr const char *OP_NEXT = "%next";
constexpr const char *OP_HAS_NEXT = "%has_nex";

constexpr const char *OP_BOOL = "%bool"; // for boolean cast

// to string
constexpr const char *OP_STR = "%str";       // for string cast or command argument
constexpr const char *OP_INTERP = "%interp"; // for interpolation

// to command argument
constexpr const char *OP_CMD_ARG = "%cmd_arg"; // for command argument

// num cast
constexpr const char *OP_TO_INT = "toInt";
constexpr const char *OP_TO_FLOAT = "toFloat";

// =====  environmental variable  =====

constexpr const char *ENV_HOME = "HOME";
constexpr const char *ENV_LOGNAME = "LOGNAME";
constexpr const char *ENV_USER = "USER";
constexpr const char *ENV_PWD = "PWD";
constexpr const char *ENV_OLDPWD = "OLDPWD";
constexpr const char *ENV_PATH = "PATH";
constexpr const char *ENV_SHLVL = "SHLVL";
constexpr const char *ENV_TERM = "TERM";

// =====  default value  =====

constexpr const char *VAL_DEFAULT_PATH = "/bin:/usr/bin:/usr/local/bin";

// =====  system error message prefix  =====

constexpr const char *EXEC_ERROR = "execution error: ";
constexpr const char *REDIR_ERROR = "io redirection error";
constexpr const char *UNDEF_ENV_ERROR = "undefined environmental variable: ";
constexpr const char *SET_ENV_ERROR = "not set environmental variable: ";

// =====  generic type name  =====

constexpr const char *TYPE_ARRAY = "Array";
constexpr const char *TYPE_MAP = "Map";
constexpr const char *TYPE_TUPLE = "Tuple";
constexpr const char *TYPE_OPTION = "Option";
constexpr const char *TYPE_FUNC = "Func";

// =====  UDC parameter offset =====

constexpr unsigned int UDC_PARAM_ATTR = 0;
constexpr unsigned int UDC_PARAM_REDIR = 1;
constexpr unsigned int UDC_PARAM_ARGV = 2;

// =====  termination kind  =====

constexpr unsigned int TERM_ON_EXIT = 1u << 0u;
constexpr unsigned int TERM_ON_ERR = 1u << 1u;
constexpr unsigned int TERM_ON_ASSERT = 1u << 2u;

// =====  for symbol lookup =====
constexpr const char *CMD_SYMBOL_SUFFIX = "%c";
constexpr const char *TYPE_ALIAS_SYMBOL_SUFFIX = "%t";
constexpr const char *MOD_HOLDER_SYMBOL_SUFFIX = "%m";

constexpr const char *MOD_SYMBOL_PREFIX = "%mod";

constexpr const char *DENIED_REDEFINED_CMD_LIST[] = {
    "eval", "exit", "exec", "command", "_exit",
};

inline std::string toCmdFullName(StringRef cmdName) {
  std::string name = cmdName.toString();
  name += CMD_SYMBOL_SUFFIX;
  return name;
}

inline std::string toTypeAliasFullName(StringRef alias) {
  std::string name = alias.toString();
  name += TYPE_ALIAS_SYMBOL_SUFFIX;
  return name;
}

inline std::string toModHolderName(unsigned short modId, bool global) {
  std::string name = global ? "_g" : "_n";
  name += std::to_string(modId);
  name += MOD_HOLDER_SYMBOL_SUFFIX;
  return name;
}

inline std::string toModTypeName(unsigned short modId) {
  std::string str = MOD_SYMBOL_PREFIX;
  str += std::to_string(modId);
  return str;
}

inline bool isCmdFullName(StringRef ref) { return ref.endsWith(CMD_SYMBOL_SUFFIX); }

inline bool isTypeAliasFullName(StringRef ref) { return ref.endsWith(TYPE_ALIAS_SYMBOL_SUFFIX); }

inline bool isModHolderName(StringRef ref) { return ref.endsWith(MOD_HOLDER_SYMBOL_SUFFIX); }

inline bool isNamedModHolderName(StringRef ref) {
  return ref.startsWith("_n") && isModHolderName(ref);
}

inline bool isGlobalModHolderName(StringRef ref) {
  return ref.startsWith("_g") && isModHolderName(ref);
}

inline bool isMagicMethodName(StringRef ref) { return ref.startsWith("%"); }

inline bool isVarName(StringRef ref) {
  return !isCmdFullName(ref) && !isTypeAliasFullName(ref) && !isModHolderName(ref) &&
         !isMagicMethodName(ref);
}

// =====  other constants  =====

constexpr const char *BUILD_ARCH =
#ifdef __x86_64__
    "x86_64"
#elif defined __i386__
    "i386"
#elif defined __aarch64__
    "aarch64"
#elif defined __EMSCRIPTEN__
    "emscripten"
#else
#error "unsupported architecture"
#endif
    ;

enum class ForkKind : unsigned char {
  NONE,     // do nothing
  STR,      // capture stdout as string. ex. "$(echo)"
  ARRAY,    // capture stdout as string array. ex. $(echo)
  IN_PIPE,  // capture stdin as pipe. ex. >(echo)
  OUT_PIPE, // capture stdout as pipe. ex. <(echo)
  COPROC,   // launch as co-process. ex. coproc echo
  JOB,      // launch as background job. ex. echo &
  DISOWN,   // launch as disowned background job. ex. echo &!
};

enum class GlobMeta : unsigned char {
  ANY,
  ZERO_OR_MORE,
};

inline const char *toString(GlobMeta meta) {
  switch (meta) {
  case GlobMeta::ANY:
    return "?";
  case GlobMeta::ZERO_OR_MORE:
    return "*";
  }
  return ""; // normally unreachable
}

// ===== for configuration =====
constexpr const char *LOCAL_CONFIG_DIR = "~/.ydsh";
constexpr const char *LOCAL_MOD_DIR = "~/.ydsh/module";

constexpr const char *SYSTEM_DATA_DIR = X_DATADIR "/ydsh";
constexpr const char *SYSTEM_MOD_DIR = X_DATADIR "/ydsh/module";

// ===== limit ======

constexpr size_t SYS_LIMIT_TYPE_ID = 0xFFFFFF; // UINT24_MAX
constexpr size_t SYS_LIMIT_TUPLE_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_FUNC_PARAM_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_METHOD_PARAM_NUM = UINT8_MAX - 1;
constexpr size_t SYS_LIMIT_MOD_ID = UINT16_MAX;
constexpr size_t SYS_LIMIT_GLOBAL_NUM = UINT16_MAX; // FIXME: check global var limit
constexpr size_t SYS_LIMIT_LOCAL_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_PIPE_LEN = 250;
constexpr size_t SYS_LIMIT_GLOB_FRAG_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_FUNC_LEN = UINT32_MAX;

} // namespace ydsh

#endif // YDSH_SYMBOL_H
