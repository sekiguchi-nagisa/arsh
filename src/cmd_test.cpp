/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "cmd.h"
#include "core.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"

namespace arsh {

static int eval1ArgExpr(const StringRef arg) {
  return !arg.empty() ? 0 : 1; // check if string is not empty
}

// for unary op
#define EACH_UNARY_FILE_OP(OP)                                                                     \
  OP(IS_EXIST, 'a') /* also accept '-e' */                                                         \
  OP(IS_BLOCK, 'b')                                                                                \
  OP(IS_CHAR, 'c')                                                                                 \
  OP(IS_DIR, 'd')                                                                                  \
  OP(IS_FILE, 'f')                                                                                 \
  OP(HAS_GID_BIT, 'g')                                                                             \
  OP(IS_SYMLINK, 'h') /* also accept '-L' */                                                       \
  OP(HAS_STIKY_BIT, 'k')                                                                           \
  OP(IS_NAMED_PIPE, 'p')                                                                           \
  OP(IS_READABLE, 'r')                                                                             \
  OP(IS_SIZE_NON_ZERO, 's')                                                                        \
  OP(IS_TTY, 't')                                                                                  \
  OP(HAS_UID_BIT, 'u')                                                                             \
  OP(IS_WRITABLE, 'w')                                                                             \
  OP(IS_EXECUTABLE, 'x')                                                                           \
  OP(IS_OWNED_BY_EGID, 'G')                                                                        \
  OP(IS_OWNED_BY_EUID, 'O')                                                                        \
  OP(IS_SOCKET, 'S')

enum class UnaryFileOp : unsigned char {
  INVALID,
#define GEN_ENUM(OP, S) OP,
  EACH_UNARY_FILE_OP(GEN_ENUM)
#undef GEN_ENUM
};

static UnaryFileOp resolveFileOp(const char ch) {
  const struct {
    char s;
    UnaryFileOp op;
  } table[] = {{'e', UnaryFileOp::IS_EXIST},   // alias for '-a'
               {'L', UnaryFileOp::IS_SYMLINK}, // alias for '-h'
#define GEN_TABLE(E, S) {S, UnaryFileOp::E},
               EACH_UNARY_FILE_OP(GEN_TABLE)
#undef GEN_TABLE
  };
  for (auto &e : table) {
    if (ch == e.s) {
      return e.op;
    }
  }
  return UnaryFileOp::INVALID;
}

static bool testFile(const UnaryFileOp op, const char *value) {
  switch (op) {
  case UnaryFileOp::IS_EXIST:
    return access(value, F_OK) == 0; // check if file exists
  case UnaryFileOp::IS_BLOCK:
    return S_ISBLK(getStMode(value)); // check if file is block device
  case UnaryFileOp::IS_CHAR:
    return S_ISCHR(getStMode(value)); // check if file is character device
  case UnaryFileOp::IS_DIR:
    return S_ISDIR(getStMode(value)); // check if file is directory
  case UnaryFileOp::IS_FILE:
    return S_ISREG(getStMode(value)); // check if file is regular file.
  case UnaryFileOp::HAS_GID_BIT:
    return S_IS_PERM_(getStMode(value), S_ISGID); // check if file has set-gid-bit
  case UnaryFileOp::IS_SYMLINK: {
    mode_t mode = 0;
    struct stat st; // NOLINT
    if (lstat(value, &st) == 0) {
      mode = st.st_mode;
    }
    return S_ISLNK(mode); // check if file is symbolic-link
  }
  case UnaryFileOp::HAS_STIKY_BIT:
    return S_IS_PERM_(getStMode(value), S_ISVTX); // check if file has sticky bit
  case UnaryFileOp::IS_NAMED_PIPE:
    return S_ISFIFO(getStMode(value)); // check if file is a named pipe
  case UnaryFileOp::IS_READABLE:
    return access(value, R_OK) == 0; // check if file is readable
  case UnaryFileOp::IS_SIZE_NON_ZERO: {
    struct stat st;                                  // NOLINT
    return stat(value, &st) == 0 && st.st_size != 0; // check if file is not empty
  }
  case UnaryFileOp::IS_TTY: {
    int fd = parseFD(value);
    return fd > -1 && isatty(fd) != 0; //  check if FD is a terminal
  }
  case UnaryFileOp::HAS_UID_BIT:
    return S_IS_PERM_(getStMode(value), S_ISUID); // check file has set-user-id bit
  case UnaryFileOp::IS_WRITABLE:
    return access(value, W_OK) == 0; // check if file is writable
  case UnaryFileOp::IS_EXECUTABLE:
    return access(value, X_OK) == 0; // check if file is executable
  case UnaryFileOp::IS_OWNED_BY_EGID: {
    struct stat st; // NOLINT
    return stat(value, &st) == 0 &&
           st.st_gid == getegid(); // check if file is effectively owned by group
  }
  case UnaryFileOp::IS_OWNED_BY_EUID: {
    struct stat st; // NOLINT
    return stat(value, &st) == 0 &&
           st.st_uid == geteuid(); // check if file is effectively owned by user
  }
  case UnaryFileOp::IS_SOCKET:
    return S_ISSOCK(getStMode(value)); // check file is a socket
  case UnaryFileOp::INVALID:
    break; // unreachable
  }
  return false;
}

static int negateStatus(int status) {
  if (status == 0) {
    return 1;
  }
  if (status == 1) {
    return 0;
  }
  return status;
}

static int needCloseParen(const ARState &st, const ArrayObject &argvObj, const StringRef actual) {
  const char *suffix = actual.empty() ? "" : ", but actual: ";
  ERROR(st, argvObj, "expect: `)'%s%s", suffix, toPrintable(actual).c_str());
  return 2;
}

static int eval2ArgsExpr(const ARState &st, const ArrayObject &argvObj, const StringRef op,
                         const StringRef arg) {
  if (op == "!") {
    return negateStatus(eval1ArgExpr(arg));
  }
  if (op.size() == 2 && op[0] == '-') {
    const char opKind = op[1]; // ignore -
    if (opKind == 'z') {       // check if string is empty
      return arg.empty() ? 0 : 1;
    }
    if (opKind == 'n') { // check if string not empty
      return !arg.empty() ? 0 : 1;
    }
    if (const auto fileOp = resolveFileOp(opKind); fileOp != UnaryFileOp::INVALID) {
      if (arg.hasNullChar()) {
        ERROR(st, argvObj, "file path contains null characters");
        return 2;
      }
      return testFile(fileOp, arg.data()) ? 0 : 1;
    }
  }
  if (op == "(") {
    if (arg == ")") {
      ERROR(st, argvObj, "expect arguments within `( )'");
      return 2;
    }
    return needCloseParen(st, argvObj, "");
  }
  ERROR(st, argvObj, "%s: invalid unary operator", toPrintable(op).c_str());
  return 2;
}

static bool isUnaryOp(const StringRef op) {
  if (op.size() == 2 && op[0] == '-') {
    const char o = op[1];
    return o == 'z' || o == 'n' || resolveFileOp(o) != UnaryFileOp::INVALID;
  }
  return false;
}

// for binary op

#define EACH_BINARY_STR_COMP_OP(OP)                                                                \
  OP(STR_EQ, "==", ==) /* also accept '=' */                                                       \
  OP(STR_NE, "!=", !=)                                                                             \
  OP(STR_LT, "<", <)                                                                               \
  OP(STR_GT, ">", >)

#define EACH_BINARY_INT_COMP_OP(OP)                                                                \
  OP(EQ, "-eq", ==)                                                                                \
  OP(NE, "-ne", !=)                                                                                \
  OP(LT, "-lt", <)                                                                                 \
  OP(GT, "-gt", >)                                                                                 \
  OP(LE, "-le", <=)                                                                                \
  OP(GE, "-ge", >=)

#define EACH_BINARY_FILE_COMP_OP(OP)                                                               \
  OP(NT, "-nt", %)                                                                                 \
  OP(OT, "-ot", %)                                                                                 \
  OP(EF, "-ef", %)

enum class BinaryOp : unsigned char {
  INVALID,
#define GEN_ENUM(E, S, O) E,
  EACH_BINARY_STR_COMP_OP(GEN_ENUM) EACH_BINARY_INT_COMP_OP(GEN_ENUM)
      EACH_BINARY_FILE_COMP_OP(GEN_ENUM)
#undef GEN_ENUM
};

static BinaryOp resolveBinaryOp(StringRef opStr) {
  const struct {
    const char *k;
    BinaryOp op;
  } table[] = {{"=", BinaryOp::STR_EQ}, // alias for '=='
#define GEN_ENTRY(E, S, O) {S, BinaryOp::E},
               EACH_BINARY_INT_COMP_OP(GEN_ENTRY) EACH_BINARY_STR_COMP_OP(GEN_ENTRY)
                   EACH_BINARY_FILE_COMP_OP(GEN_ENTRY)
#undef GEN_ENTRY
  };
  for (auto &e : table) {
    if (opStr == e.k) {
      return e.op;
    }
  }
  return BinaryOp::INVALID;
}

static bool compareStr(StringRef left, BinaryOp op, StringRef right) {
  switch (op) {
#define GEN_CASE(E, S, O)                                                                          \
  case BinaryOp::E:                                                                                \
    return left O right;
    EACH_BINARY_STR_COMP_OP(GEN_CASE)
#undef GEN_CASE
  default:
    break;
  }
  return false;
}

static bool compareInt(int64_t x, BinaryOp op, int64_t y) {
  switch (op) {
#define GEN_CASE(E, S, O)                                                                          \
  case BinaryOp::E:                                                                                \
    return x O y;
    EACH_BINARY_INT_COMP_OP(GEN_CASE)
#undef GEN_CASE
  default:
    break;
  }
  return false;
}

static bool operator<(const timespec &left, const timespec &right) {
  if (left.tv_sec == right.tv_sec) {
    return left.tv_nsec < right.tv_nsec;
  }
  return left.tv_sec < right.tv_sec;
}

static bool compareFile(StringRef x, BinaryOp op, StringRef y) {
  if (x.hasNullChar() || y.hasNullChar()) {
    return false;
  }

  struct stat st1; // NOLINT
  struct stat st2; // NOLINT

  if (stat(x.data(), &st1) != 0) {
    return false;
  }
  if (stat(y.data(), &st2) != 0) {
    return false;
  }

  switch (op) {
  case BinaryOp::NT:
#ifdef __APPLE__
    return st2.st_mtimespec < st1.st_mtimespec;
#else
    return st2.st_mtim < st1.st_mtim;
#endif
  case BinaryOp::OT:
#ifdef __APPLE__
    return st1.st_mtimespec < st2.st_mtimespec;
#else
    return st1.st_mtim < st2.st_mtim;
#endif
  case BinaryOp::EF:
    return st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino;
  default:
    return false;
  }
}

static int eval3ArgsExpr(const ARState &st, const ArrayObject &argvObj, const StringRef left,
                         const StringRef opStr, const StringRef right) {
  const auto op = resolveBinaryOp(opStr);
  switch (op) {
#define GEN_CASE(E, S, O) case BinaryOp::E:
    EACH_BINARY_STR_COMP_OP(GEN_CASE) { return compareStr(left, op, right) ? 0 : 1; }
    EACH_BINARY_INT_COMP_OP(GEN_CASE) {
      auto pair = convertToDecimal<int64_t>(left.begin(), left.end());
      const int64_t n1 = pair.value;
      if (!pair) {
        ERROR(st, argvObj, "%s: must be integer", toPrintable(left).c_str());
        return 2;
      }

      pair = convertToDecimal<int64_t>(right.begin(), right.end());
      const int64_t n2 = pair.value;
      if (!pair) {
        ERROR(st, argvObj, "%s: must be integer", toPrintable(right).c_str());
        return 2;
      }

      return compareInt(n1, op, n2) ? 0 : 1;
    }
    EACH_BINARY_FILE_COMP_OP(GEN_CASE) { return compareFile(left, op, right) ? 0 : 1; }
#undef GEN_CASE
  case BinaryOp::INVALID:
    break;
  }

  if (opStr == "-a") {
    const int s1 = eval1ArgExpr(left);
    const int s2 = eval1ArgExpr(right);
    return s1 == 0 && s2 == 0 ? 0 : 1;
  }
  if (opStr == "-o") {
    const int s1 = eval1ArgExpr(left);
    const int s2 = eval1ArgExpr(right);
    return s1 == 0 || s2 == 0 ? 0 : 1;
  }

  if (left == "!") {
    return negateStatus(eval2ArgsExpr(st, argvObj, opStr, right));
  }
  if (left == "(") {
    if (right == ")") {
      return eval1ArgExpr(opStr);
    }
    return needCloseParen(st, argvObj, right);
  }
  ERROR(st, argvObj, "%s: invalid binary operator", toPrintable(opStr).c_str());
  return 2;
}

#define TRY(E)                                                                                     \
  ({                                                                                               \
    int s = (E);                                                                                   \
    if (s != 0 && s != 1) {                                                                        \
      return s;                                                                                    \
    }                                                                                              \
    s;                                                                                             \
  })

class Evaluator {
private:
  ARState &st;
  const ArrayObject &argvObj;
  unsigned int offset{1};
  unsigned int depth{0}; // check deep recursion

  static constexpr unsigned int RECURSION_LIMIT = 16;

public:
  Evaluator(ARState &st, const ArrayObject &argvObj) : st(st), argvObj(argvObj) {}

  int eval() {
    const unsigned int size = this->argvObj.size();
    int s = TRY(this->evalOrExpr(size, 0));
    if (this->offset < size) {
      ERROR(this->st, this->argvObj, "too many arguments");
      return 2;
    }
    return s;
  }

private:
  StringRef get(unsigned int index) const { return this->argvObj.getValues()[index].asStrRef(); }

  StringRef consume() { return this->get(this->offset++); }

  StringRef current() const { return this->get(this->offset); }

  int evalOrExpr(unsigned int limitOffset, unsigned int level) {
    int s1 = 1;
    unsigned int count = 0;
    do {
      if (count++ > 0) {
        this->consume();
      }
      const int s2 = TRY(this->evalAndExpr(limitOffset, level));
      s1 = s1 == 0 || s2 == 0 ? 0 : 1;
    } while (this->offset < limitOffset && this->current() == "-o");
    return s1;
  }

  int evalAndExpr(unsigned int limitOffset, unsigned int level) {
    int s1 = 0;
    unsigned int count = 0;
    do {
      if (count++ > 0) {
        this->consume();
      }
      const int s2 = TRY(this->evalBase(limitOffset, level));
      s1 = s1 == 0 && s2 == 0 ? 0 : 1;
    } while (this->offset < limitOffset && this->current() == "-a");
    return s1;
  }

  int evalBase(unsigned int limitOffset, unsigned int level);
};

int Evaluator::evalBase(unsigned int limitOffset, const unsigned int level) {
  const auto cleanup = finally([&] { this->depth--; });
  if (++this->depth == RECURSION_LIMIT) {
    ERROR(this->st, this->argvObj, "too many nesting");
    return 3;
  }

  if (level) {
    goto OTHER;
  }

INIT:
  assert(this->offset <= limitOffset);
  switch (limitOffset - this->offset) {
  case 0:
    return 1;
  case 1: {
    const auto arg = this->consume();
    return eval1ArgExpr(arg);
  }
  case 2: {
    const auto arg1 = this->consume();
    const auto arg2 = this->consume();
    return eval2ArgsExpr(this->st, this->argvObj, arg1, arg2);
  }
  case 3: {
    const auto arg1 = this->consume();
    const auto arg2 = this->consume();
    const auto arg3 = this->consume();
    return eval3ArgsExpr(this->st, this->argvObj, arg1, arg2, arg3);
  }
  case 4: {
    const unsigned int oldOffset = this->offset;
    const auto arg1 = this->consume();
    const auto arg2 = this->consume();
    const auto arg3 = this->consume();
    const auto arg4 = this->consume();
    if (arg1 == "!") {
      return negateStatus(eval3ArgsExpr(this->st, this->argvObj, arg2, arg3, arg4));
    }
    if (arg1 == "(") {
      if (arg4 == ")") {
        return eval2ArgsExpr(this->st, this->argvObj, arg2, arg3);
      }
      return needCloseParen(this->st, this->argvObj, arg4);
    }
    this->offset = oldOffset; // restore offset
    break;
  }
  default:
    break;
  }

OTHER:
  if (this->current() == "!") {
    this->consume();
    goto INIT;
    /*return this->evalBase(limitOffset, level);*/
  }
  if (this->current() == "(") {
    this->consume();
    int s = TRY(this->evalOrExpr(limitOffset, level + 1));
    StringRef close;
    if (this->offset < limitOffset) {
      close = this->consume();
    }
    if (close != ")") {
      return needCloseParen(this->st, this->argvObj, close);
    }
    return s;
  }

  assert(this->offset <= limitOffset);
  const unsigned int remain = limitOffset - this->offset;
  if (remain >= 3) {
    if (resolveBinaryOp(this->get(this->offset + 1)) != BinaryOp::INVALID) {
      limitOffset = this->offset + 3;
      goto INIT;
      /*return this->evalBase(this->offset + 3, level);*/
    }
  }
  if (remain >= 2) {
    if (isUnaryOp(this->get(this->offset))) {
      limitOffset = this->offset + 2;
      goto INIT;
      /*return this->evalBase(this->offset + 2, level);*/
    }
  }
  if (remain >= 1) {
    limitOffset = this->offset + 1;
    goto INIT;
    /*return this->evalBase(this->offset + 1, level);*/
  }
  return 1;
}

int builtin_test(ARState &st, ArrayObject &argvObj) {
  Evaluator evaluator(st, argvObj);
  return evaluator.eval();
}

} // namespace arsh