#include "gtest/gtest.h"

#include <array>

#include <config.h>
#include <constant.h>
#include <misc/fatal.h>
#include <misc/format.hpp>
#include <misc/resource.hpp>
#include <ydsh/ydsh.h>

#include <pwd.h>
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
  DSState *state = DSState_create();
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("echo", "hello").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, case2) {
  DSState *state = DSState_create();
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("./", "hello").data());
  ASSERT_EQ(126, ret);
}

TEST(BuiltinExecTest, case3) {
  DSState *state = DSState_create();
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("exit", "12000").data());
  ASSERT_EQ(224, ret);
}

TEST(BuiltinExecTest, case4) {
  DSState *state = DSState_create();
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, nullptr);
  ASSERT_EQ(-1, ret);

  ret = DSState_exec(nullptr, make_argv("exit", "12000").data());
  ASSERT_EQ(-1, ret);
}

TEST(BuiltinExecTest, shctl) {
  DSState *state = DSState_create();
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("shctl", "is-interactive").data());
  ASSERT_EQ(1, ret);

  DSState_setOption(state, DS_OPTION_INTERACTIVE);
  ret = DSState_exec(state, make_argv("shctl", "is-interactive").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, donothing1) {
  DSState *state = DSState_createWithMode(DS_EXEC_MODE_CHECK_ONLY);
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("jfriejfieori").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, donothing2) {
  DSState *state = DSState_createWithMode(DS_EXEC_MODE_PARSE_ONLY);
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("jfriejfieori").data());
  ASSERT_EQ(0, ret);
}

TEST(BuiltinExecTest, donothing3) {
  DSState *state = DSState_createWithMode(DS_EXEC_MODE_COMPILE_ONLY);
  auto cleanup = ydsh::finally([&] { DSState_delete(&state); });

  int ret = DSState_exec(state, make_argv("jfriejfieori").data());
  ASSERT_EQ(0, ret);
}

struct APITest : public ExpectOutput, public ydsh::TempFileFactory {
  DSState *state{nullptr};

  APITest() : INIT_TEMP_FILE_FACTORY(api_test) { this->state = DSState_create(); }

  ~APITest() override { DSState_delete(&this->state); }
};

TEST_F(APITest, create) {
  DSState *st = DSState_createWithMode(static_cast<DSExecMode>(100));
  ASSERT_FALSE(st);
}

TEST_F(APITest, del) {
  DSState_delete(nullptr); // do nothing
}

TEST_F(APITest, mode) {
  DSExecMode modes[] = {
      DS_EXEC_MODE_NORMAL,
      DS_EXEC_MODE_PARSE_ONLY,
      DS_EXEC_MODE_CHECK_ONLY,
      DS_EXEC_MODE_COMPILE_ONLY,
  };
  for (auto &m : modes) {
    auto st = DSState_createWithMode(m);
    auto cleaup = ydsh::finally([&] { DSState_delete(&st); });
    ASSERT_EQ(m, DSState_mode(st));
  }
  ASSERT_EQ(0, DSState_mode(nullptr));
}

TEST_F(APITest, version) {
  DSVersion version;
  const char *v = DSState_version(&version);

  ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, version.major);
  ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, version.minor);
  ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, version.patch);
  ASSERT_TRUE(v);
  ASSERT_TRUE(ydsh::StringRef(v).startsWith("ydsh, version "));

  v = DSState_version(nullptr);
  ASSERT_TRUE(v);
  ASSERT_TRUE(ydsh::StringRef(v).startsWith("ydsh, version "));
}

TEST_F(APITest, config) {
  ydsh::StringRef value = DSState_config(this->state, DS_CONFIG_COMPILER);
  ASSERT_EQ(X_INFO_CPP " " X_INFO_CPP_V, value);

  value = DSState_config(this->state, DS_CONFIG_REGEX);
  ASSERT_TRUE(value.startsWith("PCRE"));

  value = DSState_config(this->state, DS_CONFIG_VERSION);
  {
    DSVersion version;
    DSState_version(&version);
    ASSERT_EQ(format("%d.%d.%d", version.major, version.minor, version.patch), value);
  }

  value = DSState_config(this->state, DS_CONFIG_OSTYPE);
  {
    struct utsname name {};
    uname(&name);
    ASSERT_EQ(name.sysname, value);
  }

  value = DSState_config(this->state, DS_CONFIG_MACHTYPE);
  ASSERT_EQ(ydsh::BUILD_ARCH, value);

  value = DSState_config(this->state, DS_CONFIG_CONFIG_HOME);
  ASSERT_TRUE(value.endsWith("/ydsh"));

  value = DSState_config(this->state, DS_CONFIG_DATA_HOME);
  ASSERT_TRUE(value.endsWith("/ydsh"));

  value = DSState_config(this->state, DS_CONFIG_MODULE_HOME);
  {
    const char *base = DSState_config(this->state, DS_CONFIG_DATA_HOME);
    ASSERT_EQ(format("%s/module", base), value);
  }

  value = DSState_config(this->state, DS_CONFIG_DATA_DIR);
  ASSERT_EQ(X_DATA_DIR, value);

  value = DSState_config(this->state, DS_CONFIG_MODULE_DIR);
  ASSERT_EQ(X_MODULE_DIR, value);

  // invlaid
  const char *v = DSState_config(this->state, (DSConfig)9999);
  ASSERT_EQ(nullptr, v);

  v = DSState_config(nullptr, DS_CONFIG_MACHTYPE);
  ASSERT_EQ(nullptr, v);
}

TEST_F(APITest, lineNum1) {
  ASSERT_EQ(0u, DSState_lineNum(nullptr));
  ASSERT_EQ(1u, DSState_lineNum(this->state));

  const char *str = "12 + 32\n $true\n";
  DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
  ASSERT_EQ(3u, DSState_lineNum(this->state));

  DSState_setLineNum(this->state, 49);
  str = "23";
  DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
  ASSERT_EQ(50u, DSState_lineNum(this->state));

  DSState_setLineNum(nullptr, 1000);
  ASSERT_EQ(50u, DSState_lineNum(this->state));
}

TEST_F(APITest, lineNum2) {
  DSError e;
  auto fileName1 = this->createTempFile("target1.ds", "true\ntrue\n");
  DSState_loadAndEval(this->state, fileName1.c_str(), &e);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  ASSERT_EQ(3, DSState_lineNum(this->state));
  DSError_release(&e);

  fileName1 = this->createTempFile("targe2.ds", "45/'de'");
  DSState_loadAndEval(this->state, fileName1.c_str(), &e);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
  ASSERT_EQ(1, e.lineNum);
  ASSERT_EQ(4, e.chars);
  ASSERT_EQ(2, DSState_lineNum(this->state));
  DSError_release(&e);
}

TEST_F(APITest, lineNum3) {
  const char *src = R"(
var a = 34

   $a += 45
echoechodwe \
    $a
)";
  DSError e;
  int s = DSState_eval(this->state, "(string)", src, strlen(src), &e);
  ASSERT_EQ(127, s);
  ASSERT_EQ(DS_ERROR_KIND_RUNTIME_ERROR, e.kind);
  ASSERT_EQ(5, e.lineNum);
  ASSERT_EQ(0, e.chars);
  DSError_release(&e);
}

TEST_F(APITest, shellName) {
  DSError e;
  const char *src = "assert $0 == 'ydsh'";
  int s = DSState_eval(this->state, "(string)", src, strlen(src), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  DSState_setShellName(this->state, "12345");
  src = "assert $0 == '12345'";
  s = DSState_eval(this->state, "(string)", src, strlen(src), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  DSState_setShellName(this->state, nullptr); // do nothing
  src = "assert $0 == '12345'";
  s = DSState_eval(this->state, "(string)", src, strlen(src), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  DSState_setShellName(nullptr, "ddd"); // do nothing
}

TEST_F(APITest, arg) {
  DSError e;
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
  int s = DSState_eval(this->state, "(string)", init, strlen(init), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  ASSERT_EQ(0, e.chars);
  ASSERT_EQ(0, e.lineNum);
  DSError_release(&e);

  // set arguments
  DSState_setArguments(nullptr, nullptr); // do nothing
  DSState_setArguments(this->state, make_argv("a", "b", "c", "d", "e").data());

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
  s = DSState_eval(this->state, "(string)", src, strlen(src), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  ASSERT_EQ(0, e.chars);
  ASSERT_EQ(0, e.lineNum);
  DSError_release(&e);

  // clear
  DSState_setArguments(this->state, nullptr);
  s = DSState_eval(this->state, "(string)", init, strlen(init), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  // set arguments
  DSState_setArguments(
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
)";
  s = DSState_eval(this->state, "(string)", src, strlen(src), &e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  ASSERT_EQ(0, e.chars);
  ASSERT_EQ(0, e.lineNum);
  DSError_release(&e);
}

TEST_F(APITest, dump) {
  int s = DSState_setDumpTarget(this->state, DS_DUMP_KIND_AST, "./fjreijfreoai/jfraeijfriea/53452");
  ASSERT_EQ(-1, s);

  s = DSState_setDumpTarget(nullptr, DS_DUMP_KIND_AST, "hoge");
  ASSERT_EQ(-1, s);

  s = DSState_setDumpTarget(this->state, static_cast<DSDumpKind>(10000), "hoge");
  ASSERT_EQ(-1, s);
}

TEST_F(APITest, eval) {
  int ret = DSState_eval(nullptr, nullptr, "echo hello", strlen("echo hello"), nullptr);
  ASSERT_EQ(-1, ret);

  ret = DSState_eval(this->state, nullptr, nullptr, strlen("echo hello"), nullptr);
  ASSERT_EQ(-1, ret);

  ret = DSState_loadAndEval(this->state, nullptr, nullptr);
  ASSERT_EQ(-1, ret);

  ret = DSState_loadAndEval(nullptr, nullptr, nullptr);
  ASSERT_EQ(-1, ret);
}

TEST_F(APITest, prompt1) {
  const char *buf = "";
  unsigned int s = DSState_lineEdit(this->state, static_cast<DSLineEditOp>(100000), 1, &buf);
  ASSERT_EQ(0, s);

  s = DSState_lineEdit(nullptr, DS_EDIT_PROMPT, 1, &buf);
  ASSERT_EQ(0, s);

  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 1, nullptr);
  ASSERT_EQ(0, s);

  // default prompt
  buf = "fer";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 1, &buf);
  ASSERT_EQ(0, s);

  std::string p = "ydsh-";
  p += std::to_string(X_INFO_MAJOR_VERSION);
  p += ".";
  p += std::to_string(X_INFO_MINOR_VERSION);
  p += getuid() == 0 ? "# " : "$ ";
  ASSERT_EQ(p, buf);

  buf = "hhhhh";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 2, &buf);
  ASSERT_EQ(0, s);
  ASSERT_STREQ("> ", buf);

  buf = "farjhfu";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 5, &buf);
  ASSERT_EQ(0, s);
  ASSERT_STREQ("", buf);

  buf = "fjrie";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, -82, &buf);
  ASSERT_EQ(0, s);
  ASSERT_STREQ("", buf);

  // use module
  const char *str = "source " API_TEST_WORK_DIR "/../../share/ydsh/module/edit;\n"
                    "source " API_TEST_WORK_DIR "/../../share/ydsh/module/prompt;\n"
                    "$PS1 = 'hello>'; $PS2 = 'second>'";
  int r = DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
  ASSERT_EQ(0, r);

  buf = "fjrie";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 1, &buf);
  ASSERT_EQ(1, s);
  ASSERT_STREQ("hello>", buf);

  buf = "fjrie";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 2, &buf);
  ASSERT_EQ(1, s);
  ASSERT_STREQ("second>", buf);

  buf = "fjrie";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, -82, &buf);
  ASSERT_EQ(1, s);
  ASSERT_STREQ("", buf);

  buf = "fjrie";
  s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 2, nullptr);
  ASSERT_EQ(0, s);
}

TEST_F(APITest, prompt2) {
  std::string defaultPrompt = "ydsh-";
  defaultPrompt += std::to_string(X_INFO_MAJOR_VERSION);
  defaultPrompt += ".";
  defaultPrompt += std::to_string(X_INFO_MINOR_VERSION);
  defaultPrompt += getuid() == 0 ? "# " : "$ ";

  const char *str = R"(
  $EDIT_HOOK = function($a : Int, $b : Int, $c : String) => 34/0
)";
  DSState_eval(this->state, nullptr, str, strlen(str), nullptr);

  const char *buf = "";
  unsigned int s = DSState_lineEdit(this->state, DS_EDIT_PROMPT, 1, &buf);
  ASSERT_EQ(0, s);
  ASSERT_EQ(defaultPrompt, buf);
}

static std::vector<std::string> tilde() {
  std::vector<std::string> v;
  setpwent();

  for (decltype(getpwent()) entry = nullptr; (entry = getpwent()) != nullptr;) {
    std::string str = "~";
    str += entry->pw_name;
    str += "/";
    v.push_back(std::move(str));
  }

  endpwent();

  std::sort(v.begin(), v.end());
  auto iter = std::unique(v.begin(), v.end());
  v.erase(iter, v.end());

  return v;
}

static std::vector<std::string> filter(const std::vector<std::string> &v, const char *cond) {
  std::vector<std::string> t;
  for (auto &e : v) {
    if (strstr(e.c_str(), cond) != nullptr) {
      t.push_back(e);
    }
  }
  return t;
}

TEST_F(APITest, complete) {
  // null arguments
  unsigned int s = DSState_complete(nullptr, DS_COMP_INVOKE, 1, nullptr); // do nothing
  ASSERT_EQ(0, s);

  const char *line = "echo ~";
  s = DSState_complete(this->state, DS_COMP_INVOKE, 0, &line);
  ASSERT_EQ(0, s);

  s = DSState_complete(this->state, DS_COMP_INVOKE, 6, nullptr);
  ASSERT_EQ(0, s);

  const char *ss = nullptr;
  s = DSState_complete(this->state, DS_COMP_INVOKE, 6, &ss);
  ASSERT_EQ(0, s);

  // invalid op
  s = DSState_complete(this->state, static_cast<DSCompletionOp>(10000), 6, &line); // do nothing
  ASSERT_EQ(0, s);

  unsigned int size = DSState_complete(this->state, DS_COMP_SIZE, 0, nullptr);
  ASSERT_EQ(0, size);
  s = DSState_complete(this->state, DS_COMP_GET, size, &line);
  ASSERT_STREQ(nullptr, line);
  ASSERT_EQ(0, s);
  s = DSState_complete(this->state, DS_COMP_GET, 0, nullptr);
  ASSERT_EQ(0, s);

  line = "echo ~";
  s = DSState_complete(this->state, DS_COMP_INVOKE, 6, &line);
  ASSERT_TRUE(s > 0);
  auto expect = tilde();
  for (auto &e : expect) {
    std::cerr << e << std::endl;
  }
  ASSERT_EQ(expect.size(), s);
  for (unsigned int i = 0; i < s; i++) {
    const char *ret = nullptr;
    s = DSState_complete(this->state, DS_COMP_GET, i, &ret);
    ASSERT_STREQ(expect[i].c_str(), ret);
    ASSERT_EQ(0, s);
  }
  s = DSState_complete(this->state, DS_COMP_CLEAR, 0, nullptr);
  ASSERT_EQ(0, s);
  s = DSState_complete(this->state, DS_COMP_SIZE, 0, nullptr);
  ASSERT_EQ(0, s);

  const char *prefix;
  if (ydsh::platform::platform() == ydsh::platform::PlatformType::CYGWIN) {
    prefix = "~N";
    line = "echo ~N";
  } else {
    prefix = "~r";
    line = "echo ~r";
  }
  s = DSState_complete(this->state, DS_COMP_INVOKE, 7, &line);
  ASSERT_TRUE(s > 0);
  size = DSState_complete(this->state, DS_COMP_SIZE, 0, nullptr);
  expect = filter(expect, prefix);
  ASSERT_EQ(expect.size(), size);
  for (unsigned int i = 0; i < size; i++) {
    const char *ret = nullptr;
    s = DSState_complete(this->state, DS_COMP_GET, i, &ret);
    ASSERT_STREQ(expect[i].c_str(), ret);
    ASSERT_EQ(0, s);
  }
}

TEST_F(APITest, option) {
  ASSERT_EQ(0, DSState_option(nullptr));
  ASSERT_EQ(DS_OPTION_ASSERT, DSState_option(this->state));
  DSState_unsetOption(this->state, DS_OPTION_ASSERT);
  ASSERT_EQ(0, DSState_option(this->state));
  DSState_setOption(nullptr, 0);   // do nothing
  DSState_unsetOption(nullptr, 0); // do nothing

  const unsigned short option =
      DS_OPTION_ASSERT | DS_OPTION_INTERACTIVE | DS_OPTION_TRACE_EXIT | DS_OPTION_JOB_CONTROL;
  DSState_setOption(this->state, option);
  ASSERT_EQ(option, DSState_option(this->state));
  DSState_unsetOption(this->state, DS_OPTION_TRACE_EXIT);
  ASSERT_EQ(DS_OPTION_ASSERT | DS_OPTION_INTERACTIVE | DS_OPTION_JOB_CONTROL,
            DSState_option(this->state));
  DSState_unsetOption(this->state, option);
  ASSERT_EQ(0, DSState_option(this->state));
}

TEST_F(APITest, status) {
  int s = DSState_exitStatus(this->state);
  ASSERT_EQ(0, s);

  DSState_setExitStatus(this->state, 34);
  s = DSState_exitStatus(this->state);
  ASSERT_EQ(34, s);
  ASSERT_EQ(0, DSState_exitStatus(nullptr));

  DSState_setExitStatus(nullptr, 3400); // do nothing
  DSState_setExitStatus(this->state, 3400);
  s = DSState_exitStatus(this->state);
  ASSERT_EQ(72, s);

  std::string src = "$? = 9876";
  int ret = DSState_eval(this->state, "", src.c_str(), src.size(), nullptr);
  ASSERT_EQ(148, ret);
  ASSERT_EQ(ret, DSState_exitStatus(this->state));
}

struct Deleter {
  void operator()(DSError *e) const {
    DSError_release(e);
    delete e;
  }
};

static auto newError() { return std::unique_ptr<DSError, Deleter>(new DSError()); }

TEST_F(APITest, abort) {
  std::string src = "function f( $a : String, $b : String) : Int { exit 54; }";
  auto e = newError();
  int s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e->kind);

  src = R"(["d", ""].sortBy($f))";
  e = newError();
  s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(1, s);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e->kind);

  src = R"(["d", ""].sortWith($f))";
  e = newError();
  s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(1, s);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e->kind);

  src = R"(function g( $a : String, $b : String) : Boolean { exit 54; })";
  e = newError();
  s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e->kind);

  src = R"(["d", ""].sortWith($g))";
  e = newError();
  s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), e.get());
  ASSERT_EQ(54, s);
  ASSERT_EQ(DS_ERROR_KIND_EXIT, e->kind);
}

TEST_F(APITest, pid) {
  pid_t pid = getpid();
  std::string src("assert($$ == ");
  src += std::to_string(pid);
  src += ")";

  DSError e;
  int s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), &e);
  auto kind = e.kind;
  DSError_release(&e);
  ASSERT_EQ(0, s);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, kind);
}

TEST_F(APITest, load1) {
  DSError e;
  int r = DSState_loadAndEval(this->state, "hogehuga", &e);
  int errorNum = errno;
  ASSERT_EQ(1, r);
  ASSERT_EQ(ENOENT, errorNum);
  ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);

  DSError_release(&e);
}

TEST_F(APITest, load2) {
  DSError e;
  int r = DSState_loadAndEval(this->state, ".", &e);
  int errorNum = errno;
  ASSERT_EQ(1, r);
  ASSERT_EQ(EISDIR, errorNum);
  ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);

  DSError_release(&e);
}

TEST_F(APITest, load3) {
  auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

  DSError e;
  int r = DSState_loadAndEval(this->state, modName.c_str(), &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  r = DSState_loadAndEval(this->state, modName.c_str(), &e); // file is already loaded
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);
}

TEST_F(APITest, load4) {
  auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

  std::string line = "source ";
  line += modName;

  DSError e;
  int r = DSState_eval(this->state, "(string)", line.c_str(), line.size(), &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  r = DSState_loadAndEval(this->state, modName.c_str(), &e); // file is already loaded
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);
}

TEST_F(APITest, load5) {
  auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

  DSError e;
  int r = DSState_loadAndEval(this->state, modName.c_str(), &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  // use loaded moudle
  std::string line = format(R"(
true
source %s as m
assert $m.mod_load_success
)",
                            modName.c_str());

  r = DSState_eval(this->state, "(string)", line.c_str(), line.size(), &e);
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);
}

template <typename Func>
static Output invoke(Func func) {
  IOConfig config;
  config.out = IOConfig::PIPE;
  config.err = IOConfig::PIPE;

  return ProcBuilder::spawn(config, [func]() { return func(); }).waitAndGetResult(true);
}

TEST_F(APITest, module1) {
  int ret = DSState_loadModule(nullptr, "helllo", 0, nullptr);
  ASSERT_EQ(-1, ret);

  ret = DSState_loadModule(this->state, nullptr, 0, nullptr);
  ASSERT_EQ(-1, ret);
}

TEST_F(APITest, module2) {
  auto ret = invoke([&] {
    int ret = DSState_loadModule(this->state, "fhjreuhfurie", 0, nullptr);
    DSState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(
      this->expect(ret, 1, WaitStatus::EXITED, "",
                   "ydsh: cannot load file: fhjreuhfurie, by `No such file or directory'"));

  DSError e;
  int r = DSState_loadModule(this->state, "fhuahfuiefer", 0, &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);
  ASSERT_STREQ(strerror(ENOENT), e.name);
  ASSERT_EQ(0, e.lineNum);
  ASSERT_EQ(0, e.chars);
  DSError_release(&e);

  ret = invoke([&] {
    int ret = DSState_loadModule(this->state, "fhjreuhfurie", DS_MOD_IGNORE_ENOENT, nullptr);
    DSState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 0, WaitStatus::EXITED));
}

TEST_F(APITest, module3) {
  auto fileName = this->createTempFile("target.ds", "var OK_LOADING = true");

  int r = DSState_loadModule(this->state, fileName.c_str(), 0, nullptr);
  ASSERT_EQ(0, r);
  std::string src = "assert $OK_LOADING";
  r = DSState_eval(this->state, "(string)", src.c_str(), src.size(), nullptr);
  ASSERT_EQ(0, r);
}

TEST_F(APITest, module4) {
  auto fileName = this->createTempFile("target.ds", "source  hoghreua");
  DSError e;
  int r =
      DSState_loadModule(this->state, fileName.c_str(), DS_MOD_FULLPATH | DS_MOD_IGNORE_ENOENT, &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
  ASSERT_STREQ("NotFoundMod", e.name);
  ASSERT_EQ(1, e.lineNum);
  ASSERT_EQ(9, e.chars);
  DSError_release(&e);

  // check error message
  auto ret = invoke([&] {
    int ret = DSState_loadModule(this->state, fileName.c_str(),
                                 DS_MOD_FULLPATH | DS_MOD_IGNORE_ENOENT, nullptr);
    DSState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(
      this->expectRegex(ret, 1, WaitStatus::EXITED, "",
                        "^.+/target.ds:1:9: \\[semantic error\\] module not found: `hoghreua'.+$"));
}

TEST_F(APITest, module5) {
  DSError e;
  int r = DSState_loadModule(this->state, "hfeurhfiurhefuie", DS_MOD_FULLPATH, &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);
  ASSERT_STREQ(strerror(ENOENT), e.name);
  ASSERT_EQ(0, e.lineNum);
  ASSERT_EQ(0, e.chars);
  DSError_release(&e);

  // check error message
  auto ret = invoke([&] {
    int ret = DSState_loadModule(this->state, "freijjfeir", DS_MOD_FULLPATH, nullptr);
    DSState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(
      this->expect(ret, 1, WaitStatus::EXITED, "",
                   "ydsh: cannot load file: freijjfeir, by `No such file or directory'"));
}

TEST_F(APITest, module6) {
  if (ydsh::platform::platform() == ydsh::platform::PlatformType::CYGWIN) {
    return;
  }

  auto fileName = this->createTempFile(R"(ss$ho"\hgoe
        \ \
)",
                                       "echo moduel!!; exit 56");
  auto ret = invoke([&] {
    int ret = DSState_loadModule(this->state, fileName.c_str(), DS_MOD_FULLPATH, nullptr);
    DSState_delete(&this->state);
    return ret;
  });
  ASSERT_NO_FATAL_FAILURE(this->expect(ret, 56, WaitStatus::EXITED, "moduel!!"));
}

TEST_F(APITest, module7) {
  auto fileName = this->createTempFile("mod1", "var AAA = 34");
  DSError e;
  int r = DSState_loadModule(this->state, fileName.c_str(), DS_MOD_FULLPATH, &e);
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  fileName = this->createTempFile("mod2", "var AAA = $false");
  r = DSState_loadModule(this->state, fileName.c_str(), DS_MOD_FULLPATH, &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(1, e.lineNum);
  ASSERT_EQ(1, e.chars);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
  DSError_release(&e);
}

TEST_F(APITest, moduleLimit) {
  constexpr unsigned int limit = 0x8FFF;
  const unsigned int digits = ydsh::countDigits(limit);
  for (unsigned int i = 0; i < limit; i++) {
    std::string name = "mod_";
    name += ydsh::padLeft(i, digits, '0');
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

  DSError e;
  int r = DSState_eval(this->state, "(string)", source.c_str(), source.size(), &e);
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  source = format("source %s/mod_32765", dir);
  r = DSState_eval(this->state, "(string)", source.c_str(), source.size(), &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
  ASSERT_STREQ("ModLimit", e.name);
  ASSERT_EQ(11, e.lineNum);
  ASSERT_EQ(8, e.chars);
  DSError_release(&e);

  // load module directly
  std::string src = dir;
  src += "/mod_32770";

  r = DSState_loadAndEval(this->state, src.c_str(), &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);
  ASSERT_STREQ(strerror(EPERM), e.name);
  DSError_release(&e);
}

TEST_F(APITest, globalLimit) {
  constexpr unsigned int limit = 0x8FFF;
  const unsigned int digits = ydsh::countDigits(limit);
  for (unsigned int i = 0; i < limit; i++) {
    std::string name = "mod_";
    name += ydsh::padLeft(i, digits, '0');
    std::string value = "var var_";
    value += name;
    value += " = true";
    this->createTempFile(name.c_str(), value.c_str());
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

  DSError e;
  int r = DSState_eval(this->state, "(string)", source.c_str(), source.size(), &e);
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  DSError_release(&e);

  source = format("source %s/mod_{28667..32761}", dir);
  r = DSState_eval(this->state, "(string)", source.c_str(), source.size(), &e);
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
  ASSERT_STREQ("GlobalLimit", e.name);
  ASSERT_EQ(9, e.lineNum);
  ASSERT_EQ(8, e.chars);
  DSError_release(&e);
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

      DSState *state = DSState_create();
      if (this->jobctrl) {
        DSState_setOption(state, DS_OPTION_JOB_CONTROL);
      }
      int ret = DSState_eval(state, nullptr, this->str.c_str(), this->str.size(), nullptr);
      DSState_delete(&state);
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

  // command, eval
  result = EXEC("command eval %s --first | eval command %s", PID_CHECK_PATH, PID_CHECK_PATH);
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
      this->expect(result, 1, WaitStatus::EXITED, "", "ydsh: fg: current: no such job"));

  result = EXEC("fg %hoge");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "ydsh: fg: %hoge: no such job"));

  result = EXEC("fg %1");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "ydsh: fg: %1: no such job"));

  const char *str = R"(
        sh -c 'kill -s STOP $$; exit 180'
        assert $? == 128 + %'stop'.value()
        assert { fg %1; $?; } == 180 : "$?"
        fg %1
)";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "ydsh: fg: %1: no such job"));

  str = R"(
        sh -c 'kill -s STOP $$; exit 18'
        assert $? == 128 + %'stop'.value()
        fg
)";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(this->expect(result, 18));
}

TEST_F(JobTest, jobctrl2) {
  // invalid
  auto result = EXEC("bg");
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 1, WaitStatus::EXITED, "", "ydsh: bg: current: no such job"));

  result = EXEC("bg hoge %1");
  ASSERT_NO_FATAL_FAILURE(this->expect(result, 1, WaitStatus::EXITED, "",
                                       "ydsh: bg: hoge: no such job\nydsh: bg: %1: no such job"));

  const char *str = R"(
        var j = {
             %'stop'.kill($PID)
             exit 99
        } &
        assert $j.wait() == 128 + %'stop'.value()
        assert $j.poll()
        assert { bg; $?; } == 0
        var r = $j.wait()
        assert $r == 99 : $r as String
        true
)";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(this->expect(result));

  str = R"(
        var j = {
             %'stop'.kill($PID)
             exit 99
        } &
        assert $j.wait() == 128 + %'stop'.value()
        assert $j.poll()
        assert { bg %1 %2; $?; } == 1
        var r = $j.wait()
        assert $r == 99 : $r as String
        true
  )";
  result = EXEC(str);
  ASSERT_NO_FATAL_FAILURE(
      this->expect(result, 0, WaitStatus::EXITED, "", "ydsh: bg: %2: no such job"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
