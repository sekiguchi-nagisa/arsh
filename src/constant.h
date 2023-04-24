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
  DUMMY_,      // dummy entry (indicate builtin module object)
  SCRIPT_NAME, // SCRIPT_NAME
  SCRIPT_DIR,  // SCRIPT_DIR
  REPLY,       // REPLY (for read command)
  REPLY_VAR,   // reply (fo read command)
  DYNA_UDCS,   // DYNA_UDCS (for dynamic registered command)
  MODULE,      // MODULE
  RANDOM,      // RANDOM
  SECONDS,     // SECONDS
  IFS,         // IFS
  COMPREPLY,   // COMPREPLY
  DIRSTACK,    // DIRSTACK (for directory stack
  STDIN,       // STDIN
  STDOUT,      // STDOUT
  STDERR,      // STDERR
  PIPESTATUS,  // PIPESTATUS
  EXIT_STATUS, // ?
  ARGS,        // @
  ARGS_SIZE,   // # (dummy entry for completion)
  POS_0,       // 0 (for script name)
  SHELL_PID,   // $
  PID,         // PID (current process)
  PPID,        // PPID
};

inline unsigned int toIndex(BuiltinVarOffset offset) { return static_cast<unsigned int>(offset); }

/**
 * built-in symbol(builtin variable, magic method) definition
 */

// =====  builtin variable  =====

// shell builtin
constexpr const char *VAR_LINE_EDIT = "LINE_EDIT";
constexpr const char *VAR_CMD_FALLBACK = "CMD_FALLBACK";
constexpr const char *VAR_DEF_SIGINT = "_DEF_SIGINT";

constexpr const char *VAR_SIG_DFL = "SIG_DFL";
constexpr const char *VAR_SIG_IGN = "SIG_IGN";

constexpr const char *VAR_TERM_HOOK = "TERM_HOOK";
constexpr const char *VAR_COMP_HOOK = "COMP_HOOK";

constexpr const char *VAR_YDSH_BIN = "YDSH_BIN";
constexpr const char *VAR_IFS = "IFS";
constexpr const char *VAR_THIS = "this"; // for method receiver

// used in constant expression
constexpr const char *CVAR_SCRIPT_DIR = "SCRIPT_DIR";
constexpr const char *CVAR_SCRIPT_NAME = "SCRIPT_NAME";

constexpr const unsigned int CVAR_OFFSET_SCRIPT_NAME = 0;
constexpr const unsigned int CVAR_OFFSET_SCRIPT_DIR = 1;

// =====  magic method  =====

// constructor
constexpr const char *OP_INIT = "%init";

// unary op definition
#define DEFINE_UNARY_OP_NAME(OP) "%u_" OP

constexpr const char *UNARY_OP_NAME_PREFIX = DEFINE_UNARY_OP_NAME("");

constexpr const char *OP_PLUS = DEFINE_UNARY_OP_NAME("+");
constexpr const char *OP_MINUS = DEFINE_UNARY_OP_NAME("-");
constexpr const char *OP_NOT = DEFINE_UNARY_OP_NAME("!");

#undef DEFINE_UNARY_OP_NAME

// binary op definition
#define DEFINE_BINARY_OP_NAME(OP) "%b_" OP

constexpr const char *BINARY_OP_NAME_PREFIX = DEFINE_BINARY_OP_NAME("");

constexpr const char *OP_ADD = DEFINE_BINARY_OP_NAME("+");
constexpr const char *OP_SUB = DEFINE_BINARY_OP_NAME("-");
constexpr const char *OP_MUL = DEFINE_BINARY_OP_NAME("*");
constexpr const char *OP_DIV = DEFINE_BINARY_OP_NAME("/");
constexpr const char *OP_MOD = DEFINE_BINARY_OP_NAME("%");

constexpr const char *OP_EQ = DEFINE_BINARY_OP_NAME("==");
constexpr const char *OP_NE = DEFINE_BINARY_OP_NAME("!=");

constexpr const char *OP_LT = DEFINE_BINARY_OP_NAME("<");
constexpr const char *OP_GT = DEFINE_BINARY_OP_NAME(">");
constexpr const char *OP_LE = DEFINE_BINARY_OP_NAME("<=");
constexpr const char *OP_GE = DEFINE_BINARY_OP_NAME(">=");

constexpr const char *OP_AND = DEFINE_BINARY_OP_NAME("and");
constexpr const char *OP_OR = DEFINE_BINARY_OP_NAME("or");
constexpr const char *OP_XOR = DEFINE_BINARY_OP_NAME("xor");

constexpr const char *OP_MATCH = DEFINE_BINARY_OP_NAME("=~");
constexpr const char *OP_UNMATCH = DEFINE_BINARY_OP_NAME("!~");

#undef DEFINE_BINARY_OP_NAME

// indexer op
#define DEFINE_INDEX_OP_NAME(OP) "%i_" OP

constexpr const char *INDEX_OP_NAME_PREFIX = DEFINE_INDEX_OP_NAME("");

constexpr const char *OP_GET = DEFINE_INDEX_OP_NAME("[]");
constexpr const char *OP_SET = DEFINE_INDEX_OP_NAME("[]=");

#undef DEFINE_INDEX_OP_NAME

// iterator (for-in)
constexpr const char *OP_ITER = "%iter";
constexpr const char *OP_NEXT = "%next";

constexpr const char *OP_BOOL = "%bool"; // for boolean cast

// to string
constexpr const char *OP_STR = "%str";       // for string cast or command argument
constexpr const char *OP_INTERP = "%interp"; // for interpolation

// num cast
constexpr const char *OP_TO_INT = "toInt";
constexpr const char *OP_TO_FLOAT = "toFloat";

constexpr const char *METHOD_SCRIPT_NAME = "_scriptName";
constexpr const char *METHOD_SCRIPT_DIR = "_scriptDir";

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
constexpr const char *VAL_DEFAULT_IFS = " \t\n";

// =====  system error message prefix  =====

constexpr const char *EXEC_ERROR = "execution error: ";
constexpr const char *REDIR_ERROR = "io redirection failed";
constexpr const char *UNDEF_ENV_ERROR = "undefined environmental variable: ";
constexpr const char *SET_ENV_ERROR = "not set environmental variable: ";

// =====  error message  =====

constexpr const char *CMD_SUB_ERROR = "command substitution failed";
constexpr const char *STRING_LIMIT_ERROR = "reach String size limit";

// =====  generic type name  =====

constexpr const char *TYPE_ARRAY = "Array";
constexpr const char *TYPE_MAP = "Map";
constexpr const char *TYPE_TUPLE = "Tuple";
constexpr const char *TYPE_OPTION = "Option";
constexpr const char *TYPE_FUNC = "Func";

// =====  UDC parameter offset =====

constexpr unsigned int UDC_PARAM_ATTR = 0;  // &&attr
constexpr unsigned int UDC_PARAM_REDIR = 1; // &&redir
constexpr unsigned int UDC_PARAM_ARGV = 2;  // @
constexpr unsigned int UDC_PARAM_N = UDC_PARAM_ARGV + 1;

// =====  termination kind  =====

constexpr unsigned int TERM_ON_EXIT = 1u << 0u;
constexpr unsigned int TERM_ON_ERR = 1u << 1u;
constexpr unsigned int TERM_ON_ASSERT = 1u << 2u;

// =====  for symbol lookup =====
constexpr const char *CMD_SYMBOL_SUFFIX = "%c";
constexpr const char *TYPE_ALIAS_SYMBOL_SUFFIX = "%t";
constexpr const char *METHOD_SYMBOL_SUFFIX = "%f";
constexpr const char *MOD_HOLDER_SYMBOL_SUFFIX = "%m";

constexpr const char *MOD_SYMBOL_PREFIX = "%mod";

constexpr const char *DENIED_REDEFINED_CMD_LIST[] = {
    "eval", "exit", "exec", "call", "command", "_exit",
};

// ===== Object toString =====
constexpr const char *OBJ_TEMP_MOD_PREFIX = "TMD(";
constexpr const char *OBJ_MOD_PREFIX = "module(";

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

inline std::string toMethodFullName(unsigned int recvTypeId, StringRef methodName) {
  std::string name = methodName.toString();
  name += "%";
  name += std::to_string(recvTypeId);
  name += METHOD_SYMBOL_SUFFIX;
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

inline std::string toQualifiedTypeName(StringRef name, unsigned short belongedModId) {
  std::string value = toModTypeName(belongedModId);
  value += ".";
  value += name;
  return value;
}

inline StringRef trimMethodFullNameSuffix(StringRef methodFullName) {
  methodFullName.removeSuffix(strlen(METHOD_SYMBOL_SUFFIX));
  auto pos = methodFullName.lastIndexOf("%");
  return methodFullName.slice(0, pos);
}

inline bool isCmdFullName(StringRef ref) { return ref.endsWith(CMD_SYMBOL_SUFFIX); }

inline bool isTypeAliasFullName(StringRef ref) { return ref.endsWith(TYPE_ALIAS_SYMBOL_SUFFIX); }

inline bool isMethodFullName(StringRef ref) { return ref.endsWith(METHOD_SYMBOL_SUFFIX); }

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
         !isMagicMethodName(ref) && !isMethodFullName(ref);
}

inline bool isQualifiedTypeName(StringRef ref) {
  return ref.startsWith(MOD_SYMBOL_PREFIX) && ref.contains(".");
}

// =====  other constants  =====

constexpr const char *BUILD_ARCH =
#ifdef __x86_64__
    "x86_64"
#elif defined __i386__
    "i386"
#elif defined __arm__
    "arm"
#elif defined __aarch64__
    "aarch64"
#elif defined __EMSCRIPTEN__
    "emscripten"
#else
#error "unsupported architecture"
#endif
    ;

constexpr const char *BUILD_OS =
#ifdef __linux__
    "linux"
#elif defined __APPLE__
    "darwin"
#elif defined __CYGWIN__
    "cygwin"
#elif defined __EMSCRIPTEN__
    "emscripten"
#else
#error "unsupported operating system"
#endif
    ;

enum class RedirOp : unsigned char {
  NOP,             // dummy
  REDIR_IN,        // [n]< word
  REDIR_OUT,       // [n]> word
  CLOBBER_OUT,     // [n]>| word
  APPEND_OUT,      // [n]>> word
  REDIR_OUT_ERR,   // &> word
  CLOBBER_OUT_ERR, // &>| word
  APPEND_OUT_ERR,  // &>> word
  DUP_FD,          // [n]<& N or [n]>& N
  HERE_DOC,        // [n]<< or [n]<<-
  HERE_STR,        // [n]<<< word
};

enum class ForkKind : unsigned char {
  NONE,      // do nothing
  STR,       // capture stdout as string. ex. "$(echo)"
  ARRAY,     // capture stdout as string array. ex. $(echo)
  IN_PIPE,   // capture stdin as pipe. ex. >(echo)
  OUT_PIPE,  // capture stdout as pipe. ex. <(echo)
  COPROC,    // launch as co-process. ex. coproc echo
  JOB,       // launch as background job. ex. echo &
  DISOWN,    // launch as disowned background job. ex. echo &!
  PIPE_FAIL, // check all exit status of pipeline
};

enum class ExpandMeta : unsigned char {
  ANY,
  ZERO_OR_MORE,

  BRACE_OPEN,
  BRACE_CLOSE,
  BRACE_SEP,
  BRACE_TILDE,
  BRACE_SEQ_OPEN,
  BRACE_SEQ_CLOSE,
};

enum class ExpandOp : unsigned char {
  TILDE = 1u << 0u,
  GLOB = 1u << 1u,
  BRACE = 1u << 2u,
};

template <>
struct allow_enum_bitop<ExpandOp> : std::true_type {};

inline const char *toString(ExpandMeta meta) {
  switch (meta) {
  case ExpandMeta::ANY:
    return "?";
  case ExpandMeta::ZERO_OR_MORE:
    return "*";
  case ExpandMeta::BRACE_OPEN:
  case ExpandMeta::BRACE_SEQ_OPEN:
    return "{";
  case ExpandMeta::BRACE_CLOSE:
  case ExpandMeta::BRACE_SEQ_CLOSE:
    return "}";
  case ExpandMeta::BRACE_SEP:
    return ",";
  case ExpandMeta::BRACE_TILDE:
    return "";
  }
  return ""; // normally unreachable
}

struct ConstEntry {
  enum Kind : unsigned char {
    INT,
    BOOL,
    SIG,
    NONE,
  };

  union {
    unsigned int u32;

    struct {
      Kind k;
      unsigned char v;
    } data;
  };

  explicit ConstEntry(unsigned int value) : u32(value) {}

  ConstEntry(Kind k, unsigned char v) : u32(0) {
    this->data.k = k;
    this->data.v = v;
  }
};

// ===== limit ======

constexpr size_t SYS_LIMIT_TYPE_ID = 0xFFFFFF; // UINT24_MAX
constexpr size_t SYS_LIMIT_TUPLE_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_FUNC_PARAM_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_METHOD_PARAM_NUM = UINT8_MAX - 1;
constexpr size_t SYS_LIMIT_MOD_ID = INT16_MAX;
constexpr size_t SYS_LIMIT_GLOBAL_NUM = UINT16_MAX; // FIXME: check global var limit
constexpr size_t SYS_LIMIT_LOCAL_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_PIPE_LEN = 250;
constexpr size_t SYS_LIMIT_EXPANSION_FRAG_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_EXPANSION_RESULTS = 4096;
constexpr size_t SYS_LIMIT_FUNC_LEN = UINT32_MAX;
#ifdef __CYGWIN__
constexpr size_t SYS_LIMIT_NATIVE_RECURSION = 200;
#else
constexpr size_t SYS_LIMIT_NATIVE_RECURSION = 256;
#endif
constexpr size_t SYS_LIMIT_FUNC_DEPTH = 32;
constexpr size_t SYS_LIMIT_UPVAR_NUM = UINT8_MAX;
constexpr size_t SYS_LIMIT_JOB_DESC_LEN = 96;
constexpr size_t SYS_LIMIT_STRING_MAX = INT32_MAX;
constexpr size_t SYS_LIMIT_ARRAY_MAX = INT32_MAX;
constexpr size_t SYS_LIMIT_KEY_BINDING_MAX = UINT8_MAX;
constexpr size_t SYS_LIMIT_CUSTOM_ACTION_MAX = UINT8_MAX;
constexpr size_t SYS_LIMIT_DIRSTACK_SIZE = UINT8_MAX;
constexpr size_t SYS_LIMIT_INPUT_SIZE = INT32_MAX;
constexpr size_t SYS_LIMIT_HIST_SIZE = UINT16_MAX;

// helper macro definition
#if 1

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#else

#define likely(x) (x)
#define unlikely(x) (x)

#endif
} // namespace ydsh

#endif // YDSH_SYMBOL_H
