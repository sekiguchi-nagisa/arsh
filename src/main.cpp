/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#include <unistd.h>

#include <memory>

#include "misc/flag_util.hpp"
#include "misc/opt_parser.hpp"
#include <arsh/arsh.h>

using namespace arsh;

struct Deleter {
  void operator()(ARState *state) const { ARState_delete(&state); }
};

using ARStateHandle = std::unique_ptr<ARState, Deleter>;

template <typename... Arg>
static auto createState(Arg &&...arg) {
  return ARStateHandle(ARState_createWithMode(std::forward<Arg>(arg)...));
}

static const char *statusLogPath = nullptr;

static std::string escape(const char *str) {
  std::string value;
  value += '"';
  for (; str != nullptr && *str != '\0'; str++) {
    char ch = *str;
    if (ch == '\\' || ch == '"') {
      value += '\\';
    }
    value += ch;
  }
  value += '"';
  return value;
}

static void writeStatusLog(ARError &error) {
  if (statusLogPath == nullptr) {
    return;
  }

  FILE *fp = fopen(statusLogPath, "w");
  if (fp != nullptr) {
    fprintf(fp, "kind=%d lineNum=%d chars=%d name=%s fileName=%s\n", error.kind, error.lineNum,
            error.chars, escape(error.name).c_str(), escape(error.fileName).c_str());
    fclose(fp);
  }
}

template <typename Func, typename... T>
static int apply(Func func, const ARStateHandle &handle, T &&...args) {
  ARError error{};
  int ret = func(handle.get(), std::forward<T>(args)..., &error);
  writeStatusLog(error);
  ARError_release(&error);
  return ret;
}

static void showFeature(FILE *fp) {
  const char *featureNames[] = {
      "USE_LOGGING",
      "USE_SAFE_CAST",
  };

  const unsigned int featureBit = ARState_featureBit();
  for (unsigned int i = 0; i < std::size(featureNames); i++) {
    if (hasFlag(featureBit, static_cast<unsigned int>(1u << i))) {
      fprintf(fp, "%s\n", featureNames[i]);
    }
  }
}

enum class OptionKind : unsigned char {
  DUMP_UAST,
  DUMP_AST,
  DUMP_CODE,
  PARSE_ONLY,
  CHECK_ONLY,
  COMPILE_ONLY,
  DISABLE_ASSERT,
  TRACE_EXIT,
  VERSION,
  HELP,
  COMMAND,
  NORC,
  EXEC,
  STATUS_LOG,
  FEATURE,
  RC_FILE,
  QUIET,
  SET_ARGS,
  INTERACTIVE,
  NOEXEC,
  XTRACE,
};

static const OptParser<OptionKind>::Option options[] = {
    {OptionKind::DUMP_UAST, 0, "dump-untyped-ast", OptParseOp::OPT_ARG, "file",
     "dump abstract syntax tree (before type checking)"},
    {OptionKind::DUMP_AST, 0, "dump-ast", OptParseOp::OPT_ARG, "file",
     "dump abstract syntax tree (after type checking)"},
    {OptionKind::DUMP_CODE, 0, "dump-code", OptParseOp::OPT_ARG, "file", "dump compiled code"},
    {OptionKind::PARSE_ONLY, 0, "parse-only", OptParseOp::NO_ARG, "not evaluate, parse only"},
    {OptionKind::CHECK_ONLY, 0, "check-only", OptParseOp::NO_ARG, "not evaluate, type check only"},
    {OptionKind::COMPILE_ONLY, 0, "compile-only", OptParseOp::NO_ARG, "not evaluate, compile only"},
    {OptionKind::DISABLE_ASSERT, 0, "disable-assertion", OptParseOp::NO_ARG,
     "disable assert statement"},
    {OptionKind::TRACE_EXIT, 0, "trace-exit", OptParseOp::NO_ARG,
     "print stack strace on exit command"},
    {OptionKind::VERSION, 0, "version", OptParseOp::NO_ARG, "show version and copyright"},
    {OptionKind::HELP, 0, "help", OptParseOp::NO_ARG, "show this help message"},
    {OptionKind::COMMAND, 'c', "", OptParseOp::HAS_ARG, "string", "evaluate argument"},
    {OptionKind::NORC, 0, "norc", OptParseOp::NO_ARG,
     "not load rc file (only available interactive mode)"},
    {OptionKind::EXEC, 'e', "", OptParseOp::HAS_ARG, "cmd",
     "execute command (ignore some options)"},
    {OptionKind::STATUS_LOG, 0, "status-log", OptParseOp::HAS_ARG, "file",
     "write execution status to specified file (ignored in interactive mode or -e)"},
    {OptionKind::FEATURE, 0, "feature", OptParseOp::NO_ARG, "show available features"},
    {OptionKind::RC_FILE, 0, "rcfile", OptParseOp::HAS_ARG, "file",
     "load specified rc file (only available interactive mode)"},
    {OptionKind::QUIET, 0, "quiet", OptParseOp::NO_ARG,
     "suppress startup message (only available interactive mode)"},
    {OptionKind::SET_ARGS, 's', "", OptParseOp::NO_ARG,
     "set arguments and read command from standard input"},
    {OptionKind::INTERACTIVE, 'i', "", OptParseOp::NO_ARG, "run interactive mode"},
    {OptionKind::NOEXEC, 'n', "", OptParseOp::NO_ARG, "equivalent to `--compile-only' option"},
    {OptionKind::XTRACE, 'x', "", OptParseOp::NO_ARG, "trace execution of commands"},
};

enum class InvocationKind : unsigned char {
  FROM_FILE,
  FROM_STDIN,
  FROM_STRING,
  BUILTIN,
};

static const char *version() { return ARState_version(nullptr); }

static std::string getRCFilePath(ARState *state, const char *path) {
  std::string value;
  if (path) {
    value = path;
  } else { // use default
    auto *ptr = ARState_config(state, AR_CONFIG_CONFIG_HOME);
    assert(ptr);
    value = ptr;
    value += "/arshrc";

    if (access(value.c_str(), F_OK) != 0) {
      const char *argv[] = {
          "mkdir",
          "-p",
          ptr,
          nullptr,
      };
      ARState_exec(state, const_cast<char **>(argv));

      if (FILE *fp = fopen(value.c_str(), "w")) {
        fprintf(fp, "## default interactive mode setting\n"
                    "## must install `repl` module before load this file\n"
                    "source repl inlined\n"
                    "## write addition interactive mode setting bellow\n");
        fclose(fp);
      }
    }
  }
  return value;
}

static std::pair<ARErrorKind, int> loadRC(ARState *state, const char *rcfile) {
  std::string path = getRCFilePath(state, rcfile);
  if (path.empty()) { // for --norc option
    return {AR_ERROR_KIND_SUCCESS, 0};
  }

  ARError e{};
  int ret = ARState_loadModule(state, path.c_str(), AR_MOD_FULLPATH | AR_MOD_IGNORE_ENOENT, &e);
  auto kind = e.kind;
  ARError_release(&e);

  // reset line num
  ARState_setLineNum(state, 1);

  return {kind, ret};
}

static int exec_interactive(ARState *state, const char *rcpath) {
  unsigned int option = AR_OPTION_JOB_CONTROL | AR_OPTION_INTERACTIVE;
  ARState_setOption(state, option);

  auto ret = loadRC(state, rcpath);
  if (ret.first != AR_ERROR_KIND_SUCCESS) {
    return ret.second;
  }

  unsigned int eioRetryCount = 0;
  int status = 0;
  while (true) {
    ARError e; // NOLINT
    char buf[4096];
    auto readSize = ARState_readLine(state, buf, std::size(buf), &e);
    auto kind = e.kind;
    ARError_release(&e);
    if (kind == AR_ERROR_KIND_EXIT || kind == AR_ERROR_KIND_ASSERTION_ERROR) {
      status = ARState_exitStatus(state);
      break;
    }
    if (readSize < 0) {
      if (errno == EAGAIN) {
        continue;
      }
      if (errno != 0) {
        if (errno == EIO && eioRetryCount < 2) {
          eioRetryCount++; // workaround for EIO of pty read
          fprintf(stderr, "[warn] retry readLine, caused by `%s'\n", strerror(EIO));
          continue;
        }
        fprintf(stderr, "[fatal] readLine failed, caused by `%s'\n", strerror(errno));
        return 1;
      }
      if (ARState_mode(state) != AR_EXEC_MODE_NORMAL) {
        break;
      }
      const char *str = "exit";
      auto size = strlen(str);
      assert(size + 1 <= std::size(buf));
      memcpy(buf, str, size);
      buf[size] = '\0';
      readSize = static_cast<ssize_t>(size);
    }

    eioRetryCount = 0;
    status = ARState_eval(state, nullptr, buf, static_cast<size_t>(readSize), &e);
    kind = e.kind;
    ARError_release(&e);
    if (kind == AR_ERROR_KIND_EXIT || kind == AR_ERROR_KIND_ASSERTION_ERROR) {
      break;
    }
  }
  return status;
}

int main(int argc, char **argv) {
  auto parser = createOptParser(options);
  auto begin = argv + (argc > 0 ? 1 : 0);
  auto end = argv + argc;
  OptParseResult<OptionKind> result;

  InvocationKind invocationKind = InvocationKind::FROM_FILE;
  const char *evalText = nullptr;
  const char *rcfile = nullptr;
  bool quiet = false;
  bool forceInteractive = false;
  ARExecMode mode = AR_EXEC_MODE_NORMAL;
  unsigned int option = 0;
  bool noAssert = false;
  struct {
    const ARDumpKind kind;
    const char *path;
  } dumpTarget[3] = {
      {AR_DUMP_KIND_UAST, nullptr},
      {AR_DUMP_KIND_AST, nullptr},
      {AR_DUMP_KIND_CODE, nullptr},
  };

  while ((result = parser(begin, end))) {
    switch (result.getOpt()) {
    case OptionKind::DUMP_UAST:
      dumpTarget[0].path = result.hasArg() ? result.getValue().data() : "";
      break;
    case OptionKind::DUMP_AST:
      dumpTarget[1].path = result.hasArg() ? result.getValue().data() : "";
      break;
    case OptionKind::DUMP_CODE:
      dumpTarget[2].path = result.hasArg() ? result.getValue().data() : "";
      break;
    case OptionKind::PARSE_ONLY:
      mode = AR_EXEC_MODE_PARSE_ONLY;
      break;
    case OptionKind::CHECK_ONLY:
      mode = AR_EXEC_MODE_CHECK_ONLY;
      break;
    case OptionKind::COMPILE_ONLY:
    case OptionKind::NOEXEC:
      mode = AR_EXEC_MODE_COMPILE_ONLY;
      break;
    case OptionKind::DISABLE_ASSERT:
      noAssert = true;
      break;
    case OptionKind::TRACE_EXIT:
      setFlag(option, AR_OPTION_TRACE_EXIT);
      break;
    case OptionKind::VERSION:
      fprintf(stdout, "%s\n", version());
      return 0;
    case OptionKind::HELP:
      fprintf(stdout, "%s\n%s\n", version(), parser.formatOptions().c_str());
      return 0;
    case OptionKind::COMMAND:
      invocationKind = InvocationKind::FROM_STRING;
      evalText = result.getValue().data();
      goto INIT;
    case OptionKind::NORC:
      rcfile = "";
      break;
    case OptionKind::EXEC:
      invocationKind = InvocationKind::BUILTIN;
      statusLogPath = nullptr;
      --begin;
      goto INIT;
    case OptionKind::STATUS_LOG:
      statusLogPath = result.getValue().data();
      break;
    case OptionKind::FEATURE:
      showFeature(stdout);
      return 0;
    case OptionKind::RC_FILE:
      rcfile = result.getValue().data();
      break;
    case OptionKind::QUIET:
      quiet = true;
      break;
    case OptionKind::SET_ARGS:
      invocationKind = InvocationKind::FROM_STDIN;
      goto INIT;
    case OptionKind::INTERACTIVE:
      forceInteractive = true;
      break;
    case OptionKind::XTRACE:
      setFlag(option, AR_OPTION_XTRACE);
      break;
    }
  }
  if (result.isError()) {
    fprintf(stderr, "%s\n%s\n%s\n", result.formatError().c_str(), version(),
            parser.formatOptions().c_str());
    return 1;
  }

INIT:

  // init state
  auto state = createState(mode);
  ARState_initExecutablePath(state.get());
  ARState_setOption(state.get(), option);
  for (auto &e : dumpTarget) {
    if (e.path != nullptr) {
      ARState_setDumpTarget(state.get(), e.kind, e.path);
    }
  }
  if (noAssert) {
    ARState_unsetOption(state.get(), AR_OPTION_ASSERT);
  }

  // set rest argument
  char **shellArgs = begin;
  if (invocationKind == InvocationKind::FROM_FILE &&
      (shellArgs[0] == nullptr || strcmp(shellArgs[0], "-") == 0)) {
    invocationKind = InvocationKind::FROM_STDIN;
  }

  // execute
  switch (invocationKind) {
  case InvocationKind::FROM_FILE: {
    const char *scriptName = shellArgs[0];
    ARState_setShellName(state.get(), scriptName);
    ARState_setArguments(state.get(), shellArgs + 1);
    return apply(ARState_loadAndEval, state, scriptName);
  }
  case InvocationKind::FROM_STDIN: {
    ARState_setShellName(state.get(), argv[0]);
    ARState_setArguments(state.get(), shellArgs);

    if (isatty(STDIN_FILENO) || forceInteractive) {
      if (!quiet) {
        fprintf(stdout, "%s\n%s\n", version(), ARState_copyright());
      }
      return exec_interactive(state.get(), rcfile);
    } else {
      return apply(ARState_loadAndEval, state, "/dev/stdin");
    }
  }
  case InvocationKind::FROM_STRING: {
    const char *shellName = shellArgs[0];
    ARState_setShellName(state.get(), shellName != nullptr ? shellName : argv[0]);
    ARState_setArguments(state.get(), shellName == nullptr ? nullptr : shellArgs + 1);
    return apply(ARState_eval, state, "(string)", evalText, strlen(evalText));
  }
  case InvocationKind::BUILTIN:
    return ARState_exec(state.get(), shellArgs);
  }
}
