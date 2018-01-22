#include "gtest/gtest.h"

#include <algorithm>
#include <array>

#include <ydsh/ydsh.h>
#include <config.h>
#include <misc/fatal.h>

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

TEST(API, case1) {
    SCOPED_TRACE("");

    DSVersion version;
    DSState_version(&version);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, version.major));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, version.minor));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, version.patch));
}

TEST(API, case2) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, DSState_lineNum(state)));

    const char *str = "12 + 32\n $true\n";
    DSState_eval(state, nullptr, str, strlen(str), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, DSState_lineNum(state)));

    DSState_setLineNum(state, 49);
    str = "23";
    DSState_eval(state, nullptr, str, strlen(str), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(50u, DSState_lineNum(state)));

    DSState_delete(&state);
}

TEST(API, case3) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();
    const char *str = "$PS1 = 'hello>'; $PS2 = 'second>'";
    DSState_eval(state, nullptr, str, strlen(str), nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hello>", DSState_prompt(state, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("second>", DSState_prompt(state, 2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", DSState_prompt(state, 5)));

    DSState_delete(&state);
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

TEST(API, case4) {
    SCOPED_TRACE("");

    // null arguments
    DSState_complete(nullptr, nullptr, 1, nullptr); // do nothing

    DSState *state = DSState_create();

    DSCandidates c;
    DSState_complete(state, "echo ~", 6, &c);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.values != nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.size > 0));

    auto expect = tilde();
    for(auto &e : expect) { std::cerr << e << std::endl; }
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect.size(), c.size));
    for(unsigned int i = 0; i < c.size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(expect[i].c_str(), c.values[i]));
    }
    DSCandidates_release(&c);


    DSState_complete(state, "echo ~r", 7, &c);
    expect = filter(expect, "~r");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect.size(), c.size));
    for(unsigned int i = 0; i < c.size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(expect[i].c_str(), c.values[i]));
    }
    DSCandidates_release(&c);

    DSState_delete(&state);
}

TEST(API, case5) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_OPTION_ASSERT, DSState_option(state)));

    DSState_setOption(state, DS_OPTION_HISTORY);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_OPTION_HISTORY | DS_OPTION_ASSERT, DSState_option(state)));

    DSState_unsetOption(state, DS_OPTION_ASSERT);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_OPTION_HISTORY, DSState_option(state)));

    DSState_delete(&state);
}

TEST(API, case6) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();
    int r = DSState_setScriptDir(state, "hfarefoiaji vfd");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, r));
    DSState_delete(&state);
}

static ProcHandle exec(std::string &&str, bool jobControl = true) {
    IOConfig config{IOConfig::INHERIT, IOConfig::PIPE, IOConfig::PIPE};
    return ProcBuilder::spawn(config, [&] {
        DSState *state = DSState_create();
        if(jobControl) {
            DSState_setOption(state, DS_OPTION_JOB_CONTROL);
        }
        int ret = DSState_eval(state, nullptr, str.c_str(), str.size(), nullptr);
        DSState_delete(&state);
        return ret;
    });
}

#define EXEC(...) exec(format(__VA_ARGS__)).waitAndGetResult(true)
#define EXEC2(...) exec(format(__VA_ARGS__), false).waitAndGetResult(true)

struct PIDs {
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
};

static std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> bufs;
    bufs.emplace_back();

    for(auto &ch : str) {
        if(ch == ' ') {
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

TEST(API, case7) {
    SCOPED_TRACE("");

    // normal
    auto result = EXEC("%s --first | %s | %s", PID_CHECK_PATH, PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
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
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[1].pgid));

    // udc1
    result = EXEC("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[1].pgid));

    // udc2
    result = EXEC("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].ppid, pids[1].ppid));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));
}

TEST(API, case8) {
    SCOPED_TRACE("");

    // normal
    auto result = EXEC2("%s --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    auto pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[1].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));

    // udc1
    result = EXEC2("pidcheck() { command %s $@; }; %s --first | pidcheck", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[1].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));

    // udc2
    result = EXEC2("pidcheck() { command %s $@; }; pidcheck --first | %s", PID_CHECK_PATH, PID_CHECK_PATH);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    pids = decompose(result.out);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, pids.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].ppid, pids[1].ppid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[0].pid, pids[0].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_NE(pids[1].pid, pids[1].pgid));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(pids[0].pgid, pids[1].pgid));
}

#undef EXEC
#define EXEC(S) exec(std::string(S)).waitAndGetResult(true)

TEST(API, jobctrl1) {
    SCOPED_TRACE("");

    // invalid
    auto result = EXEC("fg");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: fg: current: no such job", result.err));

    result = EXEC("fg %hoge");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: fg: %hoge: no such job", result.err));

    result = EXEC("fg %1");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: fg: %1: no such job", result.err));

    const char *str = R"(
        sh -c 'kill -s STOP $$; exit 180'
        assert $? == 128 + %'stop'.value()
        assert { fg %1; $?; } == 180 : "$?"
        fg %1
)";
    result = EXEC(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: fg: %1: no such job", result.err));

    str = R"(
        sh -c 'kill -s STOP $$; exit 18'
        assert $? == 128 + %'stop'.value()
        fg
)";
    result = EXEC(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(18, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));
}

TEST(API, jobctrl2) {
    SCOPED_TRACE("");

    // invalid
    auto result = EXEC("bg");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: bg: current: no such job", result.err));

    result = EXEC("bg hoge %1");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: bg: hoge: no such job\nydsh: bg: %1: no such job", result.err));

    const char *str = R"(
        var j = {
             %'stop'.kill($PID)
             exit 99
        } &
        assert not $j.wait()
        assert { bg; $?; } == 0
        assert $j.wait()! == 99
        true
)";
    result = EXEC(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.err));

    str = R"(
        var j = {
             %'stop'.kill($PID)
             exit 99
        } &
        assert not $j.wait()
        assert { bg %1 %2; $?; } == 1
        assert $j.wait()! == 99
        true
)";
    result = EXEC(str);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, result.status.value));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", result.out));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("ydsh: bg: %2: no such job", result.err));
}

TEST(PID, case1) {
    SCOPED_TRACE("");

    pid_t pid = getpid();
    DSState *state = DSState_create();
    std::string src("assert($$ == ");
    src += std::to_string(pid);
    src += ")";

    DSError e;
    int s = DSState_eval(state, nullptr, src.c_str(), src.size(), &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, s));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind));

    DSState_delete(&state);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
