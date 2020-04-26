#include "gtest/gtest.h"

#include <algorithm>
#include <array>

#include <ydsh/ydsh.h>
#include <config.h>
#include <misc/fatal.h>
#include <constant.h>

#include <pwd.h>

#include "../test_common.h"

#ifndef API_TEST_WORK_DIR
#error "require API_TEST_WORK_DIR"
#endif

template <typename ...T>
std::array<char *, sizeof...(T) + 2> make_argv(const char *name, T ...args) {
    return {{const_cast<char *>(name), const_cast<char *>(args)..., nullptr }};
}


TEST(BuiltinExecTest, case1) {
    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("echo", "hello").data());
    ASSERT_EQ(0, ret);

    DSState_delete(&state);
}

TEST(BuiltinExecTest, case2) {
    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("fheruifh", "hello").data());
    ASSERT_EQ(1, ret);

    DSState_delete(&state);
}

TEST(BuiltinExecTest, case3) {
    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("exit", "12000").data());
    ASSERT_EQ(224, ret);

    DSState_delete(&state);
}

TEST(BuiltinExecTest, shctl) {
    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("shctl", "is-interactive").data());
    ASSERT_EQ(1, ret);

    DSState_setOption(state, DS_OPTION_INTERACTIVE);
    ret = DSState_exec(state, make_argv("shctl", "is-interactive").data());
    ASSERT_EQ(0, ret);


    DSState_delete(&state);
}

struct APITest : public ExpectOutput, public ydsh::TempFileFactory {
    DSState *state{nullptr};

    APITest() {
        this->state = DSState_create();
    }

    ~APITest() override {
        DSState_delete(&this->state);
    }
};

TEST_F(APITest, version) {
    DSVersion version;
    DSState_version(&version);

    ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, version.major);
    ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, version.minor);
    ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, version.patch);
}

TEST_F(APITest, config) {
    ASSERT_STREQ(ydsh::SYSTEM_CONFIG_DIR, DSState_configDir());
}

TEST_F(APITest, lineNum1) {
    ASSERT_EQ(1u, DSState_lineNum(this->state));

    const char *str = "12 + 32\n $true\n";
    DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
    ASSERT_EQ(3u, DSState_lineNum(this->state));

    DSState_setLineNum(this->state, 49);
    str = "23";
    DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
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
    ASSERT_EQ(1, s);
    ASSERT_EQ(DS_ERROR_KIND_RUNTIME_ERROR, e.kind);
    ASSERT_EQ(5, e.lineNum);
    DSError_release(&e);
}

static const char *getPrompt(DSState *st, unsigned int n) {
    const char *bug = "";
    DSState_lineEditOp(st, DS_EDIT_PROMPT, n, &bug);
    return bug;
}

TEST_F(APITest, prompt) {
    // default prompt
    std::string p = "ydsh-";
    p += std::to_string(X_INFO_MAJOR_VERSION);
    p += ".";
    p += std::to_string(X_INFO_MINOR_VERSION);
    p += getuid() == 0 ? "# " : "$ ";
    ASSERT_EQ(p , getPrompt(this->state, 1));
    ASSERT_STREQ("> ", getPrompt(this->state, 2));
    ASSERT_STREQ("", getPrompt(this->state, 5));
    ASSERT_STREQ("", getPrompt(this->state, -82));

    // use module
    const char *str = "source " API_TEST_WORK_DIR "/../../etc/ydsh/module/edit;\n"
                      "source " API_TEST_WORK_DIR "/../../etc/ydsh/module/prompt;\n"
                      "$PS1 = 'hello>'; $PS2 = 'second>'";
    DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
    ASSERT_STREQ("hello>", getPrompt(this->state, 1));
    ASSERT_STREQ("second>", getPrompt(this->state, 2));
    ASSERT_STREQ("", getPrompt(this->state, -82));
}

static std::vector<std::string> tilde() {
    std::vector<std::string> v;
    setpwent();

    for(decltype(getpwent()) entry = nullptr; (entry = getpwent()) != nullptr;) {
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
    for(auto &e : v) {
        if(strstr(e.c_str(), cond) != nullptr) {
            t.push_back(e);
        }
    }
    return t;
}

TEST_F(APITest, complete) {
    // null arguments
    DSState_completionOp(nullptr, DS_COMP_INVOKE, 1, nullptr);  // do nothing

    const char *line = "echo ~";
    DSState_completionOp(this->state, DS_COMP_INVOKE, 6, &line);

    unsigned int size = DSState_completionOp(this->state, DS_COMP_SIZE, 0, nullptr);
    ASSERT_TRUE(size > 0);
    DSState_completionOp(this->state, DS_COMP_GET, size, &line);
    ASSERT_STREQ(nullptr, line);

    auto expect = tilde();
    for(auto &e : expect) { std::cerr << e << std::endl; }
    ASSERT_EQ(expect.size(), size);
    for(unsigned int i = 0; i < size; i++) {
        const char *ret = nullptr;
        DSState_completionOp(this->state, DS_COMP_GET, i, &ret);
        ASSERT_STREQ(expect[i].c_str(), ret);
    }
    DSState_completionOp(this->state, DS_COMP_CLEAR, 0, nullptr);

    line = "echo ~r";
    DSState_completionOp(this->state, DS_COMP_INVOKE, 7, &line);
    size = DSState_completionOp(this->state, DS_COMP_SIZE, 0, nullptr);
    expect = filter(expect, "~r");
    ASSERT_EQ(expect.size(), size);
    for(unsigned int i = 0; i < size; i++) {
        const char *ret = nullptr;
        DSState_completionOp(this->state, DS_COMP_GET, i, &ret);
        ASSERT_STREQ(expect[i].c_str(), ret);
    }
}

TEST_F(APITest, option) {
    ASSERT_EQ(DS_OPTION_ASSERT, DSState_option(this->state));
    DSState_unsetOption(this->state, DS_OPTION_ASSERT);
    ASSERT_EQ(0, DSState_option(this->state));
}

TEST_F(APITest, scriptDir) {
    int r = DSState_setScriptDir(this->state, "hfarefoiaji vfd");
    ASSERT_EQ(-1, r);
}

TEST_F(APITest, status) {
    int s = DSState_getExitStatus(this->state);
    ASSERT_EQ(0, s);

    DSState_setExitStatus(this->state, 34);
    s = DSState_getExitStatus(this->state);
    ASSERT_EQ(34, s);

    DSState_setExitStatus(this->state, 3400);
    s = DSState_getExitStatus(this->state);
    ASSERT_EQ(72, s);

    std::string src = "$? = 9876";
    int ret = DSState_eval(this->state, "", src.c_str(), src.size(), nullptr);
    ASSERT_EQ(148, ret);
    ASSERT_EQ(ret, DSState_getExitStatus(this->state));
}

struct Deleter {
    void operator()(DSError *e) const {
        DSError_release(e);
        delete e;
    }
};

static auto newError() {
    return std::unique_ptr<DSError, Deleter>(new DSError());
}

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

    r = DSState_loadAndEval(this->state, modName.c_str(), &e);    // file is already loaded
    int errorNum = errno;
    ASSERT_EQ(1, r);
    ASSERT_EQ(ETXTBSY, errorNum);
    ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);
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

    r = DSState_loadAndEval(this->state, modName.c_str(), &e);    // file is already loaded
    ASSERT_EQ(0, r);
    ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
    DSError_release(&e);
}

template <typename Func>
static Output invoke(Func func) {
    IOConfig config;
    config.out = IOConfig::PIPE;
    config.err = IOConfig::PIPE;

    return ProcBuilder::spawn(config, [func]() {
        return func();
    }).waitAndGetResult(true);
}

TEST_F(APITest, module2) {
    auto ret = invoke([&]{
        return DSState_loadModule(this->state, "fhjreuhfurie", 0, nullptr);
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 1, WaitStatus::EXITED, "",
                                         "ydsh: [semantic error] module not found: `fhjreuhfurie'"));

    DSError e;
    int r = DSState_loadModule(this->state, "fhuahfuiefer", 0, &e);
    ASSERT_EQ(1, r);
    ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
    ASSERT_STREQ("NotFoundMod", e.name);
    ASSERT_EQ(0, e.lineNum);
    DSError_release(&e);

    ret = invoke([&]{
        return DSState_loadModule(this->state, "fhjreuhfurie", DS_MOD_IGNORE_ENOENT, nullptr);
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
    auto fileName = this->createTempFile("target.ds", "source hoghreua");
    DSError e;
    int r = DSState_loadModule(this->state, fileName.c_str(), DS_MOD_FULLPATH | DS_MOD_IGNORE_ENOENT, &e);
    ASSERT_EQ(1, r);
    ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
    ASSERT_STREQ("NotFoundMod", e.name);
    ASSERT_EQ(1, e.lineNum);
    DSError_release(&e);

    // check error message
    auto ret = invoke([&]{
        return DSState_loadModule(this->state, fileName.c_str(),
                DS_MOD_FULLPATH | DS_MOD_IGNORE_ENOENT, nullptr);
    });
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(ret, 1, WaitStatus::EXITED, "",
                                         "^.+/target.ds:1: \\[semantic error\\] module not found: `hoghreua'.+$"));
}

TEST_F(APITest, module5) {
    DSError e;
    int r = DSState_loadModule(this->state, "hfeurhfiurhefuie", DS_MOD_FULLPATH , &e);
    ASSERT_EQ(1, r);
    ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind);
    ASSERT_STREQ("NotFoundMod", e.name);
    ASSERT_EQ(0, e.lineNum);
    DSError_release(&e);

    // check error message
    auto ret = invoke([&]{
        return DSState_loadModule(this->state, "freijjfeir",
                                  DS_MOD_FULLPATH, nullptr);
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 1, WaitStatus::EXITED, "",
                    "ydsh: [semantic error] module not found: `freijjfeir'"));
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
            for(auto &e : this->envs) {
                setenv(e.first.c_str(), e.second.c_str(), 1);
            }

            DSState *state = DSState_create();
            if(this->jobctrl) {
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

static std::vector<std::string> split(const std::string &str, int delim = ' ') {
    std::vector<std::string> bufs;
    bufs.emplace_back();

    for(auto &ch : str) {
        if(ch == delim) {
            bufs.emplace_back();
        } else {
            bufs.back() += ch;
        }
    }
    return bufs;
}

static std::vector<PIDs> decompose(const std::string &str) {
    auto ss = split(str);
    std::vector<PIDs> ret(ss.size());

    for(unsigned int i = 0; i < ret.size(); i++) {
        int r = Extractor(ss[i].c_str())("[", ret[i].pid, ",", ret[i].ppid, ",", ret[i].pgid, "]");
        if(r != 0) {
            fatal("broken\n");
        }
    }
    return ret;
}

struct JobTest : public ExpectOutput {};

#define PATTERN "\\[[0-9]+,[0-9]+,[0-9]+\\]"
#define PATTERN2 PATTERN " " PATTERN
#define PATTERN3 PATTERN " " PATTERN " " PATTERN

TEST_F(JobTest, pid1) {    // enable job control
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
    result = EXEC("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_EQ(2u, pids.size());

    ASSERT_NE(pids[0].ppid, pids[1].ppid);

    ASSERT_EQ(pids[0].pid, pids[0].pgid);
    ASSERT_EQ(pids[0].pid, pids[1].pgid);

    // udc2
    result = EXEC("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
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

TEST_F(JobTest, pid2) {    // disable job control
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
    result = EXEC2("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_EQ(2u, pids.size());

    ASSERT_NE(pids[0].ppid, pids[1].ppid);
    ASSERT_NE(pids[0].pid, pids[0].pgid);
    ASSERT_NE(pids[1].pid, pids[1].pgid);
    ASSERT_EQ(pids[0].pgid, pids[1].pgid);

    // udc2
    result = EXEC2("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
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
    ASSERT_NO_FATAL_FAILURE(
            this->expect(result, 1, WaitStatus::EXITED, "", "ydsh: bg: hoge: no such job\nydsh: bg: %1: no such job"));


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
