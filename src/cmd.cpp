/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <termios.h>

#include <cstdlib>
#include <unordered_map>

#include <ydsh/ydsh.h>

#include "cmd_desc.h"
#include "misc/files.h"
#include "misc/num_util.hpp"
#include "vm.h"

extern char **environ; // NOLINT

namespace ydsh {

// builtin command definition
static int builtin_gets(DSState &state, ArrayObject &argvObj);
static int builtin_puts(DSState &state, ArrayObject &argvObj);
static int builtin_cd(DSState &state, ArrayObject &argvObj);
static int builtin_check_env(DSState &state, ArrayObject &argvObj);
static int builtin_complete(DSState &state, ArrayObject &argvObj);
static int builtin_echo(DSState &state, ArrayObject &argvObj);
static int builtin_exit(DSState &state, ArrayObject &argvObj);
static int builtin_false(DSState &state, ArrayObject &argvObj);
static int builtin_hash(DSState &state, ArrayObject &argvObj);
static int builtin_help(DSState &state, ArrayObject &argvObj);
static int builtin_pwd(DSState &state, ArrayObject &argvObj);
static int builtin_read(DSState &state, ArrayObject &argvObj);
static int builtin_setenv(DSState &state, ArrayObject &argvObj);
static int builtin_test(DSState &state, ArrayObject &argvObj);
static int builtin_true(DSState &state, ArrayObject &argvObj);
static int builtin_ulimit(DSState &state, ArrayObject &argvObj);
static int builtin_umask(DSState &state, ArrayObject &argvObj);
static int builtin_unsetenv(DSState &state, ArrayObject &argvObj);

int builtin_fg_bg(DSState &state, ArrayObject &argvObj);
int builtin_jobs(DSState &state, ArrayObject &argvObj);
int builtin_kill(DSState &state, ArrayObject &argvObj);
int builtin_wait(DSState &state, ArrayObject &argvObj);
int builtin_disown(DSState &state, ArrayObject &argvObj);

int builtin_shctl(DSState &state, ArrayObject &argvObj);

static auto initBuiltinMap() {
  return StrRefMap<builtin_command_t>{
      {":", builtin_true},
      {"__gets", builtin_gets},
      {"__puts", builtin_puts},
      {"_exit", builtin_exit},
      {"bg", builtin_fg_bg},
      {"cd", builtin_cd},
      {"checkenv", builtin_check_env},
      {"complete", builtin_complete},
      {"disown", builtin_disown},
      {"echo", builtin_echo},
      {"exit", builtin_exit},
      {"false", builtin_false},
      {"fg", builtin_fg_bg},
      {"hash", builtin_hash},
      {"help", builtin_help},
      {"jobs", builtin_jobs},
      {"kill", builtin_kill},
      {"pwd", builtin_pwd},
      {"read", builtin_read},
      {"setenv", builtin_setenv},
      {"shctl", builtin_shctl},
      {"test", builtin_test},
      {"true", builtin_true},
      {"ulimit", builtin_ulimit},
      {"umask", builtin_umask},
      {"unsetenv", builtin_unsetenv},
      {"wait", builtin_wait},
  };
}

/**
 * return null, if not found builtin command.
 */
builtin_command_t lookupBuiltinCommand(StringRef commandName) {
  static auto builtinMap = initBuiltinMap();

  auto iter = builtinMap.find(commandName);
  if (iter == builtinMap.end()) {
    return nullptr;
  }
  return iter->second;
}

struct StrArrayIter {
  ArrayObject::IterType actual;

  explicit StrArrayIter(ArrayObject::IterType actual) : actual(actual) {}

  auto operator*() const { return this->actual->asStrRef(); }

  bool operator==(const StrArrayIter &o) const { return this->actual == o.actual; }

  bool operator!=(const StrArrayIter &o) const { return !(*this == o); }

  StrArrayIter &operator++() {
    ++this->actual;
    return *this;
  }
};

int GetOptState::operator()(const ArrayObject &obj, const char *optStr) {
  auto iter = StrArrayIter(obj.getValues().begin() + this->index);
  auto end = StrArrayIter(obj.getValues().end());
  int ret = opt::GetOptState::operator()(iter, end, optStr);
  this->index = iter.actual - obj.getValues().begin();
  return ret;
}

/**
 * if not found command, return false.
 */
static bool printUsage(FILE *fp, StringRef prefix, bool isShortHelp = true) {
  bool matched = false;
  unsigned int size = getBuiltinCmdSize();
  auto *cmdList = getBuiltinCmdDescList();
  for (unsigned int i = 0; i < size; i++) {
    const char *cmdName = cmdList[i].name;
    if (StringRef(cmdName).startsWith(prefix)) {
      fprintf(fp, "%s: %s %s\n", cmdName, cmdName, cmdList[i].usage);
      if (!isShortHelp) {
        fprintf(fp, "%s\n", cmdList[i].detail);
      }
      matched = true;
    }
  }
  return matched;
}

int showUsage(const ArrayObject &obj) {
  printUsage(stderr, obj.getValues()[0].asStrRef());
  return 2;
}

int showHelp(const ArrayObject &obj) {
  printUsage(stdout, obj.getValues()[0].asStrRef(), false);
  return 2;
}

int invalidOptionError(const ArrayObject &obj, const GetOptState &s) {
  if (s.foundLongOption) {
    ERROR(obj, "%s: invalid option", toPrintable(s.nextChar).c_str());
  } else {
    ERROR(obj, "-%c: invalid option", s.optOpt);
  }
  return showUsage(obj);
}

static void printAllUsage(FILE *fp) {
  unsigned int size = getBuiltinCmdSize();
  auto *cmdList = getBuiltinCmdDescList();
  for (unsigned int i = 0; i < size; i++) {
    fprintf(fp, "%s %s\n", cmdList[i].name, cmdList[i].usage);
  }
}

static int builtin_help(DSState &, ArrayObject &argvObj) {
  GetOptState optState;
  bool shortHelp = false;
  for (int opt; (opt = optState(argvObj, "sh")) != -1;) {
    switch (opt) {
    case 's':
      shortHelp = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  unsigned int index = optState.index;
  if (index == size) {
    printAllUsage(stdout);
    return 0;
  }
  unsigned int count = 0;
  for (; index < size; index++) {
    auto arg = argvObj.getValues()[index].asStrRef();
    if (printUsage(stdout, arg, shortHelp)) {
      count++;
    }
  }
  if (count == 0) {
    ERROR(argvObj, "no help topics match `%s'.  Try `help help'.",
          argvObj.getValues()[size - 1].asCStr());
    return 1;
  }
  return 0;
}

static int builtin_cd(DSState &state, ArrayObject &argvObj) {
  GetOptState optState;
  bool useLogical = true;
  for (int opt; (opt = optState(argvObj, "PLh")) != -1;) {
    switch (opt) {
    case 'P':
      useLogical = false;
      break;
    case 'L':
      useLogical = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  unsigned int index = optState.index;
  StringRef dest;
  bool useOldpwd = false;
  if (index < argvObj.getValues().size()) {
    dest = argvObj.getValues()[index].asStrRef();
    if (dest == "-") {
      const char *v = getenv(ENV_OLDPWD);
      if (v == nullptr) {
        ERROR(argvObj, "OLDPWD not set");
        return 1;
      }
      dest = v;
      useOldpwd = true;
    }
  } else {
    const char *v = getenv(ENV_HOME);
    if (v == nullptr) {
      ERROR(argvObj, "HOME not set");
      return 1;
    }
    dest = v;
  }

  if (useOldpwd) {
    printf("%s\n", toPrintable(dest).c_str());
  }

  if (!changeWorkingDir(state.logicalWorkingDir, dest, useLogical)) {
    PERROR(argvObj, "%s", toPrintable(dest).c_str());
    return 1;
  }
  return 0;
}

static int builtin_check_env(DSState &, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }
  unsigned int index = optState.index;
  const unsigned int size = argvObj.getValues().size();
  if (index == size) {
    return showUsage(argvObj);
  }
  for (; index < size; index++) {
    auto ref = argvObj.getValues()[index].asStrRef();
    if (ref.hasNullChar()) {
      return 1;
    }
    const char *env = getenv(ref.data());
    if (env == nullptr || strlen(env) == 0) {
      return 1;
    }
  }
  return 0;
}

static int builtin_echo(DSState &, ArrayObject &argvObj) {
  bool newline = true;
  bool interpEscape = false;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "neE")) != -1;) {
    switch (opt) {
    case 'n':
      newline = false;
      break;
    case 'e':
      interpEscape = true;
      break;
    case 'E':
      interpEscape = false;
      break;
    default:
      goto END;
    }
  }

END:
  // print argument
  if (optState.index > 1 && argvObj.getValues()[optState.index - 1].asStrRef() == "--") {
    optState.index--;
  }

  unsigned int index = optState.index;
  const unsigned int argc = argvObj.getValues().size();
  bool firstArg = true;
  for (; index < argc; index++) {
    if (firstArg) {
      firstArg = false;
    } else {
      fputc(' ', stdout);
    }
    if (!interpEscape) {
      auto ref = argvObj.getValues()[index].asStrRef();
      fwrite(ref.data(), sizeof(char), ref.size(), stdout);
      continue;
    }
    auto arg = argvObj.getValues()[index].asStrRef();
    const char *end = arg.end();
    for (const char *iter = arg.begin(); iter != end;) {
      if (*iter == '\\') {
        auto ret = parseEscapeSeq(iter, end, true);
        switch (ret.kind) {
        case EscapeSeqResult::OK_CODE: {
          char buf[5];
          unsigned int size = UnicodeUtil::codePointToUtf8(ret.codePoint, buf);
          fwrite(buf, sizeof(char), size, stdout);
          iter += ret.consumedSize;
          continue;
        }
        case EscapeSeqResult::OK_BYTE: {
          auto b = static_cast<unsigned int>(ret.codePoint);
          char buf[1];
          buf[0] = static_cast<unsigned char>(b);
          fwrite(buf, sizeof(char), 1, stdout);
          iter += ret.consumedSize;
          continue;
        }
        case EscapeSeqResult::RANGE:
          iter += ret.consumedSize; // skip
          continue;
        case EscapeSeqResult::UNKNOWN:
          if (*(iter + 1) == 'c') {
            return 0; // stop printing
          }
          break;
        default:
          break;
        }
      }
      char ch = *(iter++);
      fputc(ch, stdout);
    }
  }

  if (newline) {
    fputc('\n', stdout);
  }
  return 0;
}

static int parseExitStatus(const DSState &state, const ArrayObject &argvObj, unsigned int index) {
  int64_t ret = state.getGlobal(BuiltinVarOffset::EXIT_STATUS).asInt();
  if (index < argvObj.size() && index > 0) {
    auto value = argvObj.getValues()[index].asStrRef();
    auto pair = convertToDecimal<int64_t>(value.begin(), value.end());
    if (pair.second) {
      ret = pair.first;
    }
  }
  return maskExitStatus(ret);
}

static int builtin_exit(DSState &state, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      goto END;
    }
  }

END:
  int ret = parseExitStatus(state, argvObj, optState.index);
  if (argvObj.getValues()[0].asStrRef() == "_exit") {
    exit(ret);
  } else {
    if (hasFlag(state.runtimeOption, RuntimeOption::HUP_EXIT)) {
      state.jobTable.send(SIGHUP);
    }

    std::string str("terminated by exit ");
    str += std::to_string(ret);
    raiseError(state, TYPE::ShellExit_, std::move(str), ret);
  }
  return ret;
}

static int builtin_true(DSState &, ArrayObject &) { return 0; }

static int builtin_false(DSState &, ArrayObject &) { return 1; }

/**
 * for stdin redirection test
 */
static int builtin_gets(DSState &, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }

  char buf[256];
  ssize_t readSize = 0;
  while ((readSize = read(STDIN_FILENO, buf, std::size(buf))) > 0) {
    ssize_t r = write(STDOUT_FILENO, buf, readSize);
    (void)r;
  }
  return 0;
}

/**
 * for stdout/stderr redirection test
 */
static int builtin_puts(DSState &, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "1:2:h")) != -1;) {
    switch (opt) {
    case '1':
      fwrite(optState.optArg.data(), sizeof(char), optState.optArg.size(), stdout);
      fputc('\n', stdout);
      fflush(stdout);
      break;
    case '2':
      fwrite(optState.optArg.data(), sizeof(char), optState.optArg.size(), stderr);
      fputc('\n', stderr);
      fflush(stderr);
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }
  return 0;
}

static int builtin_pwd(DSState &state, ArrayObject &argvObj) {
  bool useLogical = true;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "LPh")) != -1;) {
    switch (opt) {
    case 'L':
      useLogical = true;
      break;
    case 'P':
      useLogical = false;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  auto workdir = getWorkingDir(state.logicalWorkingDir, useLogical);
  if (!workdir) {
    PERROR(argvObj, ".");
    return 1;
  }
  printf("%s\n", workdir.get());
  return 0;
}

#define EACH_STR_COMP_OP(OP)                                                                       \
  OP(STR_EQ, "==", ==)                                                                             \
  OP(STR_EQ2, "=", ==)                                                                             \
  OP(STR_NE, "!=", !=)                                                                             \
  OP(STR_LT, "<", <)                                                                               \
  OP(STR_GT, ">", >)

#define EACH_INT_COMP_OP(OP)                                                                       \
  OP(EQ, "-eq", ==)                                                                                \
  OP(NE, "-ne", !=)                                                                                \
  OP(LT, "-lt", <)                                                                                 \
  OP(GT, "-gt", >)                                                                                 \
  OP(LE, "-le", <=)                                                                                \
  OP(GE, "-ge", >=)

#define EACH_FILE_COMP_OP(OP)                                                                      \
  OP(NT, "-nt", %)                                                                                 \
  OP(OT, "-ot", %)                                                                                 \
  OP(EF, "-ef", %)

enum class BinaryOp : unsigned int {
  INVALID,
#define GEN_ENUM(E, S, O) E,
  EACH_STR_COMP_OP(GEN_ENUM) EACH_INT_COMP_OP(GEN_ENUM) EACH_FILE_COMP_OP(GEN_ENUM)
#undef GEN_ENUM
};

static BinaryOp resolveBinaryOp(StringRef opStr) {
  const struct {
    const char *k;
    BinaryOp op;
  } table[] = {
#define GEN_ENTRY(E, S, O) {S, BinaryOp::E},
      EACH_INT_COMP_OP(GEN_ENTRY) EACH_STR_COMP_OP(GEN_ENTRY) EACH_FILE_COMP_OP(GEN_ENTRY)
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
    EACH_STR_COMP_OP(GEN_CASE)
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
    EACH_INT_COMP_OP(GEN_CASE)
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

static int parseFD(StringRef value) {
  if (value.startsWith("/dev/fd/")) {
    value.removePrefix(strlen("/dev/fd/"));
  }
  auto ret = convertToDecimal<int32_t>(value.begin(), value.end());
  if (!ret.second || ret.first < 0) {
    return -1;
  }
  return ret.first;
}

static int testFile(char op, const char *value) {
  bool result = false;
  switch (op) {
  case 'a':
  case 'e':
    result = access(value, F_OK) == 0; // check if file exists
    break;
  case 'b':
    result = S_ISBLK(getStMode(value)); // check if file is block device
    break;
  case 'c':
    result = S_ISCHR(getStMode(value)); // check if file is character device
    break;
  case 'd':
    result = S_ISDIR(getStMode(value)); // check if file is directory
    break;
  case 'f':
    result = S_ISREG(getStMode(value)); // check if file is regular file.
    break;
  case 'g':
    result = S_IS_PERM_(getStMode(value), S_ISUID); // check if file has set-uid-bit
    break;
  case 'h':
  case 'L': {
    mode_t mode = 0;
    struct stat st; // NOLINT
    if (lstat(value, &st) == 0) {
      mode = st.st_mode;
    }
    result = S_ISLNK(mode); // check if file is symbolic-link
    break;
  }
  case 'k':
    result = S_IS_PERM_(getStMode(value), S_ISVTX); // check if file has sticky bit
    break;
  case 'p':
    result = S_ISFIFO(getStMode(value)); // check if file is a named pipe
    break;
  case 'r':
    result = access(value, R_OK) == 0; // check if file is readable
    break;
  case 's': {
    struct stat st;                                    // NOLINT
    result = stat(value, &st) == 0 && st.st_size != 0; // check if file is not empty
    break;
  }
  case 'S':
    result = S_ISSOCK(getStMode(value)); // check file is a socket
    break;
  case 't': {
    int fd = parseFD(value);
    result = fd > -1 && isatty(fd) != 0; //  check if FD is a terminal
    break;
  }
  case 'u':
    result = S_IS_PERM_(getStMode(value), S_ISUID); // check file has set-user-id bit
    break;
  case 'w':
    result = access(value, W_OK) == 0; // check if file is writable
    break;
  case 'x':
    result = access(value, X_OK) == 0; // check if file is executable
    break;
  case 'O': {
    struct stat st;                                           // NOLINT
    result = stat(value, &st) == 0 && st.st_uid == geteuid(); // check if file is effectively owned
    break;
  }
  case 'G': {
    struct stat st; // NOLINT
    result = stat(value, &st) == 0 &&
             st.st_gid == getegid(); // check if file is effectively owned by group
    break;
  }
  default:
    return 2;
  }
  return result ? 0 : 1;
}

static int builtin_test(DSState &, ArrayObject &argvObj) {
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
    if (op.size() != 2 || op[0] != '-') {
      ERROR(argvObj, "%s: invalid unary operator", toPrintable(op).c_str());
      return 2;
    }

    const char opKind = op[1]; // ignore -
    if (opKind == 'z') {       // check if string is empty
      result = ref.empty();
    } else if (opKind == 'n') { // check if string not empty
      result = !ref.empty();
    } else {
      if (ref.hasNullChar()) {
        ERROR(argvObj, "file path contains null characters");
        return 2;
      }
      int r = testFile(opKind, ref.data());
      if (r == 2) {
        ERROR(argvObj, "%s: invalid unary operator", toPrintable(op).c_str());
      }
      return r;
    }
    break;
  }
  case 3: { // binary op
    auto left = argvObj.getValues()[1].asStrRef();
    auto op = argvObj.getValues()[2].asStrRef();
    auto opKind = resolveBinaryOp(op);
    auto right = argvObj.getValues()[3].asStrRef();

    switch (opKind) {
#define GEN_CASE(E, S, O) case BinaryOp::E:
      EACH_STR_COMP_OP(GEN_CASE) {
        result = compareStr(left, opKind, right);
        break;
      }
      EACH_INT_COMP_OP(GEN_CASE) {
        auto pair = convertToDecimal<int64_t>(left.begin(), left.end());
        int64_t n1 = pair.first;
        if (!pair.second) {
          ERROR(argvObj, "%s: must be integer", toPrintable(left).c_str());
          return 2;
        }

        pair = convertToDecimal<int64_t>(right.begin(), right.end());
        int64_t n2 = pair.first;
        if (!pair.second) {
          ERROR(argvObj, "%s: must be integer", toPrintable(right).c_str());
          return 2;
        }

        result = compareInt(n1, opKind, n2);
        break;
      }
      EACH_FILE_COMP_OP(GEN_CASE) {
        result = compareFile(left, opKind, right);
        break;
      }
#undef GEN_CASE
    case BinaryOp::INVALID:
      ERROR(argvObj, "%s: invalid binary operator", toPrintable(op).c_str()); // FIXME:
      return 2;
    }
    break;
  }
  default: {
    ERROR(argvObj, "too many arguments");
    return 2;
  }
  }
  return result ? 0 : 1;
}

static int xfgetc(int fd, int timeout) {
  signed char ch;
  do {
    errno = 0;

    if (timeout > -2) {
      struct pollfd pollfd[1];
      pollfd[0].fd = fd;
      pollfd[0].events = POLLIN;
      if (poll(pollfd, 1, timeout) != 1) {
        return EOF;
      }
    }

    if (read(fd, &ch, 1) <= 0) {
      ch = EOF;
    }
  } while (static_cast<int>(ch) == EOF && errno == EAGAIN);
  return ch;
}

static int builtin_read(DSState &state, ArrayObject &argvObj) { // FIXME: timeout, UTF-8
  StringRef prompt;
  StringRef ifs;
  bool backslash = true;
  bool noecho = false;
  int fd = STDIN_FILENO;
  int timeout = -1;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, ":rp:f:su:t:h")) != -1;) {
    switch (opt) {
    case 'p':
      prompt = optState.optArg;
      break;
    case 'f':
      ifs = optState.optArg;
      break;
    case 'r':
      backslash = false;
      break;
    case 's':
      noecho = true;
      break;
    case 'u': {
      StringRef value = optState.optArg;
      fd = parseFD(value);
      if (fd < 0) {
        ERROR(argvObj, "%s: invalid file descriptor", toPrintable(value).c_str());
        return 1;
      }
      break;
    }
    case 't': {
      auto ret = convertToDecimal<int64_t>(optState.optArg.begin(), optState.optArg.end());
      int64_t t = ret.first;
      if (ret.second) {
        if (t > -1 && t <= INT32_MAX) {
          t *= 1000;
          if (t > -1 && t <= INT32_MAX) {
            timeout = static_cast<int>(t);
            break;
          }
        }
      }
      ERROR(argvObj, "%s: invalid timeout specification", toPrintable(optState.optArg).c_str());
      return 1;
    }
    case 'h':
      return showHelp(argvObj);
    case ':':
      ERROR(argvObj, "-%c: option require argument", optState.optOpt);
      return 2;
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  const unsigned int argc = argvObj.getValues().size();
  unsigned int index = optState.index;
  const bool isTTY = isatty(fd) != 0;

  // check ifs
  if (ifs.data() == nullptr) {
    ifs = state.getGlobal(BuiltinVarOffset::IFS).asStrRef();
  }

  // clear old variable before read
  state.setGlobal(BuiltinVarOffset::REPLY, DSValue::createStr());          // clear REPLY
  typeAs<MapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR)).clear(); // clear reply

  const unsigned int varSize = argc - index; // if zero, store line to REPLY
  const auto varIndex = varSize == 0 ? BuiltinVarOffset::REPLY : BuiltinVarOffset::REPLY_VAR;
  std::string strBuf;

  // show prompt
  if (isTTY) {
    fwrite(prompt.data(), sizeof(char), prompt.size(), stderr);
    fflush(stderr);
  }

  // change tty state
  struct termios tty {};
  struct termios oldtty {};
  if (noecho && isTTY) {
    tcgetattr(fd, &tty);
    oldtty = tty;
    tty.c_lflag &= ~(ECHO | ECHOK | ECHONL);
    tcsetattr(fd, TCSANOW, &tty);
  }

  // read line
  if (!isTTY) {
    timeout = -2;
  }
  unsigned int skipCount = 1;
  int ch;
  for (bool prevIsBackslash = false; (ch = xfgetc(fd, timeout)) != EOF;
       prevIsBackslash = backslash && ch == '\\' && !prevIsBackslash) {
    if (ch == '\n') {
      if (prevIsBackslash) {
        continue;
      }
      break;
    }
    if (ch == '\\' && !prevIsBackslash && backslash) {
      continue;
    }

    bool fieldSep = matchFieldSep(ifs, ch) && !prevIsBackslash;
    if (fieldSep && skipCount > 0) {
      if (isSpace(ch)) {
        continue;
      }
      if (--skipCount == 1) {
        continue;
      }
    }
    skipCount = 0;
    if (fieldSep && index < argc - 1) {
      auto &obj = typeAs<MapObject>(state.getGlobal(varIndex));
      auto varObj = argvObj.getValues()[index];
      auto valueObj = DSValue::createStr(std::move(strBuf));
      obj.set(std::move(varObj), std::move(valueObj));
      strBuf = "";
      index++;
      skipCount = isSpace(ch) ? 2 : 1;
      continue;
    }
    strBuf += static_cast<char>(ch);
  }

  // remove last spaces
  if (!strBuf.empty()) {
    if (hasSpace(ifs)) { // check if field separator has spaces
      while (!strBuf.empty() && isSpace(strBuf.back())) {
        strBuf.pop_back();
      }
    }
  }

  if (varSize == 0) {
    state.setGlobal(varIndex, DSValue::createStr(std::move(strBuf)));
    strBuf = "";
  }

  // set rest variable
  for (; index < argc; index++) {
    auto &obj = typeAs<MapObject>(state.getGlobal(varIndex));
    auto varObj = argvObj.getValues()[index];
    auto valueObj = DSValue::createStr(std::move(strBuf));
    obj.set(std::move(varObj), std::move(valueObj));
    strBuf = "";
  }

  // restore tty setting
  if (noecho && isTTY) {
    tcsetattr(fd, TCSANOW, &oldtty);
  }

  // report error
  int ret = ch == EOF ? 1 : 0;
  if (ret != 0 && errno != 0) {
    PERROR(argvObj, "%d", fd);
  }
  return ret;
}

static int builtin_hash(DSState &state, ArrayObject &argvObj) {
  bool remove = false;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "rh")) != -1;) {
    switch (opt) {
    case 'r':
      remove = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  unsigned int index = optState.index;
  if (index < size) {
    for (; index < size; index++) {
      auto ref = argvObj.getValues()[index].asStrRef();
      const char *name = ref.data();
      bool hasNul = ref.hasNullChar();
      if (remove) {
        state.pathCache.removePath(hasNul ? nullptr : name);
      } else {
        if (hasNul || state.pathCache.searchPath(name) == nullptr) {
          ERROR(argvObj, "%s: not found", toPrintable(ref).c_str());
          return 1;
        }
      }
    }
  } else {
    if (remove) { // remove all cache
      state.pathCache.clear();
    } else { // show all cache
      const auto cend = state.pathCache.end();
      if (state.pathCache.begin() == cend) {
        fputs("hash: file path cache is empty\n", stdout);
        return 0;
      }
      for (auto &entry : state.pathCache) {
        printf("%s=%s\n", entry.first, entry.second.c_str());
      }
    }
  }
  return 0;
}

static StrRefMap<CodeCompOp> initCompActions() {
  return {
      {"file", CodeCompOp::FILE},       {"dir", CodeCompOp::DIR},
      {"module", CodeCompOp::MODULE},   {"exec", CodeCompOp::EXEC},
      {"tilde", CodeCompOp::TILDE},     {"command", CodeCompOp::COMMAND},
      {"cmd", CodeCompOp::COMMAND},     {"external", CodeCompOp::EXTERNAL},
      {"builtin", CodeCompOp::BUILTIN}, {"udc", CodeCompOp::UDC},
      {"dyna", CodeCompOp::DYNA_UDC},   {"variable", CodeCompOp::VAR},
      {"var", CodeCompOp::VAR},         {"env", CodeCompOp::ENV},
      {"signal", CodeCompOp::SIGNAL},   {"user", CodeCompOp::USER},
      {"group", CodeCompOp::GROUP},     {"stmt_kw", CodeCompOp::STMT_KW},
      {"expr_kw", CodeCompOp::EXPR_KW}, {"type", CodeCompOp::TYPE},
  };
}

static int builtin_complete(DSState &state, ArrayObject &argvObj) {
  static auto actionMap = initCompActions();

  CodeCompOp compOp{};
  bool show = true;
  bool insertSpace = false;
  StringRef moduleDesc;
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, ":A:m:qsh")) != -1;) {
    switch (opt) {
    case 'A': {
      auto iter = actionMap.find(optState.optArg);
      if (iter == actionMap.end()) {
        ERROR(argvObj, "%s: invalid action", toPrintable(optState.optArg).c_str());
        return showUsage(argvObj);
      }
      setFlag(compOp, iter->second);
      break;
    }
    case 'm':
      moduleDesc = optState.optArg;
      break;
    case 'q':
      show = false;
      break;
    case 's':
      insertSpace = true;
      break;
    case 'h':
      return showHelp(argvObj);
    case ':':
      ERROR(argvObj, "-%c: option requires argument", optState.optOpt);
      return 1;
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  StringRef line;
  if (optState.index < argvObj.size()) {
    line = argvObj.getValues()[optState.index].asStrRef();
  }

  if (doCodeCompletion(state, moduleDesc, line, insertSpace, compOp) < 0) {
    if (errno == EINVAL) {
      ERROR(argvObj, "%s: unrecognized module descriptor", toPrintable(moduleDesc).c_str());
    }
    return 1;
  }
  if (show) {
    auto &ret = typeAs<ArrayObject>(state.getGlobal(BuiltinVarOffset::COMPREPLY));
    for (const auto &e : ret.getValues()) {
      fputs(e.asCStr(), stdout);
      fputc('\n', stdout);
    }
  }
  return 0;
}

static int builtin_setenv(DSState &, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  unsigned int index = optState.index;
  if (index == size) {
    for (unsigned int i = 0; environ[i] != nullptr; i++) {
      const char *e = environ[i];
      fprintf(stdout, "%s\n", e);
    }
    return 0;
  }

  for (; index < size; index++) {
    auto kv = argvObj.getValues()[index].asStrRef();
    auto pos = kv.hasNullChar() ? StringRef::npos : kv.find("=");
    errno = EINVAL;
    if (pos != StringRef::npos && pos != 0) {
      auto name = kv.substr(0, pos).toString();
      auto value = kv.substr(pos + 1);
      if (setenv(name.c_str(), value.data(), 1) == 0) {
        continue;
      }
    }
    PERROR(argvObj, "%s", toPrintable(kv).c_str());
    return 1;
  }
  return 0;
}

static int builtin_unsetenv(DSState &, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  for (unsigned int index = optState.index; index < size; index++) {
    auto envName = argvObj.getValues()[index].asStrRef();
    if (unsetenv(envName.hasNullChar() ? "" : envName.data()) != 0) {
      PERROR(argvObj, "%s", toPrintable(envName).c_str());
      return 1;
    }
  }
  return 0;
}

// for ulimit command

static constexpr flag8_t RLIM_HARD = 1u << 0;
static constexpr flag8_t RLIM_SOFT = 1u << 1;

struct ulimitOp {
  char op;
  char resource;
  char shift;
  const char *name;

  void print(flag8_set_t limOpt, unsigned int maxNameLen) const {
    rlimit limit{};
    getrlimit(this->resource, &limit);

    if (maxNameLen) {
      printf("-%c: %s  ", this->op, this->name);
      for (unsigned int len = strlen(this->name); len < maxNameLen; len++) {
        printf(" ");
      }
    }

    auto value = hasFlag(limOpt, RLIM_HARD) ? limit.rlim_max : limit.rlim_cur;
    if (value == RLIM_INFINITY) {
      printf("unlimited\n");
    } else {
      value >>= this->shift;
      printf("%llu\n", static_cast<unsigned long long>(value));
    }
    fflush(stdout);
  }
};

struct UlimitOptEntry {
  enum Kind : unsigned char {
    UNUSED,
    NUM,
    SOFT,
    HARD,
    UNLIMITED,
  };

  Kind kind{UNUSED};
  rlim_t value{0};

  explicit operator bool() const { return this->kind != UNUSED; }

  rlim_t getValue(const rlimit &limit) const {
    switch (this->kind) {
    case SOFT:
      return limit.rlim_cur;
    case HARD:
      return limit.rlim_max;
    case UNLIMITED:
      return RLIM_INFINITY;
    default:
      return this->value;
    }
  }
};

static constexpr ulimitOp ulimitOps[] = {
#define DEF(O, R, S, N, D) {O[0], R, S, N},
#include "ulimit-def.in"
#undef DEF
};

static unsigned int computeMaxNameLen() {
  unsigned int max = 0;
  for (auto &e : ulimitOps) {
    unsigned int len = strlen(e.name);
    if (len > max) {
      max = len;
    }
  }
  return max;
}

static bool parseUlimitOpt(StringRef ref, unsigned int index, UlimitOptEntry &entry) {
  using underlying_t = std::conditional<
      sizeof(rlim_t) == sizeof(uint64_t), uint64_t,
      std::conditional<sizeof(rlim_t) == sizeof(uint32_t), uint32_t, void>::type>::type;

  if (ref.hasNullChar()) {
    return false;
  }
  const char *str = ref.data();

  if (strcasecmp(str, "soft") == 0) {
    entry.kind = UlimitOptEntry::SOFT;
    return true;
  } else if (strcasecmp(str, "hard") == 0) {
    entry.kind = UlimitOptEntry::HARD;
    return true;
  } else if (strcasecmp(str, "unlimited") == 0) {
    entry.kind = UlimitOptEntry::UNLIMITED;
    return true;
  }

  auto pair = convertToDecimal<underlying_t>(str);
  if (!pair.second) {
    return false;
  }

  entry.value = pair.first;
  entry.value <<= ulimitOps[index].shift;
  entry.kind = UlimitOptEntry::NUM;
  return true;
}

struct UlimitOptEntryTable {
  uint64_t printSet{0};
  std::array<UlimitOptEntry, std::size(ulimitOps)> entries;
  unsigned int count{0};

  int tryToUpdate(GetOptState &optState, ArrayObject &argvObj, int opt) {
    DSValue arg;
    if (optState.index < argvObj.getValues().size() &&
        *argvObj.getValues()[optState.index].asCStr() != '-') {
      arg = argvObj.getValues()[optState.index++];
    }
    if (!this->update(opt, arg)) {
      ERROR(argvObj, "%s: invalid number", arg.asCStr());
      return 1;
    }
    return 0;
  }

private:
  bool update(int ch, const DSValue &value) {
    this->count++;
    // search entry
    for (unsigned int index = 0; index < std::size(ulimitOps); index++) {
      if (ulimitOps[index].op == ch) {
        auto &entry = this->entries[index];
        if (value) {
          if (!parseUlimitOpt(value.asStrRef(), index, entry)) {
            return false;
          }
        } else {
          setFlag(this->printSet, static_cast<uint64_t>(1) << index);
        }
      }
    }
    return true;
  }
};

static int builtin_ulimit(DSState &, ArrayObject &argvObj) {
  flag8_set_t limOpt = 0;
  bool showAll = false;

  GetOptState optState;
  const char *optStr = "HSah"
#define DEF(O, R, S, N, D) O
#include "ulimit-def.in"
#undef DEF
      ;

  UlimitOptEntryTable table;

  // parse option
  for (int opt; (opt = optState(argvObj, optStr)) != -1;) {
    switch (opt) {
    case 'H':
      setFlag(limOpt, RLIM_HARD);
      break;
    case 'S':
      setFlag(limOpt, RLIM_SOFT);
      break;
    case 'a':
      showAll = true;
      break;
    case 'h':
      return showHelp(argvObj);
    case '?':
      return invalidOptionError(argvObj, optState);
    default:
      int ret = table.tryToUpdate(optState, argvObj, opt);
      if (ret) {
        return ret;
      }
      break;
    }
  }

  // parse remain
  if (table.count == 0) {
    int ret = table.tryToUpdate(optState, argvObj, 'f');
    if (ret) {
      return ret;
    }
  }

  if (limOpt == 0) {
    setFlag(limOpt, RLIM_SOFT);
  }

  if (showAll) {
    unsigned int maxDescLen = computeMaxNameLen();
    for (auto &e : ulimitOps) {
      e.print(limOpt, maxDescLen);
    }
    return 0;
  }

  // print or set limit
  unsigned int maxNameLen = 0;
  if (table.printSet > 0 && (table.printSet & (table.printSet - 1)) != 0) {
    maxNameLen = computeMaxNameLen();
  }
  for (unsigned int index = 0; index < static_cast<unsigned int>(table.entries.size()); index++) {
    if (table.entries[index]) {
      const auto &op = ulimitOps[index];
      rlimit limit{};
      getrlimit(op.resource, &limit);
      rlim_t value = table.entries[index].getValue(limit);
      if (hasFlag(limOpt, RLIM_SOFT)) {
        limit.rlim_cur = value;
      }
      if (hasFlag(limOpt, RLIM_HARD)) {
        limit.rlim_max = value;
      }
      if (setrlimit(op.resource, &limit) < 0) {
        PERROR(argvObj, "%s: cannot change limit", op.name);
        return 1;
      }
    }
    if (hasFlag(table.printSet, static_cast<uint64_t>(1) << index)) {
      ulimitOps[index].print(limOpt, maxNameLen);
    }
  }
  return 0;
}

enum class PrintMaskOp : unsigned int {
  ONLY_PRINT = 1 << 0,
  REUSE = 1 << 1,
  SYMBOLIC = 1 << 2,
};

template <>
struct allow_enum_bitop<PrintMaskOp> : std::true_type {};

static void printMask(mode_t mask, PrintMaskOp op) {
  if (hasFlag(op, PrintMaskOp::SYMBOLIC)) {
    char buf[std::size("u=rwx,g=rwx,o=rwx")];
    char *ptr = buf;

    /**
     *  u   g   o
     * rwx|rwx|rwx
     * 111 111 111
     *
     */
    for (auto &user : {'u', 'g', 'o'}) {
      if (ptr != buf) {
        *(ptr++) = ',';
      }
      *(ptr++) = user;
      *(ptr++) = '=';
      for (auto &perm : {'r', 'w', 'x'}) {
        if (!(mask & 0400)) {
          *(ptr++) = perm;
        }
        mask <<= 1;
      }
    }
    *ptr = '\0';
    fprintf(stdout, "%s%s\n", hasFlag(op, PrintMaskOp::REUSE) ? "umask -S " : "", buf);
  } else if (hasFlag(op, PrintMaskOp::ONLY_PRINT)) {
    fprintf(stdout, "%s%04o\n", hasFlag(op, PrintMaskOp::REUSE) ? "umask " : "", mask);
  }
}

/**
 * MODE = [ugoa]* [+-=] [rwx]*
 *
 * @param value
 * @param mode
 * @return
 * if failed, return false
 */
static bool parseMode(const char *&value, mode_t &mode) {
  // [ugoa]*
  mode_t user = 0;
  for (bool next = true; next;) {
    char ch = *(value++);
    switch (ch) {
    case 'u':
      user |= 0700;
      break;
    case 'g':
      user |= 0070;
      break;
    case 'o':
      user |= 0007;
      break;
    case 'a':
      user |= 0777;
      break;
    default: // may be [-+=]
      next = false;
      --value;
      break;
    }
  }
  if (user == 0) {
    user = 0777;
  }

  // [-+=]
  char op = *(value++);
  if (op != '-' && op != '+' && op != '=') {
    return false;
  }

  // [rwx]*
  mode_t newMode = 0;
  while (*value && *value != ',') {
    char ch = *(value++);
    switch (ch) {
    case 'r':
      newMode |= 0444 & user;
      break;
    case 'w':
      newMode |= 0222 & user;
      break;
    case 'x':
      newMode |= 0111 & user;
      break;
    default:
      return false;
    }
  }

  // apply op
  if (op == '+') {
    unsetFlag(mode, newMode);
  } else if (op == '-') {
    setFlag(mode, newMode);
  } else {
    setFlag(mode, user);
    unsetFlag(mode, newMode);
  }
  return true;
}

struct SymbolicParseResult {
  bool success;
  char invalid;
  mode_t mode;
};

/**
 * MODES = MODE (, MODE)*
 *
 * @param ref
 * @param mode
 * @return
 */
static SymbolicParseResult parseSymbolicMode(StringRef ref, mode_t mode) {
  SymbolicParseResult ret{
      .success = true,
      .invalid = 0,
      .mode = mode,
  };

  if (ref.hasNullChar()) {
    ret.success = false;
    ret.invalid = '\0';
    return ret;
  }
  const char *value = ref.data();
  if (!parseMode(value, ret.mode)) {
    ret.success = false;
    ret.invalid = *(--value);
    return ret;
  }
  while (*value) {
    if (*(value++) == ',' && parseMode(value, ret.mode)) {
      continue;
    }
    ret.success = false;
    ret.invalid = *(--value);
    break;
  }
  return ret;
}

static int builtin_umask(DSState &, ArrayObject &argvObj) {
  auto op = PrintMaskOp::ONLY_PRINT;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "pSh")) != -1;) {
    switch (opt) {
    case 'p':
      setFlag(op, PrintMaskOp::REUSE);
      break;
    case 'S':
      setFlag(op, PrintMaskOp::SYMBOLIC);
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  auto mask = umask(0);
  umask(mask);

  if (optState.index < argvObj.getValues().size()) {
    unsetFlag(op, PrintMaskOp::ONLY_PRINT | PrintMaskOp::REUSE);
    auto value = argvObj.getValues()[optState.index].asStrRef();
    if (!value.empty() && isDecimal(*value.data())) {
      auto pair = convertToNum<int32_t>(value.begin(), value.end(), 8);
      int num = pair.first;
      if (!pair.second || num < 0 || num > 0777) {
        ERROR(argvObj, "%s: octal number out of range (0000~0777)", toPrintable(value).c_str());
        return 1;
      }
      mask = num;
    } else {
      auto ret = parseSymbolicMode(value, mask);
      mask = ret.mode;
      if (!ret.success) {
        int ch = static_cast<unsigned char>(ret.invalid);
        if (isascii(ch) && ch != 0) {
          ERROR(argvObj, "%c: invalid symbolic operator", ch);
        } else {
          ERROR(argvObj, "0x%02x: invalid symbolic operator", ch);
        }
        return 1;
      }
    }
    umask(mask);
  }
  printMask(mask, op);
  return 0;
}

} // namespace ydsh
