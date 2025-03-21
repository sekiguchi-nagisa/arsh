#include "gtest/gtest.h"

#include <array>

#include <arsh/arsh.h>
#include <config.h>
#include <constant.h>
#include <misc/fatal.h>
#include <misc/format.hpp>
#include <misc/resource.hpp>

#include <sys/utsname.h>

#include "../../tools/platform/platform.h"
#include "../test_common.h"

#ifndef API_TEST_WORK_DIR
#error "require API_TEST_WORK_DIR"
#endif

template <typename... T>
std::array<char *, sizeof...(T) + 2> make_argv(const char *name, T... args) {
  return {{const_cast<char *>(name), const_cast<char *>(args)..., nullptr}};
}

TEST(BuiltinExecTest, case1) {
  ARState *state = ARState_create();
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("echo", "hello").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, case2) {
  ARState *state = ARState_create();
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("./", "hello").data());
  ASSERT_EQ(126, ret);
}

TEST(BuiltinExecTest, case3) {
  ARState *state = ARState_create();
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("exit", "12000").data());
  ASSERT_EQ(224, ret);
}

TEST(BuiltinExecTest, case4) {
  ARState *state = ARState_create();
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, nullptr);
  ASSERT_EQ(-1, ret);

  ret = ARState_exec(nullptr, make_argv("exit", "12000").data());
  ASSERT_EQ(-1, ret);
}

TEST(BuiltinExecTest, case5) {
  ARState *state = ARState_create();
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });
  int ret = ARState_exec(state, make_argv(nullptr).data()); // empty argv
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, shctl) {
  ARState *state = ARState_create();
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("shctl", "is-interactive").data());
  ASSERT_EQ(1, ret);

  ARState_setOption(state, AR_OPTION_INTERACTIVE);
  ret = ARState_exec(state, make_argv("shctl", "is-interactive").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, donothing1) {
  ARState *state = ARState_createWithMode(AR_EXEC_MODE_CHECK_ONLY);
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("jfriejfieori").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, donothing2) {
  ARState *state = ARState_createWithMode(AR_EXEC_MODE_PARSE_ONLY);
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("jfriejfieori").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, donothing3) {
  ARState *state = ARState_createWithMode(AR_EXEC_MODE_COMPILE_ONLY);
  auto cleanup = arsh::finally([&] { ARState_delete(&state); });

  int ret = ARState_exec(state, make_argv("jfriejfieori").data());
  ASSERT_EQ(0, ret);
}

struct APITest : public ExpectOutput, public arsh::TempFileFactory {
  ARState *state{nullptr};

  APITest() : INIT_TEMP_FILE_FACTORY(api_test) { this->state = ARState_create(); }

  ~APITest() override { ARState_delete(&this->state); }
};

struct Deleter {
  void operator()(ARError *e) const {
    ARError_release(e);
    delete e;
  }
};

static auto newError() { return std::unique_ptr<ARError, Deleter>(new ARError()); }

TEST_F(APITest, create) {
  ARState *st = ARState_createWithMode(static_cast<ARExecMode>(100));
  ASSERT_FALSE(st);
}

TEST_F(APITest, del) {
  ARState_delete(nullptr); // do nothing
}

TEST_F(APITest, mode) {
  ARExecMode modes[] = {
      AR_EXEC_MODE_NORMAL,
      AR_EXEC_MODE_PARSE_ONLY,
      AR_EXEC_MODE_CHECK_ONLY,
      AR_EXEC_MODE_COMPILE_ONLY,
  };
  for (auto &m : modes) {
    auto st = ARState_createWithMode(m);
    auto cleaup = arsh::finally([&] { ARState_delete(&st); });
    ASSERT_EQ(m, ARState_mode(st));
  }
  ASSERT_EQ(0, ARState_mode(nullptr));
}

TEST_F(APITest, version) {
  ARVersion version;
  const char *v = ARState_version(&version);

  ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, version.major);
  ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, version.minor);
  ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, version.patch);
  ASSERT_TRUE(v);
  ASSERT_TRUE(arsh::StringRef(v).startsWith("arsh, version "));

  v = ARState_version(nullptr);
  ASSERT_TRUE(v);
  ASSERT_TRUE(arsh::StringRef(v).startsWith("arsh, version "));
}

TEST_F(APITest, config) {
  arsh::StringRef value = ARState_config(this->state, AR_CONFIG_COMPILER);
  ASSERT_EQ(X_INFO_CPP " " X_INFO_CPP_V, value);

  value = ARState_config(this->state, AR_CONFIG_REGEX);
  ASSERT_TRUE(value.startsWith("PCRE"));

  value = ARState_config(this->state, AR_CONFIG_VERSION);
  {
    ARVersion version;
    ARState_version(&version);
    ASSERT_EQ(format("%d.%d.%d", version.major, version.minor, version.patch), value);
  }

  value = ARState_config(this->state, AR_CONFIG_OSTYPE);
  ASSERT_EQ(arsh::BUILD_OS, value);

  value = ARState_config(this->state, AR_CONFIG_MACHTYPE);
  ASSERT_EQ(arsh::BUILD_ARCH, value);

  value = ARState_config(this->state, AR_CONFIG_CONFIG_HOME);
  ASSERT_TRUE(value.endsWith("/arsh"));

  value = ARState_config(this->state, AR_CONFIG_DATA_HOME);
  ASSERT_TRUE(value.endsWith("/arsh"));

  value = ARState_config(this->state, AR_CONFIG_MODULE_HOME);
  {
    const char *base = ARState_config(this->state, AR_CONFIG_DATA_HOME);
    ASSERT_EQ(format("%s/modules", base), value);
  }

  value = ARState_config(this->state, AR_CONFIG_DATA_DIR);
  ASSERT_EQ(X_DATA_DIR, value);

  value = ARState_config(this->state, AR_CONFIG_MODULE_DIR);
  ASSERT_EQ(X_MODULE_DIR, value);

  // invalid
  const char *v = ARState_config(this->state, (ARConfig)9999);
  ASSERT_EQ(nullptr, v);

  v = ARState_config(nullptr, AR_CONFIG_MACHTYPE);
  ASSERT_EQ(nullptr, v);
}

TEST_F(APITest, lineNum1) {
  ASSERT_EQ(0u, ARState_lineNum(nullptr));
  ASSERT_EQ(1u, ARState_lineNum(this->state));

  const char *str = "12 + 32\n $true\n";
  ARState_eval(this->state, nullptr, str, strlen(str), nullptr);
  ASSERT_EQ(3u, ARState_lineNum(this->state));

  ARState_setLineNum(this->state, 49);
  str = "23";
  ARState_eval(this->state, nullptr, str, strlen(str), nullptr);
  ASSERT_EQ(50u, ARState_lineNum(this->state));

  ARState_setLineNum(nullptr, 1000);
  ASSERT_EQ(50u, ARState_lineNum(this->state));
}

TEST_F(APITest, lineNum2) {
  auto e = newError();
  auto fileName1 = this->createTempFile("target1.ds", "true\ntrue\n");
  ARState_loadAndEval(this->state, fileName1.c_str(), e.get());
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(1, ARState_lineNum(this->state)); // loadAndEval api does not update internal lineNum
  e = newError();

  fileName1 = this->createTempFile("targe2.ds", "45/'de'");
  ARState_loadAndEval(this->state, fileName1.c_str(), e.get());
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);
  ASSERT_EQ(1, e->lineNum);
  ASSERT_EQ(4, e->chars);
  ASSERT_EQ(1, ARState_lineNum(this->state)); // loadAndEval api does not update internal lineNum
  e = newError();
}

TEST_F(APITest, lineNum3) {
  const char *src = R"(
var a = 34

   $a += 45
echoechodwe \
    $a
)";
  auto e = newError();
  int s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(127, s);
  ASSERT_EQ(AR_ERROR_KIND_RUNTIME_ERROR, e->kind);
  ASSERT_EQ(5, e->lineNum);
  ASSERT_EQ(0, e->chars);
  e = newError();
}

TEST_F(APITest, shellName) {
  auto e = newError();
  const char *src = "assert $0 == 'arsh'";
  int s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  int r = ARState_setShellName(this->state, "12345");
  src = "assert $0 == '12345'";
  s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(0, r);
  e = newError();

  r = ARState_setShellName(this->state, nullptr); // do nothing
  src = "assert $0 == '12345'";
  s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(-1, r);
  e = newError();

  r = ARState_setShellName(nullptr, "ddd"); // do nothing
  ASSERT_EQ(0, r);
}

TEST_F(APITest, arg) {
  auto e = newError();
  const char *init = R"(
    assert $@.size() == 0
    assert $1.empty()
    assert $2.empty()
    assert $3.empty()
    assert $4.empty()
    assert $5.empty()
    assert $6.empty()
    assert $7.empty()
    assert $8.empty()
    assert $9.empty()
)";
  int s = ARState_eval(this->state, "(string)", init, strlen(init), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(0, e->chars);
  ASSERT_EQ(0, e->lineNum);
  e = newError();

  // set arguments
  ARState_setArguments(nullptr, nullptr); // do nothing
  ARState_setArguments(this->state, make_argv("a", "b", "c", "d", "e").data());

  const char *src = R"(
    assert $@.size() == 5
    assert $1 == 'a' && $1 == $@[0]
    assert $2 == 'b' && $2 == $@[1]
    assert $3 == 'c' && $3 == $@[2]
    assert $4 == 'd' && $4 == $@[3]
    assert $5 == 'e' && $5 == $@[4]
    assert $6.empty()
    assert $7.empty()
    assert $8.empty()
    assert $9.empty()
)";
  s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(0, e->chars);
  ASSERT_EQ(0, e->lineNum);
  e = newError();

  // clear
  ARState_setArguments(this->state, nullptr);
  s = ARState_eval(this->state, "(string)", init, strlen(init), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  // set arguments
  ARState_setArguments(
      this->state,
      make_argv("aa", "bb", "ccc", "ddd", "eee", "f", "ggg", "hhhh", "i", "100", "hey").data());
  src = R"(
    assert $@.size() == 11
    assert $1 == 'aa' && $1 == $@[0]
    assert $2 == 'bb' && $2 == $@[1]
    assert $3 == 'ccc' && $3 == $@[2]
    assert $4 == 'ddd' && $4 == $@[3]
    assert $5 == 'eee' && $5 == $@[4]
    assert $6 == 'f'   && $6 == $@[5]
    assert $7 == 'ggg' && $7 == $@[6]
    assert $8 == 'hhhh'&& $8 == $@[7]
    assert $9 == 'i' && $9 == $@[8]
    assert $@[9] == '100'
    assert $@[10] == 'hey'
    var old = $@  # save old arguments
)";
  s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(0, e->chars);
  ASSERT_EQ(0, e->lineNum);
  e = newError();

  // clear arguments, but still maintain old (allocate new object)
  ARState_setArguments(this->state, nullptr);
  src = R"(
    assert $old.size() == 11
    assert $old[0] == 'aa'
    assert $old[1] == 'bb'
    assert $old[2] == 'ccc'
    assert $old[3] == 'ddd'
    assert $old[4] == 'eee'
    assert $old[5] == 'f'
    assert $old[6] == 'ggg'
    assert $old[7] == 'hhhh'
    assert $old[8] == 'i'
    assert $old[9] == '100'
    assert $old[10] == 'hey'
    assert $@.empty()
)";
  s = ARState_eval(this->state, "(string)", src, strlen(src), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  ASSERT_EQ(0, e->chars);
  ASSERT_EQ(0, e->lineNum);
  e = newError();
}

TEST_F(APITest, size) {
  auto e = newError();
  int s = ARState_eval(this->state, "(string)", "echo hello", UINT32_MAX, e.get());
  ASSERT_EQ(1, s);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e->kind);
  ASSERT_EQ(0, e->chars);
  ASSERT_EQ(0, e->lineNum);
  ASSERT_STREQ("(string)", e->fileName);
  ASSERT_STREQ(strerror(EFBIG), e->name);
}

TEST_F(APITest, dump) {
  int s = ARState_setDumpTarget(this->state, AR_DUMP_KIND_AST, "./fjreijfreoai/jfraeijfriea/53452");
  ASSERT_EQ(-1, s);

  s = ARState_setDumpTarget(nullptr, AR_DUMP_KIND_AST, "hoge");
  ASSERT_EQ(-1, s);

  s = ARState_setDumpTarget(this->state, static_cast<ARDumpKind>(10000), "hoge");
  ASSERT_EQ(-1, s);
}

TEST_F(APITest, eval) {
  int ret = ARState_eval(nullptr, nullptr, "echo hello", strlen("echo hello"), nullptr);
  ASSERT_EQ(-1, ret);

  ret = ARState_eval(this->state, nullptr, nullptr, strlen("echo hello"), nullptr);
  ASSERT_EQ(-1, ret);

  ret = ARState_loadAndEval(this->state, nullptr, nullptr);
  ASSERT_EQ(-1, ret);

  ret = ARState_loadAndEval(nullptr, nullptr, nullptr);
  ASSERT_EQ(-1, ret);
}

TEST_F(APITest, option) {
  ASSERT_EQ(0, ARState_option(nullptr));
  ASSERT_EQ(AR_OPTION_ASSERT, ARState_option(this->state));
  ARState_unsetOption(this->state, AR_OPTION_ASSERT);
  ASSERT_EQ(0, ARState_option(this->state));
  ARState_setOption(nullptr, 0);   // do nothing
  ARState_unsetOption(nullptr, 0); // do nothing

  const unsigned short option =
      AR_OPTION_ASSERT | AR_OPTION_INTERACTIVE | AR_OPTION_TRACE_EXIT | AR_OPTION_JOB_CONTROL;
  ARState_setOption(this->state, option);
  ASSERT_EQ(option, ARState_option(this->state));
  ARState_unsetOption(this->state, AR_OPTION_TRACE_EXIT);
  ASSERT_EQ(AR_OPTION_ASSERT | AR_OPTION_INTERACTIVE | AR_OPTION_JOB_CONTROL,
            ARState_option(this->state));
  ARState_unsetOption(this->state, option);
  ASSERT_EQ(0, ARState_option(this->state));
}

TEST_F(APITest, status) {
  int s = ARState_exitStatus(this->state);
  ASSERT_EQ(0, s); // initial exit status is 0

  std::string src = "$? = 98";
  int ret = ARState_eval(this->state, "", src.c_str(), src.size(), nullptr);
  ASSERT_EQ(98, ret);
  ASSERT_EQ(ret, ARState_exitStatus(this->state));

  // truncate
  src = "$? = 9876";
  ret = ARState_eval(this->state, "", src.c_str(), src.size(), nullptr);
  ASSERT_EQ(148, ret);
  ASSERT_EQ(ret, ARState_exitStatus(this->state));

  // negative number
  src = "$? = -1";
  ret = ARState_eval(this->state, "", src.c_str(), src.size(), nullptr);
  ASSERT_EQ(255, ret);
  ASSERT_EQ(ret, ARState_exitStatus(this->state));
}

TEST_F(APITest, abort) {
  std::string src = "function f( $a : String, $b : String) : Bool { exit 54; }";
  auto e = newError();
  int s = ARState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);

  src = R"(["d", ""].sortBy($f))";
  e = newError();
  s = ARState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(1, s);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);

  src = R"(["d", ""].sortBy($f))";
  e = newError();
  s = ARState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(1, s);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);

  src = R"(function g( $a : String, $b : String) : Int { exit 54; })";
  e = newError();
  s = ARState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);

  src = R"(["d", ""].sortBy($g))";
  e = newError();
  s = ARState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(54, s);
  ASSERT_EQ(AR_ERROR_KIND_EXIT, e->kind);
}

TEST_F(APITest, pid) {
  pid_t pid = getpid();
  std::string src("assert($$ == ");
  src += std::to_string(pid);
  src += ")";

  auto e = newError();
  int s = ARState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  auto kind = e->kind;
  e = newError();
  ASSERT_EQ(0, s);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, kind);
}

TEST_F(APITest, load1) {
  auto e = newError();
  int r = ARState_loadAndEval(this->state, "hogehuga", e.get());
  int errorNum = errno;
  ASSERT_EQ(1, r);
  ASSERT_EQ(ENOENT, errorNum);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e->kind);

  e = newError();
}

TEST_F(APITest, load2) {
  auto e = newError();
  int r = ARState_loadAndEval(this->state, ".", e.get());
  int errorNum = errno;
  ASSERT_EQ(1, r);
  ASSERT_EQ(EISDIR, errorNum);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e->kind);

  e = newError();
}

TEST_F(APITest, load3) {
  auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

  auto e = newError();
  int r = ARState_loadAndEval(this->state, modName.c_str(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  r = ARState_loadAndEval(this->state, modName.c_str(), e.get()); // file is already loaded
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();
}

TEST_F(APITest, load4) {
  auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

  std::string line = "source ";
  line += modName;

  auto e = newError();
  int r = ARState_eval(this->state, "(string)", line.c_str(), line.size(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  r = ARState_loadAndEval(this->state, modName.c_str(), e.get()); // file is already loaded
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();
}

TEST_F(APITest, load5) {
  auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

  auto e = newError();
  int r = ARState_loadAndEval(this->state, modName.c_str(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  // use loaded module
  std::string line = format(R"(
true
source %s as m
assert $m.mod_load_success
)",
                            modName.c_str());

  r = ARState_eval(this->state, "(string)", line.c_str(), line.size(), e.get());
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();
}

TEST_F(APITest, load6) {
  auto modName = this->createTempFile("mod.ds", "var aaa = 34; \nexit $aaa");

  auto e = newError();
  int r = ARState_loadAndEval(this->state, modName.c_str(), e.get());
  ASSERT_EQ(34, r);
  ASSERT_EQ(AR_ERROR_KIND_EXIT, e->kind);
  ASSERT_EQ(2, e->lineNum);
  e = newError();

  // use loaded module
  std::string line = format(R"(
source %s as m    # last statement of loaded module is Nothing type
echo hello
)",
                            modName.c_str());

  r = ARState_eval(this->state, "(string)", line.c_str(), line.size(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);
  ASSERT_STREQ("Unreachable", e->name);
  ASSERT_EQ(3, e->lineNum);
  ASSERT_EQ(1, e->chars);
  e = newError();
}

TEST_F(APITest, cmdfallback) {
  auto modName = this->createTempFile("mod.ds", R"(
  $CMD_FALLBACK = function(m,a) => {
    echo $m $a;
    ($m as Any) as Module
    assert ($m as Any) is Module
    $? = 99;
    $? == 0
  };
)");

  auto e = newError();
  int r = ARState_loadAndEval(this->state, modName.c_str(), e.get());
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();
  r = ARState_exec(this->state, make_argv("jfirejfoaei").data());
  ASSERT_EQ(99, r);
}

template <typename Func>
static Output invoke(Func func) {
  IOConfig config;
  config.out = IOConfig::PIPE;
  config.err = IOConfig::PIPE;

  return ProcBuilder::spawn(config, [func]() { return func(); }).waitAndGetResult(true);
}

TEST_F(APITest, scriptDir) {
  std::string text = R"(
  function check(v : String) : Bool {
    case $v {
    $SCRIPT_DIR => return true
    else => return false
    }
  }

  function test1() : Bool {
    return $check($SCRIPT_DIR)
  }
  function test2() : Bool {
    return $check($MODULE._scriptDir())
  }
  assert $test1()
  assert $test2()
)";

  auto error = newError();
  int ret = ARState_eval(this->state, "(string)", text.c_str(), text.size(), error.get());
  ASSERT_EQ(0, ret);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, error->kind);
  error = newError();

  /**
   * after change CWD, SCRIPT_DIR is also changed with CWD.
   * as a result, runtiem SCRIPT_DIR and compile runtime SCRIPT_DIR are different
   */
  auto output = invoke([&] {
    if (chdir(this->getTempDirName()) != 0) {
      fatal_perror("chdir failed");
    }
    text = "assert $test1()";
    ret = ARState_eval(this->state, "(string)", text.c_str(), text.size(), error.get());
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_EQ(1, output.status.value);
  ASSERT_EQ("", output.out);
  ASSERT_EQ(R"([runtime error]
AssertionFailed: `$test1()'
    from (string):17 '<toplevel>()')",
            output.err);

  output = invoke([&] {
    if (chdir(this->getTempDirName()) != 0) {
      fatal_perror("chdir failed");
    }
    text = "assert $test2()";
    ret = ARState_eval(this->state, "(string)", text.c_str(), text.size(), error.get());
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_EQ(1, output.status.value);
  ASSERT_EQ("", output.out);
  ASSERT_EQ(R"([runtime error]
AssertionFailed: `$test2()'
    from (string):17 '<toplevel>()')",
            output.err);

  output = invoke([&] {
    if (chdir(this->getTempDirName()) != 0) {
      fatal_perror("chdir failed");
    }
    text = "assert $check($MODULE._scriptDir())";
    ret = ARState_eval(this->state, "(string)", text.c_str(), text.size(), error.get());
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_EQ(1, output.status.value);
  ASSERT_EQ("", output.out);
  ASSERT_EQ(R"([runtime error]
AssertionFailed: `$check($MODULE._scriptDir())'
    from (string):17 '<toplevel>()')",
            output.err);

  output = invoke([&] {
    if (chdir(this->getTempDirName()) != 0) {
      fatal_perror("chdir failed");
    }
    text = "assert $check($SCRIPT_DIR)";
    ret = ARState_eval(this->state, "(string)", text.c_str(), text.size(), error.get());
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_EQ(1, output.status.value);
  ASSERT_EQ("", output.out);
  ASSERT_EQ(R"([runtime error]
AssertionFailed: `$check($SCRIPT_DIR)'
    from (string):17 '<toplevel>()')",
            output.err);
}

TEST_F(APITest, module1) {
  int ret = ARState_loadModule(nullptr, "helllo", 0, nullptr);
  ASSERT_EQ(-1, ret);

  ret = ARState_loadModule(this->state, nullptr, 0, nullptr);
  ASSERT_EQ(-1, ret);
}

TEST_F(APITest, module2) {
  auto ret = invoke([&] {
    int ret = ARState_loadModule(this->state, "fhjreuhfurie", 0, nullptr);
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(
      this->expect(ret, 1, WaitStatus::EXITED, "",
                   "arsh: cannot load file: fhjreuhfurie, by `No such file or directory'"));

  auto e = newError();
  int r = ARState_loadModule(this->state, "fhuahfuiefer", 0, e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e->kind);
  ASSERT_STREQ(strerror(ENOENT), e->name);
  ASSERT_EQ(0, e->lineNum);
  ASSERT_EQ(0, e->chars);
  e = newError();

  ret = invoke([&] {
    int ret = ARState_loadModule(this->state, "fhjreuhfurie", AR_MOD_IGNORE_ENOENT, nullptr);
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 0, WaitStatus::EXITED));
}

TEST_F(APITest, module3) {
  auto fileName = this->createTempFile("target.ds", "var OK_LOADING = true");

  int r = ARState_loadModule(this->state, fileName.c_str(), 0, nullptr);
  ASSERT_EQ(0, r);
  std::string src = "assert $OK_LOADING";
  r = ARState_eval(this->state, "(string)", src.c_str(), src.size(), nullptr);
  ASSERT_EQ(0, r);
}

TEST_F(APITest, module4) {
  auto fileName = this->createTempFile("target.ds", "source  hoghreua");
  auto e = newError();
  int r = ARState_loadModule(this->state, fileName.c_str(), AR_MOD_FULLPATH | AR_MOD_IGNORE_ENOENT,
                             e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);
  ASSERT_STREQ("NotFoundMod", e->name);
  ASSERT_EQ(1, e->lineNum);
  ASSERT_EQ(9, e->chars);
  e = newError();

  // check error message
  auto ret = invoke([&] {
    int ret = ARState_loadModule(this->state, fileName.c_str(),
                                 AR_MOD_FULLPATH | AR_MOD_IGNORE_ENOENT, nullptr);
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(
      ret, 1, WaitStatus::EXITED, "", "^\\[semantic error\\] module not found: `hoghreua'.+$"));
}

TEST_F(APITest, module5) {
  auto e = newError();
  int r = ARState_loadModule(this->state, "hfeurhfiurhefuie", AR_MOD_FULLPATH, e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e->kind);
  ASSERT_STREQ(strerror(ENOENT), e->name);
  ASSERT_EQ(0, e->lineNum);
  ASSERT_EQ(0, e->chars);
  e = newError();

  // check error message
  auto ret = invoke([&] {
    int ret = ARState_loadModule(this->state, "freijjfeir", AR_MOD_FULLPATH, nullptr);
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(
      this->expect(ret, 1, WaitStatus::EXITED, "",
                   "arsh: cannot load file: freijjfeir, by `No such file or directory'"));
}

TEST_F(APITest, module6) {
  if (arsh::platform::isCygwinOrMsys(arsh::platform::platform())) {
    return;
  }

  auto fileName = this->createTempFile(R"(ss$ho"\hgoe
        \ \
)",
                                       "echo moduel!!; exit 56");
  auto ret = invoke([&] {
    int ret = ARState_loadModule(this->state, fileName.c_str(), AR_MOD_FULLPATH, nullptr);
    ARState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 56, WaitStatus::EXITED, "moduel!!"));
}

TEST_F(APITest, module7) {
  auto fileName = this->createTempFile("mod1", "var AAA = 34");
  auto e = newError();
  int r = ARState_loadModule(this->state, fileName.c_str(), AR_MOD_FULLPATH, e.get());
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  fileName = this->createTempFile("mod2", "var AAA = $false");
  r = ARState_loadModule(this->state, fileName.c_str(), AR_MOD_FULLPATH, e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(1, e->lineNum);
  ASSERT_EQ(1, e->chars);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);
  e = newError();
}

TEST_F(APITest, moduleLimit) {
  constexpr unsigned int limit = 0x8FFF;
  const unsigned int digits = arsh::countDigits(limit);
  for (unsigned int i = 0; i < limit; i++) {
    std::string name = "mod_";
    name += arsh::padLeft(i, digits, '0');
    this->createTempFile(name.c_str(), "true");
  }

  const char *dir = this->getTempDirName();
  std::string source = format(R"(
source %s/mod_{0..04095}
source %s/mod_{04096..8191}
source %s/mod_{08192..12286}
source %s/mod_{12287..16381}
source %s/mod_{16381..20476}
source %s/mod_{20477..24571}
source %s/mod_{24572..28666}
source %s/mod_{28667..32761}
source %s/mod_{32762..32764}   # max module num is INT16_MAX (include builtin, root module)
)",
                              dir, dir, dir, dir, dir, dir, dir, dir, dir);

  auto e = newError();
  int r = ARState_eval(this->state, "(string)", source.c_str(), source.size(), e.get());
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  source = format("source %s/mod_32765", dir);
  r = ARState_eval(this->state, "(string)", source.c_str(), source.size(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);
  ASSERT_STREQ("ModLimit", e->name);
  ASSERT_EQ(11, e->lineNum);
  ASSERT_EQ(8, e->chars);
  e = newError();

  // load module directly
  std::string src = dir;
  src += "/mod_32770";

  r = ARState_loadAndEval(this->state, src.c_str(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e->kind);
  ASSERT_STREQ(strerror(EPERM), e->name);
  e = newError();
}

TEST_F(APITest, globalLimit) {
  constexpr unsigned int limit = 0x8FFF;
  const unsigned int digits = arsh::countDigits(limit);
  for (unsigned int i = 0; i < limit; i++) {
    std::string name = "mod_";
    name += arsh::padLeft(i, digits, '0');
    std::string value = "var var_";
    value += name;
    value += " = true";
    static_cast<void>(this->createTempFile(name.c_str(), value));
  }

  const char *dir = this->getTempDirName();
  std::string source = format(R"(
source %s/mod_{0..04095}
source %s/mod_{04096..8191}
source %s/mod_{08192..12286}
source %s/mod_{12287..16381}
source %s/mod_{16381..20476}
source %s/mod_{20477..24571}
source %s/mod_{24572..28666}
)",
                              dir, dir, dir, dir, dir, dir, dir);

  auto e = newError();
  int r = ARState_eval(this->state, "(string)", source.c_str(), source.size(), e.get());
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e->kind);
  e = newError();

  source = format("source %s/mod_{28667..32760}", dir);
  r = ARState_eval(this->state, "(string)", source.c_str(), source.size(), e.get());
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_TYPE_ERROR, e->kind);
  ASSERT_STREQ("GlobalLimit", e->name);
  ASSERT_EQ(1, e->lineNum);
  ASSERT_EQ(5, e->chars);
  // ASSERT_EQ(9, e->lineNum);
  // ASSERT_EQ(8, e->chars);
  e = newError();
}

struct Executor {
  std::string str;
  bool jobctrl;
  std::unordered_map<std::string, std::string> envs;

  Executor(std::string &&str, bool jobctrl) : str(std::move(str)), jobctrl(jobctrl) {}

  Executor &env(const char *name, const char *value) {
    this->envs.insert({name, value});
    return *this;
  }

  ProcHandle operator()() const {
    IOConfig config{IOConfig::INHERIT, IOConfig::PIPE, IOConfig::PIPE};
    return ProcBuilder::spawn(config, [&] {
      for (auto &e : this->envs) {
        setenv(e.first.c_str(), e.second.c_str(), 1);
      }

      ARState *state = ARState_create();
      if (this->jobctrl) {
        ARState_setOption(state, AR_OPTION_JOB_CONTROL);
      }
      int ret = ARState_eval(state, "(string)", this->str.c_str(), this->str.size(), nullptr);
      ARState_delete(&state);
      return ret;
    });
  }
};

static ProcHandle exec(std::string &&str, bool jobControl = true) {
  return Executor(std::move(str), jobControl)();
}

#define EXEC(...) exec(format(__VA_ARGS__)).waitAndGetResult(true)
#define EXEC2(...) exec(format(__VA_ARGS__), false).waitAndGetResult(true)

struct PIDs {
  pid_t pid;
  pid_t ppid;
  pid_t pgid;
};

static std::vector<std::string> split(const std::string &str) { return split(str, ' '); }

static std::vector<PIDs> decompose(const std::string &str) {
  auto ss = split(str);
  std::vector<PIDs> ret(ss.size());

  for (unsigned int i = 0; i < ret.size(); i++) {
    int r = Extractor(ss[i].c_str())("[", ret[i].pid, ",", ret[i].ppid, ",", ret[i].pgid, "]");
    if (r != 0) {
      fatal("broken\n");
    }
  }
  return ret;
}

struct JobTest : public ExpectOutput {};

#define PATTERN "\\[[0-9]+,[0-9]+,[0-9]+\\]"
#define PATTERN2 PATTERN " " PATTERN
#define PATTERN3 PATTERN " " PATTERN " " PATTERN

TEST_F(JobTest, pid1) { // enable job control
  // normal
  auto result = EXEC("%s --first | %s | %s", PID_CHECK_PATH, PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN3));
  auto pids = decompose(result.out);
  ASSERT_EQ(3u, pids.size());

  ASSERT_EQ(pids[0].ppid, pids[1].ppid);
  ASSERT_EQ(pids[1].ppid, pids[2].ppid);
  ASSERT_EQ(pids[2].ppid, pids[0].ppid);

  ASSERT_EQ(pids[0].pid, pids[0].pgid);
  ASSERT_EQ(pids[0].pid, pids[1].pgid);
  ASSERT_EQ(pids[0].pid, pids[2].pgid);

  // command, call
  result = EXEC("command call %s --first | call command %s", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_EQ(pids[0].ppid, pids[1].ppid);

  ASSERT_EQ(pids[0].pid, pids[0].pgid);
  ASSERT_EQ(pids[0].pid, pids[1].pgid);

  // udc1
  result =
      EXEC("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_NE(pids[0].ppid, pids[1].ppid);

  ASSERT_EQ(pids[0].pid, pids[0].pgid);
  ASSERT_EQ(pids[0].pid, pids[1].pgid);

  // udc2
  result =
      EXEC("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_NE(pids[0].ppid, pids[1].ppid);

  ASSERT_NE(pids[0].pid, pids[0].pgid);
  ASSERT_NE(pids[0].pid, pids[1].pgid);
  ASSERT_EQ(pids[0].pgid, pids[1].pgid);

  // last pipe
  result = EXEC("%s --first | { %s; }", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_EQ(pids[0].ppid, pids[1].ppid);

  ASSERT_EQ(pids[0].pid, pids[0].pgid);
  ASSERT_NE(pids[0].pid, pids[1].pgid);
  ASSERT_NE(pids[0].pgid, pids[1].pgid);
}

TEST_F(JobTest, pid2) { // disable job control
  // normal
  auto result = EXEC2("%s --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  auto pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_EQ(pids[0].ppid, pids[1].ppid);
  ASSERT_NE(pids[0].pid, pids[0].pgid);
  ASSERT_NE(pids[1].pid, pids[1].pgid);
  ASSERT_EQ(pids[0].pgid, pids[1].pgid);

  // udc1
  result =
      EXEC2("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_NE(pids[0].ppid, pids[1].ppid);
  ASSERT_NE(pids[0].pid, pids[0].pgid);
  ASSERT_NE(pids[1].pid, pids[1].pgid);
  ASSERT_EQ(pids[0].pgid, pids[1].pgid);

  // udc2
  result =
      EXEC2("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_NE(pids[0].ppid, pids[1].ppid);
  ASSERT_NE(pids[0].pid, pids[0].pgid);
  ASSERT_NE(pids[1].pid, pids[1].pgid);
  ASSERT_EQ(pids[0].pgid, pids[1].pgid);

  // last pipe
  result = EXEC("%s --first | { %s; }", PID_CHECK_PATH, PID_CHECK_PATH);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
  pids = decompose(result.out);
  ASSERT_EQ(2u, pids.size());

  ASSERT_EQ(pids[0].ppid, pids[1].ppid);

  ASSERT_EQ(pids[0].pid, pids[0].pgid);
  ASSERT_NE(pids[0].pid, pids[1].pgid);
  ASSERT_NE(pids[0].pgid, pids[1].pgid);
}

#undef EXEC
#define EXEC(S) exec(std::string(S)).waitAndGetResult(true)

TEST_F(JobTest, jobctrl1) {
  // invalid
  auto result = EXEC("fg");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "(string):1: fg: current: no such job"));

  result = EXEC("fg %hoge");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "(string):1: fg: %hoge: no such job"));

  result = EXEC("fg %1");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "(string):1: fg: %1: no such job"));

  const char *str = R"(
        sh -c 'kill -s STOP $$; exit 180'
        assert $? == 128 + $SIGSTOP.value()
        assert { fg %1; $?; } == 180 : "$?"
        fg %1
)";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(this->expect(result, 1, WaitStatus::EXITED,
                                       "sh -c kill -s STOP $$; exit 180",
                                       "[1] + Stopped  sh -c kill -s STOP $$; exit 180\n"
                                       "(string):5: fg: %1: no such job"));

  str = R"(
        sh -c 'kill -s STOP $$; exit 18'
        assert $? == 128 + $SIGSTOP.value()
        fg
)";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(this->expect(result, 18, WaitStatus::EXITED,
                                       "sh -c kill -s STOP $$; exit 18",
                                       "[1] + Stopped  sh -c kill -s STOP $$; exit 18"));
}

TEST_F(JobTest, jobctrl2) {
  // invalid
  auto result = EXEC("bg");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "(string):1: bg: current: no such job"));

  result = EXEC("bg hoge %1");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "",
                   "(string):1: bg: hoge: no such job\n(string):1: bg: %1: no such job"));

  const char *str = R"(
var j = {
     $SIGSTOP.kill($PID)
     exit 99
} &
sleep 0.5
assert $j.wait() == 128 + $SIGSTOP.value()
assert $j.poll()
assert { bg; $?; } == 0
sleep 0.5
var r = $j.wait()
assert $r == 99 : $r as String
        true
)";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(this->expect(result, 0, WaitStatus::EXITED,
                                       "[1]  {\\n     $SIGSTOP.kill($PID)\\n     exit 99\\n}"));

  str = R"(
var j = {
     $SIGSTOP.kill($PID)
     exit 99
} &
sleep 0.5
assert $j.wait() == 128 + $SIGSTOP.value()
assert $j.poll()
assert { bg %1 %2; $?; } == 1
sleep 0.5
var r = $j.wait()
assert $r == 99 : $r as String
true
  )";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(this->expect(result, 0, WaitStatus::EXITED,
                                       "[1]  {\\n     $SIGSTOP.kill($PID)\\n     exit 99\\n}",
                                       "(string):9: bg: %2: no such job"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
