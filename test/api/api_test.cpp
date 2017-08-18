#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include <ydsh/ydsh.h>
#include <config.h>

#include <sys/types.h>
#include <pwd.h>

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
    int r = DSState_complete(nullptr, nullptr, 1, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, r));

    DSState *state = DSState_create();

    DSCandidates c;
    r = DSState_complete(state, "echo ~", 6, &c);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.values != nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.size > 0));

    auto expect = tilde();
    for(auto &e : expect) { std::cerr << e << std::endl; }
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect.size(), c.size));
    for(unsigned int i = 0; i < c.size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(expect[i].c_str(), c.values[i]));
    }
    DSCandidates_release(&c);


    r = DSState_complete(state, "echo ~r", 7, &c);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));

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
