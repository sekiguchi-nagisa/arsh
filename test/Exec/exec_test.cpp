#include <gtest/gtest.h>

#include <ydsh/ydsh.h>
#include <misc/files.h>
#include <directive.h>
#include <config.h>


#ifndef EXEC_TEST_DIR
#define EXEC_TEST_DIR "."
#endif

using namespace ydsh;
using namespace ydsh::directive;
using namespace ydsh::misc;

class ExecTest : public ::testing::TestWithParam<std::string> {
private:
    std::string targetName;
    DSContext *ctx;

    /**
     * save some env
     */
    std::vector<std::pair<std::string, std::string>> envList;

public:
    ExecTest() : targetName(), ctx(), envList() {
    }

    virtual ~ExecTest() = default;


    virtual void saveEnv(const char *envName) {
        const char *value = getenv(envName);
        if(value != nullptr) {
            this->envList.push_back(std::make_pair(envName, value));
        }
    }

    virtual void restoreAllEnv() {
        for(auto &pair : this->envList) {
            setenv(pair.first.c_str(), pair.second.c_str(), 1);
        }
    }

    virtual void SetUp() {
        // save env
        this->saveEnv("HOME");
        this->saveEnv("PATH");
        this->saveEnv("OLDPWD");
        this->saveEnv("PWD");
        this->saveEnv("TMPDIR");

        this->targetName = this->GetParam();
        this->ctx = DSContext_create();
    }

    virtual void TearDown() {
        this->targetName.clear();
        DSContext_delete(&this->ctx);

        this->restoreAllEnv();
    }

    virtual const std::string &getSourceName() {
        return this->targetName;
    }

    virtual void doTest() {
        SCOPED_TRACE("");

        // create directive
        Directive d;
        bool s = Directive::init(this->getSourceName().c_str(), d);
        ASSERT_TRUE(s);

        // check run condition
        RunCondition haveDBus = RunCondition::FALSE;
#ifdef USE_DBUS
        haveDBus = RunCondition::TRUE;
#endif
        if(d.getIfHaveDBus() != RunCondition::IGNORE && haveDBus != d.getIfHaveDBus()) {
            return; // do nothing
        }


        const char *scriptName = this->getSourceName().c_str();
        FILE *fp = fopen(scriptName, "rb");
        ASSERT_TRUE(fp != nullptr);

        // set argument
        std::unique_ptr<char *const[]> argv = d.getAsArgv(scriptName);
        DSContext_setArguments(this->ctx, argv.get());

        // execute
        DSStatus *status;
        int ret = DSContext_loadAndEval(this->ctx, scriptName, fp, &status);

        // check status
        ASSERT_EQ(d.getResult(), DSStatus_getType(status));
        ASSERT_EQ(d.getLineNum(), DSStatus_getErrorLineNum(status));
        ASSERT_EQ(d.getStatus(), ret);
        ASSERT_STREQ(d.getErrorKind().c_str(), DSStatus_getErrorKind(status));
    }
};

TEST_P(ExecTest, baseTest) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(ExecTest, ExecTest, ::testing::ValuesIn(getFileList(EXEC_TEST_DIR, true)));


void addArg(std::vector<char *> &out) { }

template <typename... T>
void addArg(std::vector<char *> &out, const char *first, T ...rest) {
    out.push_back(const_cast<char *>(first));
    addArg(out, rest...);
}

template <typename... T>
std::unique_ptr<char *const[]> make_argv(const char *name, T ...args) {
    std::vector<char *> out;
    addArg(out, name, args...);
    unsigned int size = out.size();
    char **ptr = new char*[size + 1];
    for(unsigned int i = 0; i < size; i++) {
        ptr[i] = out[i];
    }
    ptr[size] = nullptr;
    return std::unique_ptr<char *const[]>(ptr);
}



TEST(BuiltinExecTest, case1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        DSContext *ctx = DSContext_create();
        DSStatus *s;

        int ret = DSContext_exec(ctx, make_argv("echo", "hello").get(), &s);
        ASSERT_EQ(0, ret);
        ASSERT_EQ(DS_STATUS_SUCCESS, DSStatus_getType(s));

        DSStatus_free(&s);
        DSContext_delete(&ctx);
    });
}

TEST(BuiltinExecTest, case2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        DSContext *ctx = DSContext_create();
        DSStatus *s;

        int ret = DSContext_exec(ctx, make_argv("fheruifh", "hello").get(), &s);
        ASSERT_EQ(1, ret);
        ASSERT_EQ(DS_STATUS_SUCCESS, DSStatus_getType(s));  // if command not found, still success.

        DSStatus_free(&s);
        DSContext_delete(&ctx);
    });
}

TEST(BuiltinExecTest, case3) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        DSContext *ctx = DSContext_create();
        DSStatus *s;

        int ret = DSContext_exec(ctx, make_argv("exit", "12").get(), &s);
        ASSERT_EQ(12, ret);
        ASSERT_EQ(DS_STATUS_EXIT, DSStatus_getType(s));
        ASSERT_EQ(0, DSStatus_getErrorLineNum(s));  // error line num is always 0.

        DSStatus_free(&s);
        DSContext_delete(&ctx);
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}