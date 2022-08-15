#include "gtest/gtest.h"

#include "../../tools/platform/platform.h"
#include "../test_common.h"
#include <config.h>
#include <constant.h>

#ifndef BIN_PATH
#define "require BIN_PATH"
#endif

#ifndef PID_CHECK_PATH
#error "require PID_CHECK_PATH"
#endif

using namespace ydsh;

template <typename... T>
static ProcBuilder ds(T &&...args) {
  return ProcBuilder{BIN_PATH, std::forward<T>(args)...};
}

struct InputWrapper {
  std::string value;
  ProcBuilder builder;

  auto execAndGetResult() {
    auto handle =
        this->builder.setIn(IOConfig::PIPE).setOut(IOConfig::PIPE).setErr(IOConfig::PIPE)();
    if (write(handle.in(), this->value.c_str(), this->value.size()) < 0) {
      fatal_perror("");
    }
    close(handle.in());
    return handle.waitAndGetResult(false);
  }
};

template <unsigned int N>
InputWrapper operator|(const char (&value)[N], ProcBuilder &&builder) {
  return InputWrapper{
      .value = std::string(value, N - 1),
      .builder = std::forward<ProcBuilder>(builder),
  };
}

class CmdlineTest : public ExpectOutput {
public:
  CmdlineTest() = default;

  using ExpectOutput::expect;
  using ExpectOutput::expectRegex;

  void expect(InputWrapper &&wrapper, int status, const std::string &out = "",
              const std::string &err = "") {
    auto result = wrapper.execAndGetResult();
    ExpectOutput::expect(result, status, WaitStatus::EXITED, out, err);
  }

  void expectRegex(InputWrapper &&wrapper, int status, const std::string &out = "",
                   const std::string &err = "") {
    auto result = wrapper.execAndGetResult();
    ExpectOutput::expectRegex(result, status, WaitStatus::EXITED, out, err);
  }
};

template <unsigned int N>
static std::string toString(const char (&value)[N]) {
  static_assert(N > 0);
  return std::string(value, N - 1);
}

TEST_F(CmdlineTest, marker1) {
  // line marker of syntax error
  const char *msg = "(string):4:4: [syntax error] expected `=', `:'\n   \n   ^\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "var a   \n    \\\n   \t  \t  \n   "), 1, "", msg));

  msg = "(string):1:10: [syntax error] expected `=', `:'\nvar a    \n         ^\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "var a    "), 1, "", msg));

  auto result = ds("-c", "{").execAndGetResult(false);
  ASSERT_EQ(1, result.status.value);
  ASSERT_EQ("", result.out);
  ASSERT_STREQ("{\n ^\n", strchr(result.err.c_str(), '\n') + 1);

  result = ds("-c", "\n);").execAndGetResult(false);
  ASSERT_EQ(1, result.status.value);
  ASSERT_STREQ(");\n^\n", strchr(result.err.c_str(), '\n') + 1);
  ASSERT_EQ("", result.out);

  // line marker of semantic error
  msg = R"((string):1:6: [semantic error] require `Int' type, but is `String' type
[34, "hey"]
     ^~~~~
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "[34, \"hey\"]"), 1, "", msg));

  // line marker containing newline
  const char *s = R"(
var a = 34
$a = 34 +
     'de'
)";
  msg = R"((string):3:6: [semantic error] require `Int' type, but is `String' type
$a = 34 +
     ^~~~
     'de'
~~~~~~~~~
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", s), 1, "", msg));

  // line marker (reach null character)
  msg = ".+:1:6: \\[syntax error\\] invalid token, expected `<NewLine>'\nhello\n     \n";
  ASSERT_NO_FATAL_FAILURE(this->expectRegex("hello\0world" | ds(), 1, "", msg));
}

TEST_F(CmdlineTest, marker2) {
  const char *s = "$e";
  const char *msg = R"((string):1:1: [semantic error] undefined symbol: `e'
$e
^~
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", s), 1, "", msg));

  s = "for $a in 34 {}";
  msg = "(string):1:11: [semantic error] undefined method: `%iter'\n"
        "for $a in 34 {}\n"
        "          ^~\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", s), 1, "", msg));
}

TEST_F(CmdlineTest, marker3) {
  const char *msg = R"((string):1:4: [semantic error] undefined symbol: `a'
"${a.b}"
   ^
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", R"EOF("${a.b}")EOF"), 1, "", msg));

  msg = R"((string):1:13: [semantic error] undefined field: `t'
echo ${true.t}
            ^
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", R"EOF(echo ${true.t})EOF"), 1, "", msg));

  msg = R"((string):1:11: [semantic error] unreachable code
throw 23; assert $false
          ^~~~~~~~~~~~~
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", "throw 23; assert $false"), 1, "", msg));

  msg = R"((string):2:6: [semantic error] require `Int' type, but is `Boolean' type
$a = true|(true|false)
     ^~~~~~~~~~~~~~~~~
)";
  ASSERT_NO_FATAL_FAILURE(
      this->expect(ds("-c", "var a = 34;\n$a = true|(true|false)"), 1, "", msg));
}

TEST_F(CmdlineTest, marker4) {
  const char *msg = R"((string):1:10: [semantic error] undefined field: `d'
'まま○2'.d
         ^
)";
  auto builder = ds("-c", "'まま○2'.d").addEnv("LC_CTYPE", "C").addEnv("LC_ALL", "C");
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 1, "", msg));

  if (setlocale(LC_CTYPE, "ja_JP.UTF-8")) {
    msg = R"((string):1:11: [semantic error] undefined field: `d'
'まま○2'.d
          ^
)";
    builder =
        ds("-c", "'まま○2'.d").addEnv("LC_CTYPE", "ja_JP.UTF-8").addEnv("LC_ALL", "ja_JP.UTF-8");
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 1, "", msg));
  }

  if (setlocale(LC_CTYPE, "zh_CN.UTF-8")) {
    msg = R"((string):1:11: [semantic error] undefined field: `d'
'まま○2'.d
          ^
)";
    builder =
        ds("-c", "'まま○2'.d").addEnv("LC_CTYPE", "zh_CN.UTF-8").addEnv("LC_ALL", "zh_CN.UTF-8");
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 1, "", msg));
  }

  if (setlocale(LC_CTYPE, "ko_KR.UTF-8")) {
    msg = R"((string):1:11: [semantic error] undefined field: `d'
'まま○2'.d
          ^
)";
    builder =
        ds("-c", "'まま○2'.d").addEnv("LC_CTYPE", "ko_KR.UTF-8").addEnv("LC_ALL", "ko_KR.UTF-8");
    ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 1, "", msg));
  }
}

TEST_F(CmdlineTest, version) {
  std::string msg = "^ydsh, version ";
  msg += X_INFO_VERSION;
  msg += ", build by .+\n";
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("--version"), 0, msg));
}

TEST_F(CmdlineTest, logger) {
#ifdef USE_LOGGING
  bool useLogging = true;
#else
  bool useLogging = false;
#endif

  std::string cmd = BIN_PATH;
  cmd += " --feature | grep USE_LOGGING";
  if (useLogging) {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd.c_str()), 0, "USE_LOGGING\n"));

    auto builder = ds("-c", "sh -c true").addEnv("YDSH_DUMP_EXEC", "on");
    const char *re = ".+\\(xexecve\\).+";
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(std::move(builder), 0, "", re));

    // specify appender
    builder = ds("-c", "var a = 0; exit $a")
                  .addEnv("YDSH_TRACE_TOKEN", "on")
                  .addEnv("YDSH_APPENDER", "/dev/stdout");
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(std::move(builder), 0, ".+"));

    // specify appender (not found)
    builder = ds("-c", "var a = 0; exit $a")
                  .addEnv("YDSH_TRACE_TOKEN", "on")
                  .addEnv("YDSH_APPENDER", "/dev/null/hogehu");
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(std::move(builder), 0, "", ".+"));
  } else {
    ASSERT_NO_FATAL_FAILURE(this->expect(ds("-c", cmd.c_str()), 1));
  }
}

#define CL(...)                                                                                    \
  ProcBuilder { BIN_PATH, "-c", format(__VA_ARGS__).c_str() }

TEST_F(CmdlineTest, pid) {
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("%s --pid $PID --ppid $PPID | grep .", PID_CHECK_PATH), 0, "OK\n"));
}

#define DS(S) ds("-c", S)

TEST_F(CmdlineTest, termHook) {
  const char *src = R"(
        function f($k : Int, $a : Any) {
            echo receive error: $k: $a
        }
        $TERM_HOOK = $f

        exit 45
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(DS(src), 45, "receive error: 1: 45\n"));

  src = R"(
        function f($k : Int, $a : Any) {
            echo receive error: $k: $a
        }
        $TERM_HOOK = $f

        45 / 0
)";
  const char *e = R"([runtime error]
ArithmeticError: zero division
    from (string):7 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(
      this->expect(DS(src), 1, "receive error: 2: ArithmeticError: zero division\n", e));

  src = R"(
        function f($k : Int, $a : Any) {
            echo receive error: $k: $a
        }
        $TERM_HOOK = $f

        assert false
)";
  e = R"(Assertion Error: `false'
    from (string):7 '<toplevel>()'
)";
  ASSERT_NO_FATAL_FAILURE(this->expect(DS(src), 1, "receive error: 4: 1\n", e));
}

TEST_F(CmdlineTest, signal) {
  // simple command
  std::string str = strsignal(SIGKILL);
  str += "\n";
  ASSERT_NO_FATAL_FAILURE(this->expect(DS("sh -c 'kill -s kill $$'"), 128 + SIGKILL, "", str));
  ASSERT_NO_FATAL_FAILURE(this->expect(DS("echo ${$SIG['kill'].message()}"), 0, str));

  // pipeline
  ASSERT_NO_FATAL_FAILURE(
      this->expect(DS("echo hello | sh -c 'kill -s kill $$'"), 128 + SIGKILL, "", str));

  // command substitution
  ASSERT_NO_FATAL_FAILURE(this->expect(DS("$(sh -c 'kill -s kill $$')"), 128 + SIGKILL, "", str));

  // background job (not report signal message)
  ASSERT_NO_FATAL_FAILURE(
      this->expect(DS("var j = sh -c 'kill -s kill $$' & exit ${$j.wait()}"), 128 + SIGKILL));

  // background job (not report signal message)
  ASSERT_NO_FATAL_FAILURE(this->expect(DS("sh -c 'kill -s kill $$' & wait"), 128 + SIGKILL));

  if (platform::platform() == platform::PlatformType::DARWIN) {
    return;
  }

  // core dump
  str = strsignal(SIGQUIT);
  str += " (core dumped)\n";
  auto builder = DS(R"(
        ulimit -c unlimited 2> /dev/null
        echo | eval $YDSH_BIN -c 'kill -s quit $$'
        exit $?
)");
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 128 + SIGQUIT, "", str));
}

TEST_F(CmdlineTest, execPath) {
  ASSERT_NO_FATAL_FAILURE(this->expect(DS("echo -n $YDSH_BIN"), 0, BIN_PATH));
}

struct CmdlineTest2 : public CmdlineTest, public TempFileFactory {
  CmdlineTest2() : INIT_TEMP_FILE_FACTORY(cmdline_test) {}
};

TEST_F(CmdlineTest2, exec) {
  auto fileName = this->createTempFile("run.sh", "echo hey: $0: $1 $2");
  errno = 0;
  auto mode = getStMode(fileName.c_str());
  mode |= S_IXUSR | S_IXGRP | S_IXOTH;
  chmod(fileName.c_str(), mode);
  ASSERT_STREQ(strerror(0), strerror(errno));

  auto out = format("hey: %s: 11111 8888\n", fileName.c_str());
  auto cmd = format("%s 11111 8888", fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->expect(DS(cmd.c_str()), 0, out));
}

TEST_F(CmdlineTest2, script) {
  auto fileName = this->createTempFile(
      "target.ds",
      format("assert($0 == \"%s/target.ds\"); assert($@.size() == 1); assert($@[0] == 'A')",
             this->getTempDirName()));

  ASSERT_NO_FATAL_FAILURE(this->expect(ds(fileName.c_str(), "A"), 0));
  ASSERT_NO_FATAL_FAILURE(this->expect(ds("--", fileName.c_str(), "A"), 0));
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(ds("hogehogehuga"), 1, "",
                                            "^ydsh: cannot load file: hogehogehuga, by .+$"));

  // script dir
  fileName =
      this->createTempFile("target2.ds", "assert $SCRIPT_DIR == \"$(cd $(dirname $0) && pwd -P)\"");

  ASSERT_NO_FATAL_FAILURE(this->expect(ds(fileName.c_str()), 0));
}

TEST_F(CmdlineTest2, complete) {
  std::string target = this->getTempDirName();
  target += "/work/actual";

  // create working dir
  auto builder =
      CL("mkdir -p %s; ln -s %s ./link && cd ./link && touch hogehuga && chmod +x hogehuga",
         target.c_str(), target.c_str())
          .setWorkingDir(this->getTempDirName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

  // follow symbolic link
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("cd %s; assert \"$(complete ./link/)\" == 'hogehuga'", this->getTempDirName()), 0));

  builder = CL("cd %s; var ret = $(complete ./link/../);\n"
               "assert $ret.size() == 2\n"
               "assert $ret[0] == 'link/'\n"
               "assert $ret[1] == 'work/'",
               this->getTempDirName());

  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));
}

TEST_F(CmdlineTest2, cwd) {
  std::string target = this->getTempDirName();
  target += "/work/actual";

  // create working dir
  auto builder = CL("mkdir -p %s; ln -s %s ./link", target.c_str(), target.c_str())
                     .setWorkingDir(this->getTempDirName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

  // follow symbolic link
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("cd %s/link; import-env PWD; assert $PWD == '%s/link'",
                                          this->getTempDirName(), this->getTempDirName()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("cd %s/link; assert \"$(pwd)\" == '%s/link'",
                                          this->getTempDirName(), this->getTempDirName()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("cd -L %s/link; assert \"$(pwd -L)\" == '%s/link'",
                                          this->getTempDirName(), this->getTempDirName()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd %s/link); assert \"$(pwd -P)\" == '%s'",
                                          this->getTempDirName(), target.c_str()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("assert(cd %s/link); assert cd ../; assert \"$(pwd -P)\" == '%s'",
                      this->getTempDirName(), this->getTempDirName()),
                   0));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("assert(cd %s/link); assert cd ../; assert \"$(pwd -L)\" == '%s'",
                      this->getTempDirName(), this->getTempDirName()),
                   0));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("assert(cd %s/link); assert cd ../; import-env OLDPWD; assert $OLDPWD == '%s/link'",
         this->getTempDirName(), this->getTempDirName()),
      0));

  // without symbolic link
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("assert(cd -P %s/link); import-env PWD; assert $PWD == '%s'",
                      this->getTempDirName(), target.c_str()),
                   0));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert \"$(pwd)\" == '%s'",
                                          this->getTempDirName(), target.c_str()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert \"$(pwd -P)\" == '%s'",
                                          this->getTempDirName(), target.c_str()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("assert(cd -P %s/link); assert \"$(pwd -L)\" == '%s'",
                                          this->getTempDirName(), target.c_str()),
                                       0));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("assert(cd -P %s/link); assert cd ../; assert \"$(pwd -L)\" == '%s/work'",
                      this->getTempDirName(), this->getTempDirName()),
                   0));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("assert(cd -P %s/link); assert cd ../; import-env OLDPWD; assert $OLDPWD == '%s'",
         this->getTempDirName(), target.c_str()),
      0));
}

TEST_F(CmdlineTest2, import1) {
  if (platform::platform() == platform::PlatformType::CYGWIN) {
    return;
  }
  if (getuid() == 0) {
    return;
  }

  auto fileName = this->createTempFile("target.ds", "throw new Error('invalid!!')");
  chmod(fileName.c_str(), ~S_IRUSR);

  std::string str =
      format("(string):1:8: [semantic error] cannot read module: `%s', by `Permission denied'\n"
             "source %s as mod\n"
             "       %s\n",
             fileName.c_str(), fileName.c_str(), makeLineMarker(fileName).c_str());

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("source %s as mod", fileName.c_str()), 1, "", str));
}

TEST_F(CmdlineTest2, import2) {
  auto modName =
      this->createTempFile("mod.ds", format("source %s/target.ds as mod2", this->getTempDirName()));
  auto fileName =
      this->createTempFile("target.ds", format("source %s/mod.ds as mod1", this->getTempDirName()));

  std::string str =
      format("%s:1:8: [semantic error] circular module import: `%s'\n"
             "source %s as mod2\n"
             "       %s\n",
             modName.c_str(), fileName.c_str(), fileName.c_str(), makeLineMarker(fileName).c_str());
  str += format("%s:1:8: [note] at module import\n"
                "source %s as mod1\n"
                "       %s\n",
                fileName.c_str(), modName.c_str(), makeLineMarker(modName).c_str());

  ASSERT_NO_FATAL_FAILURE(this->expect(ProcBuilder{BIN_PATH, fileName.c_str()}, 1, "", str));
}

TEST_F(CmdlineTest2, import3) {
  std::string str =
      format("(string):1:9: [semantic error] cannot read module: `.', by `Is a directory'\n"
             "source  .\n"
             "        %s\n",
             makeLineMarker(".").c_str());
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("source  ."), 1, "", str));
}

TEST_F(CmdlineTest2, import4) {
  std::string str = format("(string):1:10: [semantic error] module not found: `hoge=~/huga'\n"
                           "source   hoge=~/huga\n"
                           "         %s\n",
                           makeLineMarker("hoge=~/huga").c_str());
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("source   hoge=~/huga"), 1, "", str));
}

TEST_F(CmdlineTest2, import5) {
  auto modName = this->createTempFile("mod.ds", "var a = 0;\n\n34/$a");
  auto str = format("[runtime error]\n"
                    "ArithmeticError: zero division\n"
                    "    from %s:3 '<toplevel>()'\n"
                    "    from (string):2 '<toplevel>()'\n",
                    modName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("34\nsource %s", modName.c_str()), 1, "", str));
}

TEST_F(CmdlineTest2, backtrace) {
  auto fileName = this->createTempFile("file.ds", "function f() { shctl backtrace; }");
  auto str = format("from %s:1 'function f()'\n"
                    "from (string):2 '<toplevel>()'\n",
                    fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("source %s; 34\n$f()", fileName.c_str()), 0, str));

  ASSERT_NO_FATAL_FAILURE(this->expect(ds("-e", "shctl", "backtrace"), 0));
}

TEST_F(CmdlineTest2, nocwd) {
  auto builder =
      CL("assert '.' == $SCRIPT_DIR").setWorkingDir(this->getTempDirName()).setBeforeExec([&] {
        removeDirWithRecursively(this->getTempDirName());
      });
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));
}

TEST_F(CmdlineTest2, import_nocwd1) {
  std::string workdir = this->getTempDirName();
  workdir += "/work";

  {
    std::string text = format("mkdir -p %s; echo echo hello >> %s/module.ds", workdir.c_str(),
                              this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(DS(text.c_str()), 0));
  }

  /**
   * even if cwd is removed and mod path is relative,
   * module loading is still success
   */
  auto builder = DS("source ../module.ds").setWorkingDir(workdir.c_str()).setBeforeExec([&] {
    removeDirWithRecursively(workdir.c_str());
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0, "hello\n"));
}

TEST_F(CmdlineTest2, import_nocwd2) {
  std::string workdir = this->getTempDirName();
  workdir += "/work";

  {
    std::string text = format("mkdir -p %s; echo echo hello >> %s/module.ds", workdir.c_str(),
                              this->getTempDirName());
    ASSERT_NO_FATAL_FAILURE(this->expect(DS(text.c_str()), 0));
  }

  std::string src = format("source %s/module.ds", this->getTempDirName());
  auto builder = DS(src.c_str()).setWorkingDir(workdir.c_str()).setBeforeExec([&] {
    removeDirWithRecursively(workdir.c_str());
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0, "hello\n"));
}

struct CmdlineTest3 : public CmdlineTest2 {
  using ExpectOutput::expect;

  struct Param {
    std::string workdir;
    std::function<void()> beforeExec;
    std::pair<std::string, std::string> before;
    std::pair<std::string, std::string> after;
  };

  void expect(Param &&p) {
    auto builder = CL("import-env PWD; import-env OLDPWD; echo -n $PWD $OLDPWD")
                       .setWorkingDir(p.workdir.c_str())
                       .setBeforeExec([&] {
                         if (p.beforeExec) {
                           p.beforeExec();
                         }

                         // set PWD
                         if (p.before.first.empty()) {
                           unsetenv(ENV_PWD);
                         } else {
                           setenv(ENV_PWD, p.before.first.c_str(), 1);
                         }

                         // set OLDPWD
                         if (p.before.second.empty()) {
                           unsetenv(ENV_OLDPWD);
                         } else {
                           setenv(ENV_OLDPWD, p.before.second.c_str(), 1);
                         }
                       });

    std::string out = p.after.first;
    out += " ";
    out += p.after.second;
    this->expect(std::move(builder), 0, out);
  }
};

TEST_F(CmdlineTest3, pwd) {
  std::string target = this->getTempDirName();
  target += "/work/actual";
  std::string link = this->getTempDirName();
  link += "/link";

  // create working dir
  auto builder = CL("mkdir -p %s; ln -s %s ./link", target.c_str(), target.c_str())
                     .setWorkingDir(this->getTempDirName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

  // [PWD, OLDWD] is [valid, valid]
  // [PWD, OLDPWD] is [valid, valid] => [not update, not update]
  Param p = {.workdir = target,
             .beforeExec = {},
             .before = {target, this->getTempDirName()},
             .after = {target, this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid with symlink, valid] => [not update, not update]
  p = {.workdir = link,
       .beforeExec = {},
       .before = {link, this->getTempDirName()},
       .after = {link, this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid, valid with symlink] => [not update, not update]
  p = {.workdir = target, .beforeExec = {}, .before = {target, link}, .after = {target, link}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid, invalid]
  // [PWD, OLDPWD] is [valid, not set] => [not update, PWD]
  p = {.workdir = target, .beforeExec = {}, .before = {target, ""}, .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid, not dir] => [not update, PWD]
  p = {
      .workdir = target, .beforeExec = {}, .before = {target, BIN_PATH}, .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid, not full path] => [not update, PWD]
  p = {.workdir = target, .beforeExec = {}, .before = {target, "."}, .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid with symlink, not set] => [not update, PWD]
  p = {.workdir = link, .beforeExec = {}, .before = {link, ""}, .after = {link, link}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [invalid, valid]
  // [PWD, OLDPWD] is [not set, valid] => [cwd, not update]
  p = {.workdir = target,
       .beforeExec = {},
       .before = {"", this->getTempDirName()},
       .after = {target, this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [invalid, valid] => [cwd, not update]
  p = {.workdir = target,
       .beforeExec = {},
       .before = {"hgoehgoa", this->getTempDirName()},
       .after = {target, this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [not full path, valid] => [cwd, not update]
  p = {.workdir = link,
       .beforeExec = {},
       .before = {".", this->getTempDirName()},
       .after = {target, this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [not full path, valid with symlink] => [cwd, not update]
  p = {.workdir = this->getTempDirName(),
       .beforeExec = {},
       .before = {".", link},
       .after = {this->getTempDirName(), link}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [not dir, valid] => [cwd, not update]
  p = {.workdir = target,
       .beforeExec = {},
       .before = {BIN_PATH, this->getTempDirName()},
       .after = {target, this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [invalid, invalid]
  // [PWD, OLDPWD] is [not set, not set] => [cwd, cwd]
  p = {.workdir = target, .beforeExec = {}, .before = {"", ""}, .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [not dir, not dir] => [cwd, cwd]
  p = {
      .workdir = link, .beforeExec = {}, .before = {BIN_PATH, BIN_PATH}, .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [not dir, not dir] => [cwd, cwd]
  p = {.workdir = link, .beforeExec = {}, .before = {BIN_PATH, "."}, .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // cwd is not removed
  // [PWD, OLDPWD] is [valid with symlink, valid with symlink] => [cwd, cwd]
  p = {.workdir = link,
       .beforeExec = [&] { removeDirWithRecursively(link.c_str()); },
       .before = {link, link},
       .after = {target, target}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  // [PWD, OLDPWD] is [valid, valid] => [., not update]
  p = {.workdir = target,
       .beforeExec = [&] { removeDirWithRecursively(target.c_str()); },
       .before = {target, this->getTempDirName()},
       .after = {".", this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  builder = CL("mkdir -p %s; ln -s %s ./link", target.c_str(), target.c_str())
                .setWorkingDir(this->getTempDirName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

  // [PWD, OLDPWD] is [valid with symlink, valid with symlink] => [., .]
  p = {.workdir = link,
       .beforeExec = [&] { removeDirWithRecursively(target.c_str()); },
       .before = {link, link},
       .after = {".", "."}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));

  builder = CL("mkdir -p %s", target.c_str()).setWorkingDir(this->getTempDirName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(builder), 0));

  // [PWD, OLDPWD] is [valid, valid with symlink] => [not update, PWD]
  p = {.workdir = this->getTempDirName(),
       .beforeExec = [&] { removeDirWithRecursively(link.c_str()); },
       .before = {this->getTempDirName(), link},
       .after = {this->getTempDirName(), this->getTempDirName()}};
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(p)));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}