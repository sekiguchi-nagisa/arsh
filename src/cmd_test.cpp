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

static int evalBinaryOp(const ARState &st, const ArrayObject &argvObj, StringRef left, BinaryOp op,
                        StringRef right) {
  bool result = false;
  switch (op) {
#define GEN_CASE(E, S, O) case BinaryOp::E:
    EACH_BINARY_STR_COMP_OP(GEN_CASE) {
      result = compareStr(left, op, right);
      break;
    }
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

      result = compareInt(n1, op, n2);
      break;
    }
    EACH_BINARY_FILE_COMP_OP(GEN_CASE) {
      result = compareFile(left, op, right);
      break;
    }
#undef GEN_CASE
  case BinaryOp::INVALID:
    break; // unreachable
  }
  return result ? 0 : 1;
}

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

int parseFD(StringRef value) {
  if (value.startsWith("/dev/fd/")) {
    value.removePrefix(strlen("/dev/fd/"));
  }
  auto ret = convertToDecimal<int32_t>(value.begin(), value.end());
  if (!ret || ret.value < 0) {
    return -1;
  }
  return ret.value;
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

static int evalUnaryOp(const ARState &st, const ArrayObject &argvObj, const StringRef op,
                       const StringRef arg) {
  if (op.size() != 2 || op[0] != '-') {
    ERROR(st, argvObj, "%s: invalid unary operator", toPrintable(op).c_str());
    return 2;
  }

  const char opKind = op[1]; // ignore -
  if (opKind == 'z') {       // check if string is empty
    return arg.empty() ? 0 : 1;
  } else if (opKind == 'n') { // check if string not empty
    return !arg.empty() ? 0 : 1;
  } else {
    if (arg.hasNullChar()) {
      ERROR(st, argvObj, "file path contains null characters");
      return 2;
    }
    const auto fileOp = resolveFileOp(opKind);
    if (fileOp == UnaryFileOp::INVALID) {
      ERROR(st, argvObj, "%s: invalid unary operator", toPrintable(op).c_str());
      return 2;
    }
    return testFile(fileOp, arg.data()) ? 0 : 1;
  }
}

int builtin_test(ARState &st, ArrayObject &argvObj) {
  bool result = false;
  unsigned int argc = argvObj.getValues().size();
  const unsigned int argSize = argc - 1;

  switch (argSize) {
  case 0: {
    result = false;
    break;
  }
  case 1: {
    result = !argvObj.getValues()[1].asStrRef().empty(); // check if string is not empty
    break;
  }
  case 2: { // unary op
    auto op = argvObj.getValues()[1].asStrRef();
    auto ref = argvObj.getValues()[2].asStrRef();
    return evalUnaryOp(st, argvObj, op, ref);
  }
  case 3: { // binary op
    auto left = argvObj.getValues()[1].asStrRef();
    auto op = argvObj.getValues()[2].asStrRef();
    auto opKind = resolveBinaryOp(op);
    auto right = argvObj.getValues()[3].asStrRef();

    if (opKind == BinaryOp::INVALID) {
      ERROR(st, argvObj, "%s: invalid binary operator", toPrintable(op).c_str()); // FIXME:
      return 2;
    }
    return evalBinaryOp(st, argvObj, left, opKind, right);
  }
  default: {
    ERROR(st, argvObj, "too many arguments");
    return 2;
  }
  }
  return result ? 0 : 1;
}

} // namespace arsh