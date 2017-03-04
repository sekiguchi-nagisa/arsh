#include <gtest/gtest.h>

#include <memory>
#include <algorithm>

#include <ydsh/ydsh.h>
#include <config.h>

#include <sys/types.h>
#include <pwd.h>

void addArg(std::vector<char *> &) {
}

template <typename... T>
void addArg(std::vector<char *> &out, const char *first, T ...rest) {
    out.push_back(const_cast<char *>(first));
    addArg(out, rest...);
}

template <typename... T>
std::unique_ptr<char *[]> make_argv(const char *name, T ...args) {
    std::vector<char *> out;
    addArg(out, name, args...);
    unsigned int size = out.size();
    std::unique_ptr<char *[]> ptr(new char*[size + 1]);
    for(unsigned int i = 0; i < size; i++) {
        ptr[i] = out[i];
    }
    ptr[size] = nullptr;
    return ptr;
}

TEST(BuiltinExecTest, case1) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("echo", "hello").get());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret));

    DSState_delete(&state);
}

TEST(BuiltinExecTest, case2) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();

    int ret = DSState_exec(state, make_argv("fheruifh", "hello").get());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, ret));

    DSState_delete(&state);
}

TEST(API, case1) {
    SCOPED_TRACE("");

    unsigned int vec[3];
    DSState_version(vec, 3);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, vec[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, vec[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, vec[2]));
}

TEST(API, case2) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, DSState_lineNum(state)));

    DSState_eval(state, nullptr, "12 + 32\n $true\n", nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, DSState_lineNum(state)));

    DSState_setLineNum(state, 49);
    DSState_eval(state, nullptr, "23", nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(50u, DSState_lineNum(state)));

    DSState_delete(&state);
}

TEST(API, case3) {
    SCOPED_TRACE("");

    DSState *state = DSState_create();
    DSState_eval(state, nullptr, "$PS1 = 'hello>'; $PS2 = 'second>'", nullptr);
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

    std::unique(v.begin(), v.end());
    std::sort(v.begin(), v.end());

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
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect.size(), c.size));
    for(unsigned int i = 0; i < c.size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(expect[i].c_str(), c.values[i]));
    }
    DSCandidates_release(&c);


    r = DSState_complete(state, "echo ~r", 7, &c);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, r));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.values != nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.size > 0));

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

    DSState_setOption(state, DS_OPTION_DUMP_CODE);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_OPTION_ASSERT | DS_OPTION_DUMP_CODE, DSState_option(state)));

    DSState_unsetOption(state, DS_OPTION_ASSERT);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_OPTION_DUMP_CODE, DSState_option(state)));

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
    src += "u)";

    DSError e;
    int s = DSState_eval(state, nullptr, src.c_str(), &e);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, s));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind));

    DSError_release(&e);
    DSState_delete(&state);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}