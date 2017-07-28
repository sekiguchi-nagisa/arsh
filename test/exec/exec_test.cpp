#include <gtest/gtest.h>

#include <fstream>

#include <ydsh/ydsh.h>
#include <misc/files.h>
#include <directive.h>

#include "../test_common.hpp"


#ifndef EXEC_TEST_DIR
#define EXEC_TEST_DIR "."
#endif

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh;
using namespace ydsh::directive;

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
    long value = std::stol(buf);
    if(value < 0 || value > UINT32_MAX) {
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

class ExecTest : public ::testing::TestWithParam<std::string>, public TempFileFactory {
private:
    std::string targetName;

public:
    ExecTest() : targetName() { }

    virtual ~ExecTest() = default;

    virtual void SetUp() {
        this->createTemp();
        this->targetName = this->GetParam();
    }

    virtual void TearDown() {
        this->deleteTemp();
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
        RunCondition haveDBus = DSState_supportDBus() ? RunCondition::TRUE : RunCondition::FALSE;
        if(d.getIfHaveDBus() != RunCondition::IGNORE && haveDBus != d.getIfHaveDBus()) {
            return; // do nothing
        }

        const char *scriptName = this->getSourceName().c_str();
        std::string cmd(BIN_PATH);
        cmd += " --status-log ";
        cmd += this->getTmpFileName();

        // set argument
        auto argv = d.getAsArgv(scriptName);
        for(auto &e : argv) {
            cmd += " ";
            cmd += '"';
            cmd += e;
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

        unsigned int kind;
        unsigned int lineNum;
        std::string name;

        int r = parse(line, "kind", "=", kind, "lineNum", "=", lineNum, "name", "=", name);
        ASSERT_EQ(0, r);

        // check status
        ASSERT_EQ(d.getResult(), kind);
        ASSERT_EQ(d.getLineNum(), lineNum);
        ASSERT_EQ(d.getStatus(), static_cast<unsigned int>(ret));
        ASSERT_EQ(d.getErrorKind(), name);
    }
};

TEST_P(ExecTest, baseTest) {
    SCOPED_TRACE("");
    ASSERT_NO_FATAL_FAILURE(this->doTest());
}

INSTANTIATE_TEST_CASE_P(ExecTest, ExecTest, ::testing::ValuesIn(getFileList(EXEC_TEST_DIR, true)));


TEST(Base, case1) {
    SCOPED_TRACE("");

    std::string line("type=3 lineNum=1 kind=SystemError");
    unsigned int type;
    unsigned int lineNum;
    std::string kind;

    int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, type));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, lineNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("SystemError", kind));
}

TEST(Base, case2) {
    SCOPED_TRACE("");

    std::string line("type=0 lineNum=0 kind=");
    unsigned int type;
    unsigned int lineNum;
    std::string kind;

    int ret = parse(line, "type", "=", type, "lineNum", "=", lineNum, "kind", "=", kind);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, ret));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, type));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, lineNum));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", kind));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}