#include "gtest/gtest.h"

#include <algorithm>
#include <array>

#include <ydsh/ydsh.h>
#include <config.h>
#include <misc/fatal.h>
#include <constant.h>

#include <sys/types.h>
#include <pwd.h>

#include "../test_common.h"

template <typename ...T>
std::array<char *, sizeof...(T) + 2> make_argv(const char *name, T ...args) {
    return {{const_cast<char *>(name), const_cast<char *>(args)..., nullptr }};
}


TEST(BuiltinExecTest, case1) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("echo", "hello").data());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret));

    DSState_delete(&state);
}

TEST(BuiltinExecTest, case2) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("fheruifh", "hello").data());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, ret));

    DSState_delete(&state);
}

struct APITest : public ExpectOutput, public TempFileFactory {
    DSState *state{nullptr};

    APITest() {
        this->state = DSState_create();
    }

    ~APITest() override {
        DSState_delete(&this->state);
    }
};

TEST_F(APITest, version) {
    SCOPED_TRACE("");

    DSVersion version;
    DSState_version(&version);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, version.major));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, version.minor));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, version.patch));
}

TEST_F(APITest, config) {
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(ydsh::SYSTEM_CONFIG_DIR, DSState_systemConfigDir()));
}

TEST_F(APITest, lineNum1) {
    SCOPED_TRACE("");

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, DSState_lineNum(this->state)));

    const char *str = "12 + 32\n $true\n";
    DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, DSState_lineNum(this->state)));

    DSState_setLineNum(this->state, 49);
    str = "23";
    DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(50u, DSState_lineNum(this->state)));
}

TEST_F(APITest, lineNum2) {
    SCOPED_TRACE("");

    DSError e;
    auto fileName1 = this->createTempFile("target1.ds", "true\ntrue\n");
    DSState_loadAndEval(this->state, fileName1.c_str(), &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3, DSState_lineNum(this->state)));
    DSError_release(&e);

    fileName1 = this->createTempFile("targe2.ds", "45/'de'");
    DSState_loadAndEval(this->state, fileName1.c_str(), &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, e.lineNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, DSState_lineNum(this->state)));
    DSError_release(&e);
}

TEST_F(APITest, prompt) {
    SCOPED_TRACE("");

    const char *str = "$PS1 = 'hello>'; $PS2 = 'second>'";
    DSState_eval(this->state, nullptr, str, strlen(str), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hello>", DSState_prompt(this->state, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("second>", DSState_prompt(this->state, 2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", DSState_prompt(this->state, 5)));
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
    SCOPED_TRACE("");

    // null arguments
    auto *c = DSState_complete(nullptr, nullptr, 1); // do nothing
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c == nullptr));

    c = DSState_complete(this->state, "echo ~", 6);
    unsigned int size = DSCandidates_size(c);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(size > 0));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(nullptr, DSCandidates_get(c, size)));

    auto expect = tilde();
    for(auto &e : expect) { std::cerr << e << std::endl; }
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect.size(), size));
    for(unsigned int i = 0; i < size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(expect[i].c_str(), DSCandidates_get(c, i)));
    }
    DSCandidates_release(&c);


    c = DSState_complete(this->state, "echo ~r", 7);
    size = DSCandidates_size(c);
    expect = filter(expect, "~r");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect.size(), size));
    for(unsigned int i = 0; i < size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(expect[i].c_str(), DSCandidates_get(c, i)));
    }
    DSCandidates_release(&c);
}

TEST_F(APITest, option) {
    SCOPED_TRACE("");

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_OPTION_ASSERT, DSState_option(this->state)));
    DSState_unsetOption(this->state, DS_OPTION_ASSERT);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, DSState_option(this->state)));
}

TEST_F(APITest, scriptDir) {
    SCOPED_TRACE("");

    int r = DSState_setScriptDir(this->state, "hfarefoiaji vfd");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, r));
}

TEST_F(APITest, status) {
    int s = DSState_getExitStatus(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, s));

    DSState_setExitStatus(this->state, 34);
    s = DSState_getExitStatus(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(34, s));
}

TEST_F(APITest, pid) {
    SCOPED_TRACE("");

    pid_t pid = getpid();
    std::string src("assert($$ == ");
    src += std::to_string(pid);
    src += ")";

    DSError e;
    int s = DSState_eval(this->state, nullptr, src.c_str(), src.size(), &e);
    auto kind = e.kind;
    DSError_release(&e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, s));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, kind));
}

TEST_F(APITest, load1) {
    DSError e;
    int r = DSState_loadAndEval(this->state, "hogehuga", &e);
    int errorNum = errno;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ENOENT, errorNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind));

    DSError_release(&e);
}

TEST_F(APITest, load2) {
    DSError e;
    int r = DSState_loadAndEval(this->state, ".", &e);
    int errorNum = errno;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(EISDIR, errorNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind));

    DSError_release(&e);
}

TEST_F(APITest, load3) {
    auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

    DSError e;
    int r = DSState_loadAndEval(this->state, modName.c_str(), &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind));
    DSError_release(&e);

    r = DSState_loadAndEval(this->state, modName.c_str(), &e);    // file is already loaded
    int errorNum = errno;
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(ETXTBSY, errorNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind));
    DSError_release(&e);
}

TEST_F(APITest, load4) {
    auto modName = this->createTempFile("mod.ds", "var mod_load_success = true; false");

    std::string line = "source ";
    line += modName;

    DSError e;
    int r = DSState_eval(this->state, "(string)", line.c_str(), line.size(), &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind));
    DSError_release(&e);

    r = DSState_loadAndEval(this->state, modName.c_str(), &e);    // file is already loaded
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind));
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

TEST_F(APITest, module1) {
    DSError e;
    int r = DSState_loadModule(this->state, "fhuahfuiefer", "12", 0, &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_PARSE_ERROR, e.kind));
    DSError_release(&e);

    auto ret = invoke([&]{
        return DSState_loadModule(this->state, "fhjreuhfurie", "34", 0, nullptr);
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 1, WaitStatus::EXITED, "",
            "ydsh: [syntax error] invalid token, expected: <Identifier>"));
}

TEST_F(APITest, module2) {
    auto ret = invoke([&]{
        return DSState_loadModule(this->state, "fhjreuhfurie", "hoge", 0, nullptr);
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 1, WaitStatus::EXITED, "",
                                         "ydsh: [semantic error] module not found: `fhjreuhfurie'"));

    DSError e;
    int r = DSState_loadModule(this->state, "fhuahfuiefer", "hoge", 0, &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_TYPE_ERROR, e.kind));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("NotFoundMod", e.name));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, e.lineNum));
    DSError_release(&e);

    ret = invoke([&]{
        return DSState_loadModule(this->state, "fhjreuhfurie", "hoge", DS_MOD_IGNORE_ENOENT, nullptr);
    });
    ASSERT_NO_FATAL_FAILURE(this->expect(ret, 1, WaitStatus::EXITED));
}

TEST_F(APITest, module3) {
    auto fileName = this->createTempFile("target.ds", "var OK_LOADING = true");

    int r = DSState_loadModule(this->state, fileName.c_str(), nullptr, 0, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
    std::string src = "assert $OK_LOADING";
    r = DSState_eval(this->state, "(string)", src.c_str(), src.size(), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
}

TEST_F(APITest, module4) {
    auto fileName = this->createTempFile("target.ds", "var OK_LOADING = true");

    int r = DSState_loadModule(this->state, fileName.c_str(), "hoge", DS_MOD_FULLPATH, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
    std::string src = "assert $hoge.OK_LOADING";
    r = DSState_eval(this->state, "(string)", src.c_str(), src.size(), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
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

static std::vector<std::string> split(const std::string &str, char delim = ' ') {
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
    SCOPED_TRACE("");

    // normal
    auto result = EXEC("%s --first | %s | %s", PID_CHECK_PATH, PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN3));
    auto pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[1].ppid, pids[2].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[2].ppid, pids[0].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[2].pgid));

    // command, eval
    result = EXEC("command eval %s --first | eval command %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[1].pgid));

    // udc1
    result = EXEC("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[1].pgid));

    // udc2
    result = EXEC("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));

    // last pipe
    result = EXEC("%s --first | { %s; }", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pgid, pids[1].pgid));
}

TEST_F(JobTest, pid2) {    // disable job control
    SCOPED_TRACE("");

    // normal
    auto result = EXEC2("%s --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    auto pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[1].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));

    // udc1
    result = EXEC2("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[1].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));

    // udc2
    result = EXEC2("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[1].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));

    // last pipe
    result = EXEC("%s --first | { %s; }", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, 0, WaitStatus::EXITED, PATTERN2));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pgid, pids[1].pgid));
}

#undef EXEC
#define EXEC(S) exec(std::string(S)).waitAndGetResult(true)

TEST_F(JobTest, jobctrl1) {
    SCOPED_TRACE("");

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
    SCOPED_TRACE("");

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
