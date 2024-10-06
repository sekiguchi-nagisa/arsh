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
#include <sys/resource.h>
#include <sys/stat.h>

#include <cstdlib>
#include <unordered_map>

#include <arsh/arsh.h>

#include "candidates.h"
#include "cmd_desc.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "vm.h"

extern char **environ; // NOLINT

namespace arsh {

// builtin command definition
static int builtin_gets(ARState &state, ArrayObject &argvObj);
static int builtin_puts(ARState &state, ArrayObject &argvObj);
static int builtin_check_env(ARState &state, ArrayObject &argvObj);
static int builtin_complete(ARState &state, ArrayObject &argvObj);
static int builtin_eval(ARState &state, ArrayObject &argvObj);
static int builtin_exit(ARState &state, ArrayObject &argvObj);
static int builtin_false(ARState &state, ArrayObject &argvObj);
static int builtin_getenv(ARState &state, ArrayObject &argvObj);
static int builtin_hash(ARState &state, ArrayObject &argvObj);
static int builtin_help(ARState &state, ArrayObject &argvObj);
static int builtin_setenv(ARState &state, ArrayObject &argvObj);
static int builtin_true(ARState &state, ArrayObject &argvObj);
static int builtin_ulimit(ARState &state, ArrayObject &argvObj);
static int builtin_umask(ARState &state, ArrayObject &argvObj);
static int builtin_unsetenv(ARState &state, ArrayObject &argvObj);

int builtin_fg_bg(ARState &state, ArrayObject &argvObj);
int builtin_jobs(ARState &state, ArrayObject &argvObj);
int builtin_kill(ARState &state, ArrayObject &argvObj);
int builtin_wait(ARState &state, ArrayObject &argvObj);
int builtin_disown(ARState &state, ArrayObject &argvObj);

int builtin_read(ARState &state, ArrayObject &argvObj);

int builtin_shctl(ARState &state, ArrayObject &argvObj);

int builtin_pwd(ARState &state, ArrayObject &argvObj);
int builtin_cd(ARState &state, ArrayObject &argvObj);
int builtin_dirs(ARState &state, ArrayObject &argvObj);
int builtin_pushd_popd(ARState &state, ArrayObject &argvObj);

int builtin_echo(ARState &state, ArrayObject &argvObj);
int builtin_printf(ARState &state, ArrayObject &argvObj);

int builtin_test(ARState &state, ArrayObject &argvObj);

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
      {"dirs", builtin_dirs},
      {"disown", builtin_disown},
      {"echo", builtin_echo},
      {"eval", builtin_eval},
      {"exit", builtin_exit},
      {"false", builtin_false},
      {"fg", builtin_fg_bg},
      {"getenv", builtin_getenv},
      {"hash", builtin_hash},
      {"help", builtin_help},
      {"jobs", builtin_jobs},
      {"kill", builtin_kill},
      {"popd", builtin_pushd_popd},
      {"printf", builtin_printf},
      {"pushd", builtin_pushd_popd},
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
  static const auto builtinMap = initBuiltinMap();

  const auto iter = builtinMap.find(commandName);
  if (iter == builtinMap.end()) {
    return nullptr;
  }
  return iter->second;
}

int GetOptState::operator()(const ArrayObject &obj) {
  auto iter = StrArrayIter(obj.getValues().begin() + this->index);
  const auto end = StrArrayIter(obj.getValues().end());
  const int ret = opt::GetOptState::operator()(iter, end);
  this->index = iter.actual - obj.getValues().begin();
  return ret;
}

/**
 * if not found command, return false.
 */
static bool printUsage(FILE *fp, StringRef prefix, bool isShortHelp = true) {
  bool matched = false;
  const unsigned int size = getBuiltinCmdSize();
  auto *cmdList = getBuiltinCmdDescList();
  for (unsigned int i = 0; i < size; i++) {
    if (const char *cmdName = cmdList[i].name; StringRef(cmdName).startsWith(prefix)) {
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

int invalidOptionError(const ARState &st, const ArrayObject &obj, const GetOptState &s) {
  char buf[] = {'-', static_cast<char>(s.optOpt)};
  StringRef opt(buf, std::size(buf));
  if (s.foundLongOption) {
    opt = s.nextChar;
  }
  ERROR(st, obj, "%s: invalid option", toPrintable(opt).c_str());
  return showUsage(obj);
}

int parseFD(StringRef value) {
  if (value.startsWith("/dev/fd/")) {
    value.removePrefix(strlen("/dev/fd/"));
  }
  const auto ret = convertToNum10<int32_t>(value.begin(), value.end());
  if (!ret || ret.value < 0) {
    return -1;
  }
  return ret.value;
}

static void printAllUsage(FILE *fp) {
  const unsigned int size = getBuiltinCmdSize();
  auto *cmdList = getBuiltinCmdDescList();
  for (unsigned int i = 0; i < size; i++) {
    fprintf(fp, "%s %s\n", cmdList[i].name, cmdList[i].usage);
  }
}

static int builtin_help(ARState &st, ArrayObject &argvObj) {
  GetOptState optState("sh");
  bool shortHelp = false;
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 's':
      shortHelp = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(st, argvObj, optState);
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
    const auto arg = argvObj.getValues()[index].asStrRef();
    if (printUsage(stdout, arg, shortHelp)) {
      count++;
    }
  }
  if (count == 0) {
    ERROR(st, argvObj, "no help topics match `%s'.  Try `help help'.",
          argvObj.getValues()[size - 1].asCStr());
    return 1;
  }
  return 0;
}

static int builtin_check_env(ARState &st, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(st, argvObj, optState);
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

static int builtin_eval(ARState &st, ArrayObject &argvObj) {
  ERROR(st, argvObj, "currently does not support eval command");
  return 1;
}

static int parseExitStatus(const ARState &state, const ArrayObject &argvObj, unsigned int index) {
  int64_t ret = state.getGlobal(BuiltinVarOffset::EXIT_STATUS).asInt();
  if (index < argvObj.size() && index > 0) {
    const auto value = argvObj.getValues()[index].asStrRef();
    const auto pair = convertToNum10<int64_t>(value.begin(), value.end());
    if (pair) {
      ret = pair.value;
    }
  }
  return maskExitStatus(ret);
}

static int builtin_exit(ARState &state, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      goto END;
    }
  }

END:
  const int ret = parseExitStatus(state, argvObj, optState.index);
  if (argvObj.getValues()[0].asStrRef() == "_exit") {
    exit(ret);
  }
  raiseShellExit(state, ret);
  return ret;
}

static int builtin_true(ARState &, ArrayObject &) { return 0; }

static int builtin_false(ARState &, ArrayObject &) { return 1; }

/**
 * for stdin redirection test
 */
static int builtin_gets(ARState &st, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(st, argvObj, optState);
    }
  }

  char buf[256];
  ssize_t readSize = 0;
  while ((readSize = read(STDIN_FILENO, buf, std::size(buf))) > 0) {
    const ssize_t r = write(STDOUT_FILENO, buf, readSize);
    (void)r;
  }
  return 0;
}

static int writeLine(StringRef ref, FILE *fp, bool flush) {
  if (fwrite(ref.data(), sizeof(char), ref.size(), fp) != ref.size()) {
    return errno;
  }
  if (fputc('\n', fp) == EOF) {
    return errno;
  }
  if (flush) {
    if (fflush(fp) == EOF) {
      return errno;
    }
  }
  return 0;
}

/**
 * for stdout/stderr redirection test
 */
static int builtin_puts(ARState &st, ArrayObject &argvObj) {
  int errNum = 0;
  GetOptState optState("1:2:h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case '1':
      errNum = writeLine(optState.optArg, stdout, true);
      if (errNum != 0) {
        goto END;
      }
      break;
    case '2':
      errNum = writeLine(optState.optArg, stderr, true);
      if (errNum != 0) {
        goto END;
      }
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(st, argvObj, optState);
    }
  }

END:
  CHECK_STDOUT_ERROR(st, argvObj, errNum);
  return 0;
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      errNum = errno;                                                                              \
      goto END;                                                                                    \
    }                                                                                              \
  } while (false)

static int builtin_hash(ARState &state, ArrayObject &argvObj) {
  bool remove = false;

  GetOptState optState("rh");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'r':
      remove = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  unsigned int index = optState.index;
  if (index < size) {
    for (; index < size; index++) {
      auto ref = argvObj.getValues()[index].asStrRef();
      const char *name = ref.data();
      const bool hasNul = ref.hasNullChar();
      if (remove) {
        state.pathCache.removePath(hasNul ? nullptr : name);
      } else {
        if (hasNul || state.pathCache.searchPath(name) == nullptr) {
          ERROR(state, argvObj, "%s: not found", toPrintable(ref).c_str());
          return 1;
        }
      }
    }
  } else {
    if (remove) { // remove all cache
      state.pathCache.clear();
    } else { // show all cache
      int errNum = 0;
      if (state.pathCache.begin() == state.pathCache.end()) {
        TRY(printf("hash: file path cache is empty\n") > -1);
      }
      for (auto &entry : state.pathCache) {
        TRY(printf("%s=%s\n", entry.first, entry.second.c_str()) > -1);
      }

    END:
      CHECK_STDOUT_ERROR(state, argvObj, errNum);
    }
  }
  return 0;
}

static StrRefMap<CodeCompOp> initCompActions() {
  return {
      {"file", CodeCompOp::FILE},
      {"dir", CodeCompOp::DIR},
      {"module", CodeCompOp::MODULE},
      {"exec", CodeCompOp::EXEC},
      {"tilde", CodeCompOp::TILDE},
      {"command", CodeCompOp::COMMAND},
      {"cmd", CodeCompOp::COMMAND},
      {"external", CodeCompOp::EXTERNAL},
      {"builtin", CodeCompOp::BUILTIN},
      {"udc", CodeCompOp::UDC},
      {"dyna", CodeCompOp::DYNA_UDC},
      {"variable", CodeCompOp::VAR},
      {"var", CodeCompOp::VAR},
      {"env", CodeCompOp::ENV},
      {"valid_env", CodeCompOp::VALID_ENV},
      {"signal", CodeCompOp::SIGNAL},
      {"user", CodeCompOp::USER},
      {"group", CodeCompOp::GROUP},
      {"stmt_kw", CodeCompOp::STMT_KW},
      {"expr_kw", CodeCompOp::EXPR_KW},
      {"type", CodeCompOp::TYPE},
  };
}

static int builtin_complete(ARState &state, ArrayObject &argvObj) {
  static const auto actionMap = initCompActions();

  DoCodeCompletionOption option{};
  bool show = true;
  bool insertSpace = false;
  StringRef moduleDesc;
  GetOptState optState(":A:m:dqsh");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'A': {
      auto iter = actionMap.find(optState.optArg);
      if (iter == actionMap.end()) {
        ERROR(state, argvObj, "%s: invalid action", toPrintable(optState.optArg).c_str());
        return showUsage(argvObj);
      }
      setFlag(option.op, iter->second);
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
    case 'd':
      option.putDesc = true;
      break;
    case 'h':
      return showHelp(argvObj);
    case ':':
      ERROR(state, argvObj, "-%c: option requires argument", optState.optOpt);
      return 1;
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  StringRef line;
  if (optState.index < argvObj.size()) {
    line = argvObj.getValues()[optState.index].asStrRef();
  }

  if (doCodeCompletion(state, moduleDesc, option, line) < 0) {
    if (errno == EINVAL) {
      ERROR(state, argvObj, "%s: unrecognized module descriptor", toPrintable(moduleDesc).c_str());
    }
    return 1;
  }
  if (show) {
    int errNum = 0;
    std::string out;
    const CandidatesWrapper wrapper(
        toObjPtr<ArrayObject>(state.getGlobal(BuiltinVarOffset::COMPREPLY)));
    const unsigned int size = wrapper.size();
    for (unsigned int i = 0; i < size; i++) {
      out.clear();
      out += wrapper.getCandidateAt(i);
      if (insertSpace && wrapper.getAttrAt(i).needSpace) {
        out += ' ';
      }
      errNum = writeLine(out, stdout, false);
      if (errNum != 0) {
        break;
      }
    }
    CHECK_STDOUT_ERROR(state, argvObj, errNum);
  }
  return 0;
}

static int builtin_getenv(ARState &state, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(state, argvObj, optState);
    }
  }

  const unsigned int index = optState.index;
  if (index == argvObj.size()) {
    return showUsage(argvObj);
  }

  state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr());
  const auto envName = argvObj.getValues()[index].asStrRef();
  if (envName.hasNullChar()) {
    ERROR(state, argvObj, "contains null characters: %s", toPrintable(envName).c_str());
    return 1;
  }
  if (const char *env = getenv(envName.data())) {
    std::string value = env;
    assert(value.size() <= SYS_LIMIT_STRING_MAX);
    state.setGlobal(BuiltinVarOffset::REPLY, Value::createStr(std::move(value)));
    return 0;
  } else {
    return 1;
  }
}

static int builtin_setenv(ARState &st, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(st, argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  unsigned int index = optState.index;
  if (index == size) {
    int errNum = 0;
    for (unsigned int i = 0; environ[i] != nullptr; i++) {
      const char *e = environ[i];
      errNum = writeLine(e, stdout, false);
      if (errNum != 0) {
        break;
      }
    }
    CHECK_STDOUT_ERROR(st, argvObj, errNum);
    return 0;
  }

  for (; index < size; index++) {
    auto kv = argvObj.getValues()[index].asStrRef();
    const auto pos = kv.hasNullChar() ? StringRef::npos : kv.find("=");
    errno = EINVAL;
    if (pos != StringRef::npos && pos != 0) {
      auto name = kv.substr(0, pos).toString();
      auto value = kv.substr(pos + 1);
      if (setEnv(st.pathCache, name.c_str(), value.data())) {
        continue;
      }
    }
    PERROR(st, argvObj, "%s", toPrintable(kv).c_str());
    return 1;
  }
  return 0;
}

static int builtin_unsetenv(ARState &st, ArrayObject &argvObj) {
  GetOptState optState("h");
  for (int opt; (opt = optState(argvObj)) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(st, argvObj, optState);
    }
  }

  const unsigned int size = argvObj.size();
  for (unsigned int index = optState.index; index < size; index++) {
    auto envName = argvObj.getValues()[index].asStrRef();
    if (!unsetEnv(st.pathCache, envName.hasNullChar() ? "" : envName.data())) {
      PERROR(st, argvObj, "%s", toPrintable(envName).c_str());
      return 1;
    }
  }
  return 0;
}

// for ulimit command

#undef TRY
#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

static constexpr flag8_t RLIM_HARD = 1u << 0;
static constexpr flag8_t RLIM_SOFT = 1u << 1;

struct ulimitOp {
  char op;
  char resource;
  char shift;
  const char *name;

  bool print(flag8_set_t limOpt, unsigned int maxNameLen) const {
    rlimit limit{};
    getrlimit(this->resource, &limit);

    errno = 0;
    if (maxNameLen) {
      TRY(printf("-%c: %-*s", this->op, maxNameLen + 2, this->name) > -1);
    }

    auto value = hasFlag(limOpt, RLIM_HARD) ? limit.rlim_max : limit.rlim_cur;
    if (value == RLIM_INFINITY) {
      TRY(printf("unlimited\n") > -1);
    } else {
      value >>= this->shift;
      TRY(printf("%llu\n", static_cast<unsigned long long>(value)) > -1);
    }
    TRY(fflush(stdout) != EOF);
    return true;
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
    if (const unsigned int len = strlen(e.name); len > max) {
      max = len;
    }
  }
  return max;
}

static bool parseUlimitOpt(StringRef ref, unsigned int index, UlimitOptEntry &entry) {
  using underlying_t =
      std::conditional_t<sizeof(rlim_t) == sizeof(uint64_t), uint64_t,
                         std::conditional_t<sizeof(rlim_t) == sizeof(uint32_t), uint32_t, void>>;

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

  const auto pair = convertToNum10<underlying_t>(str);
  if (!pair) {
    return false;
  }

  entry.value = pair.value;
  entry.value <<= ulimitOps[index].shift;
  entry.kind = UlimitOptEntry::NUM;
  return true;
}

struct UlimitOptEntryTable {
  uint64_t printSet{0};
  std::array<UlimitOptEntry, std::size(ulimitOps)> entries;
  unsigned int count{0};

  int tryToUpdate(const ARState &st, GetOptState &optState, ArrayObject &argvObj, int opt) {
    Value arg;
    if (optState.index < argvObj.getValues().size() &&
        *argvObj.getValues()[optState.index].asCStr() != '-') {
      arg = argvObj.getValues()[optState.index++];
    }
    if (!this->update(opt, arg)) {
      ERROR(st, argvObj, "%s: invalid number", arg.asCStr());
      return 1;
    }
    return 0;
  }

private:
  bool update(int ch, const Value &value) {
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

static int builtin_ulimit(ARState &st, ArrayObject &argvObj) {
  flag8_set_t limOpt = 0;
  bool showAll = false;

  const char *optStr = "HSah"
#define DEF(O, R, S, N, D) O
#include "ulimit-def.in"
#undef DEF
      ;
  GetOptState optState(optStr);

  UlimitOptEntryTable table;

  // parse option
  for (int opt; (opt = optState(argvObj)) != -1;) {
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
      return invalidOptionError(st, argvObj, optState);
    default:
      if (const int ret = table.tryToUpdate(st, optState, argvObj, opt)) {
        return ret;
      }
      break;
    }
  }

  // parse remain
  if (table.count == 0) {
    if (const int ret = table.tryToUpdate(st, optState, argvObj, 'f')) {
      return ret;
    }
  }

  if (limOpt == 0) {
    setFlag(limOpt, RLIM_SOFT);
  }

  if (showAll) {
    const unsigned int maxDescLen = computeMaxNameLen();
    int errNum = 0;
    for (auto &e : ulimitOps) {
      if (!e.print(limOpt, maxDescLen)) {
        errNum = errno;
        break;
      }
    }
    CHECK_STDOUT_ERROR(st, argvObj, errNum);
    return 0;
  }

  // print or set limit
  unsigned int maxNameLen = 0;
  if (table.printSet > 0 && (table.printSet & (table.printSet - 1)) != 0) {
    maxNameLen = computeMaxNameLen();
  }
  int errNum = 0;
  for (unsigned int index = 0; index < static_cast<unsigned int>(table.entries.size()); index++) {
    if (table.entries[index]) {
      const auto &op = ulimitOps[index];
      rlimit limit{};
      getrlimit(op.resource, &limit);
      const rlim_t value = table.entries[index].getValue(limit);
      if (hasFlag(limOpt, RLIM_SOFT)) {
        limit.rlim_cur = value;
      }
      if (hasFlag(limOpt, RLIM_HARD)) {
        limit.rlim_max = value;
      }
      if (setrlimit(op.resource, &limit) < 0) {
        PERROR(st, argvObj, "%s: cannot change limit", op.name);
        return 1;
      }
    }
    if (hasFlag(table.printSet, static_cast<uint64_t>(1) << index)) {
      if (!ulimitOps[index].print(limOpt, maxNameLen)) {
        errNum = errno;
        break;
      }
    }
  }
  CHECK_STDOUT_ERROR(st, argvObj, errNum);
  return 0;
}

#undef TRY
#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!(E)) {                                                                                    \
      return errno;                                                                                \
    }                                                                                              \
  } while (false)

enum class PrintMaskOp : unsigned char {
  ONLY_PRINT = 1 << 0,
  REUSE = 1 << 1,
  SYMBOLIC = 1 << 2,
};

template <>
struct allow_enum_bitop<PrintMaskOp> : std::true_type {};

static int printMask(mode_t mask, PrintMaskOp op) {
  errno = 0;
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
    TRY(printf("%s%s\n", hasFlag(op, PrintMaskOp::REUSE) ? "umask -S " : "", buf) > -1);
  } else if (hasFlag(op, PrintMaskOp::ONLY_PRINT)) {
    TRY(printf("%s%04o\n", hasFlag(op, PrintMaskOp::REUSE) ? "umask " : "", mask) > -1);
  }
  return 0;
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
    switch (*(value++)) {
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
  const char op = *(value++);
  if (op != '-' && op != '+' && op != '=') {
    return false;
  }

  // [rwx]*
  mode_t newMode = 0;
  while (*value && *value != ',') {
    switch (*(value++)) {
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
    if (const char ch = *(value++); ch == ',' && parseMode(value, ret.mode)) {
      continue;
    }
    ret.success = false;
    ret.invalid = *(--value);
    break;
  }
  return ret;
}

static int builtin_umask(ARState &st, ArrayObject &argvObj) {
  auto op = PrintMaskOp::ONLY_PRINT;

  GetOptState optState("pSh");
  for (int opt; (opt = optState(argvObj)) != -1;) {
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
      return invalidOptionError(st, argvObj, optState);
    }
  }

  auto mask = umask(0);
  umask(mask);

  if (optState.index < argvObj.getValues().size()) {
    unsetFlag(op, PrintMaskOp::ONLY_PRINT | PrintMaskOp::REUSE);
    const auto value = argvObj.getValues()[optState.index].asStrRef();
    if (!value.empty() && isDecimal(*value.data())) {
      const auto pair = convertToNum<int32_t>(value.begin(), value.end(), 8);
      const int num = pair.value;
      if (!pair || num < 0 || num > 0777) {
        ERROR(st, argvObj, "%s: octal number out of range (0000~0777)", toPrintable(value).c_str());
        return 1;
      }
      mask = num;
    } else {
      const auto ret = parseSymbolicMode(value, mask);
      mask = ret.mode;
      if (!ret.success) {
        const int ch = static_cast<unsigned char>(ret.invalid);
        if (isascii(ch) && ch != 0) {
          ERROR(st, argvObj, "%c: invalid symbolic operator", ch);
        } else {
          ERROR(st, argvObj, "0x%02x: invalid symbolic operator", ch);
        }
        return 1;
      }
    }
    umask(mask);
  }
  const int errNum = printMask(mask, op);
  CHECK_STDOUT_ERROR(st, argvObj, errNum);
  return 0;
}

} // namespace arsh
