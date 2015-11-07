#include <gtest/gtest.h>

#include <fstream>

#include <ydsh/ydsh.h>
#include <misc/files.h>
#include <misc/num.h>
#include <directive.h>


#ifndef EXEC_TEST_DIR
#define EXEC_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh;
using namespace ydsh::directive;
using namespace ydsh::misc;

// parse config(key = value)

bool isSpace(char ch) {
    switch(ch) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
        return true;
    default:
        return false;
    }
}

void consumeSpace(const std::string &src, unsigned int &index) {
    for(; index < src.size(); index++) {
        if(!isSpace(src[index])) {
            return;
        }
    }
}

int extract(const std::string &src, unsigned int &index, unsigned int &first) {
    consumeSpace(src, index);

    std::string buf;
    for(; index < src.size(); index++) {
        char ch = src[index];
        if(!isdigit(ch)) {
            break;
        }
        buf += ch;
    }
    int status;
    long value = convertToInt64(buf.c_str(), status, false);
    if(status != 0 || value < 0 || value > UINT32_MAX) {
        return 1;
    }
    first = (unsigned int) value;
    return 0;
}

int extract(const std::string &src, unsigned int &index, std::string &first) {
    consumeSpace(src, index);

    for(; index < src.size(); index++) {
        char ch = src[index];
        if(isSpace(ch)) {
            break;
        }
        first += ch;
    }
    return 0;
}

int extract(const std::string &src, unsigned int &index, const char *first) {
    consumeSpace(src, index);

    for(unsigned int i = 0; first[i] != '\0'; i++) {
        if(index >= src.size()) {
            return 1;   // not match
        }
        if(src[index++] != first[i]) {
            return 1;
        }
    }
    return 0;
}

int parseImpl(const std::string &src, unsigned int &index) {
    consumeSpace(src, index);
    return index - src.size();
}

template <typename F, typename ...T>
int parseImpl(const std::string &src, unsigned int &index, F &&first, T&& ...args) {
    int ret = extract(src, index, std::forward<F>(first));
    return ret == 0 ? parseImpl(src, index, std::forward<T>(args)...) : ret;
}

template <typename ...T>
int parse(const std::string &src, T&& ...args) {
    unsigned int index = 0;
    return parseImpl(src, index, std::forward<T>(args)...);
}

template <typename ...T>
int parse(const char *src, T&& ...args) {
    std::string str(src);
    return parse(str, std::forward<T>(args)...);
}



class ExecTest : public ::testing::TestWithParam<std::string> {
private:
    std::string tmpFileName;
    std::string targetName;

public:
    ExecTest() : tmpFileName(), targetName() { }

    virtual ~ExecTest() = default;

    virtual void SetUp() {
        const char *tmpdir = getenv("TMPDIR");
        if(tmpdir == nullptr) {
            tmpdir = "/tmp";
        }
        unsigned int size = 512;
        char name[size];
        snprintf(name, size, "%s/exec_test_tmpXXXXXX", tmpdir);

        int fd = mkstemp(name);
        close(fd);
        this->tmpFileName = name;
        this->targetName = this->GetParam();
    }

    virtual void TearDown() {
        remove(this->tmpFileName.c_str());
    }

    virtual const std::string &getTmpFileName() {
        return this->tmpFileName;
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
        RunCondition haveDBus = DSContext_supportDBus() ? RunCondition::TRUE : RunCondition::FALSE;
        if(d.getIfHaveDBus() != RunCondition::IGNORE && haveDBus != d.getIfHaveDBus()) {
            return; // do nothing
        }

        const char *scriptName = this->getSourceName().c_str();
        std::string cmd(BIN_PATH);
        cmd += " --status-log ";
        cmd += this->getTmpFileName();

        // set argument
        std::unique_ptr<char *[]> argv = d.getAsArgv(scriptName);
        for(unsigned int i = 0; argv[i] != nullptr; i++) {
            cmd += " ";
            cmd += '"';
            cmd += argv[i];
            cmd += '"';
        }

        // execute
        int ret = system(cmd.c_str());
        ret = WEXITSTATUS(ret);

        // get internal status
        std::ifstream input(this->getTmpFileName());
        ASSERT_FALSE(!input);

        std::string line;
        std::getline(input, line);
        ASSERT_FALSE(line.empty());

        unsigned int type;
        unsigned int lineNum;
        std::string kind;

        int r = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
        ASSERT_EQ(0, r);

        // check status
        ASSERT_EQ(d.getResult(), type);
        ASSERT_EQ(d.getLineNum(), lineNum);
        ASSERT_EQ(d.getStatus(), static_cast<unsigned int>(ret));
        ASSERT_EQ(d.getErrorKind(), kind);
    }
};

TEST_P(ExecTest, baseTest) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->doTest();
    });
}

INSTANTIATE_TEST_CASE_P(ExecTest, ExecTest, ::testing::ValuesIn(getFileList(EXEC_TEST_DIR, true)));


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

TEST(Base, case1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        std::string line("type=3 lineNum=1 kind=SystemError");
        unsigned int type;
        unsigned int lineNum;
        std::string kind;

        int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
        ASSERT_EQ(0, ret);
        ASSERT_EQ(3u, type);
        ASSERT_EQ(1u, lineNum);
        ASSERT_EQ("SystemError", kind);
    });
}

TEST(Base, case2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        std::string line("type=0 lineNum=0 kind=");
        unsigned int type;
        unsigned int lineNum;
        std::string kind;

        int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
        ASSERT_EQ(0, ret);
        ASSERT_EQ(0u, type);
        ASSERT_EQ(0u, lineNum);
        ASSERT_EQ("", kind);
    });
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
        ASSERT_EQ(0u, DSStatus_getErrorLineNum(s));  // error line num is always 0.

        DSStatus_free(&s);
        DSContext_delete(&ctx);
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}