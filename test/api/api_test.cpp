#include <gtest/gtest.h>

#include <memory>

#include <ydsh/ydsh.h>
#include <config.h>
#include <symbol.h>
#include <vm.h>

#include "../test_common.hpp"


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

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MAJOR_VERSION, DSState_majorVersion()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_MINOR_VERSION, DSState_minorVersion()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ((unsigned int)X_INFO_PATCH_VERSION, DSState_patchVersion()));
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

TEST(API, case4) {
    SCOPED_TRACE("");

    // null arguments
    DSState_complete(nullptr, nullptr, 1, nullptr);

    DSState *state = DSState_create();
    DSCandidates c;
    DSState_complete(state, "~", 1, &c);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.values != nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(c.size > 0));

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

class HistoryTest : public ::testing::Test, public TempFileFactory {
protected:
    DSState *state;

public:
    HistoryTest() = default;
    virtual ~HistoryTest() = default;

    virtual void SetUp() {
        this->createTemp();
        this->state = DSState_create();

        std::string value;
        value += '"';
        value += this->tmpFileName;
        value += '"';
        this->assignValue(VAR_HISTFILE, std::move(value));
    }

    virtual void TearDown() {
        this->deleteTemp();
        DSState_delete(&this->state);
    }

    virtual void setHistSize(unsigned int size, bool sync = true) {
        this->assignUintValue(VAR_HISTSIZE, size);
        if(sync) {
            DSState_syncHistorySize(this->state);
        }
    }

    virtual void setHistFileSize(unsigned int size) {
        this->assignUintValue(VAR_HISTFILESIZE, size);
    }


    virtual void assertHistCmd(unsigned int expect) {
        unsigned int index = this->state->symbolTable.lookupHandle(VAR_HISTCMD)->getFieldIndex();
        unsigned int value = typeAs<Int_Object>(this->state->getGlobal(index))->getValue();
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect, value));
    }

private:
    void assignValue(const char *varName, std::string &&value) {
        std::string str = "$";
        str += varName;
        str += " = ";
        str += std::move(value);

        auto r = DSState_eval(this->state, "(dummy)", str.c_str(), nullptr);
        SCOPED_TRACE("");
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(r == 0));
    }

    void assignUintValue(const char *varName, unsigned int value) {
        std::string str = std::to_string(value);
        str += "u";
        this->assignValue(varName, std::move(str));
    }
};

TEST_F(HistoryTest, base) {
    SCOPED_TRACE("");

    // default size
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, DSState_history(this->state)->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, DSState_history(this->state)->capacity));

    // after synchronization (call DSState_syncHistory)
    this->setHistSize(100, false);
    DSState_syncHistorySize(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, DSState_history(this->state)->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(100u, DSState_history(this->state)->capacity));
}

TEST_F(HistoryTest, add) {
    this->setHistSize(2);
    DSState_addHistory(this->state, "aaa");
    DSState_addHistory(this->state, "bbb");

    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", history->data[1]));
    ASSERT_(this->assertHistCmd(3));

    DSState_addHistory(this->state, "ccc");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ccc", history->data[1]));
    ASSERT_(this->assertHistCmd(4));

    DSState_addHistory(this->state, "ccc");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->capacity));
}


TEST_F(HistoryTest, set) {
    SCOPED_TRACE("");

    this->setHistSize(10);
    DSState_addHistory(this->state, "aaa");
    DSState_addHistory(this->state, "bbb");

    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
    ASSERT_(this->assertHistCmd(3));

    DSState_setHistoryAt(this->state, 1, "ccc");
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ccc", history->data[1]));
    ASSERT_(this->assertHistCmd(3));

    DSState_setHistoryAt(this->state, 3, "ccc");    // do nothing, if out of range
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));

    DSState_setHistoryAt(this->state, 1000, "ccc");    // do nothing, if out of range
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
}

TEST_F(HistoryTest, remove) {
    SCOPED_TRACE("");

    this->setHistSize(10);
    DSState_addHistory(this->state, "aaa");
    DSState_addHistory(this->state, "bbb");
    DSState_addHistory(this->state, "ccc");
    DSState_addHistory(this->state, "ddd");
    DSState_addHistory(this->state, "eee");

    auto *history = DSState_history(this->state);
    DSState_deleteHistoryAt(this->state, 2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", history->data[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", history->data[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("eee", history->data[3]));
    ASSERT_(this->assertHistCmd(5));

    DSState_deleteHistoryAt(this->state, 3);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", history->data[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", history->data[2]));
    ASSERT_(this->assertHistCmd(4));

    // do nothing, if out of range
    DSState_deleteHistoryAt(this->state, 6);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", history->data[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", history->data[2]));

    // do nothing, if out of range
    DSState_deleteHistoryAt(this->state, 600);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", history->data[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", history->data[2]));
}

TEST_F(HistoryTest, clear) {
    SCOPED_TRACE("");

    this->setHistSize(10);
    DSState_addHistory(this->state, "aaa");
    DSState_addHistory(this->state, "bbb");
    DSState_addHistory(this->state, "ccc");
    DSState_addHistory(this->state, "ddd");
    DSState_addHistory(this->state, "eee");
    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));

    DSState_clearHistory(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));
    ASSERT_(this->assertHistCmd(1));
}

TEST_F(HistoryTest, resize) {
    this->setHistSize(10);
    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));

    this->setHistSize(10);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity)); // no change

    this->setHistSize(DS_HISTSIZE_LIMIT + 10);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_HISTSIZE_LIMIT, history->capacity));

    for(unsigned int i = 0; i < 10; i++) {
        DSState_addHistory(this->state, std::to_string(i).c_str());
    }

    this->setHistSize(5);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("0", history->data[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("1", history->data[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("2", history->data[2]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("3", history->data[3]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("4", history->data[4]));
}

TEST_F(HistoryTest, file) {
    this->setHistSize(10);
    auto *history = DSState_history(this->state);
    for(unsigned int i = 0; i < 10; i++) {
        DSState_addHistory(this->state, std::to_string(i).c_str());
    }

    this->setHistFileSize(15);
    /**
     * 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
     */
    DSState_saveHistory(this->state);
    DSState_clearHistory(this->state);
    DSState_loadHistory(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->capacity));

    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i).c_str(), history->data[i]));
    }

    for(unsigned int i = 0; i < 5; i++) {
        DSState_addHistory(this->state, std::to_string(i + 10).c_str());
    }

    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 5).c_str(), history->data[i]));
    }

    /**
     * previous hist file content
     * 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
     *
     * newly added content
     * 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
     *
     * current hist file content
     * 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
     */
    DSState_saveHistory(this->state);
    DSState_clearHistory(this->state);
    this->setHistSize(15);
    DSState_loadHistory(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(15u, history->capacity));
    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 5).c_str(), history->data[i]));
    }

    // not overwrite history file when buffer size is 0
    DSState_clearHistory(this->state);
    DSState_saveHistory(this->state);
    DSState_loadHistory(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(15u, history->capacity));
    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 5).c_str(), history->data[i]));
    }

    // not overwrite history file when hist file size is 0
    this->setHistFileSize(0);
    DSState_clearHistory(this->state);
    DSState_addHistory(this->state, "hoge");
    DSState_saveHistory(this->state);
    DSState_loadHistory(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(11u, history->size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(15u, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hoge", history->data[0]));
    for(unsigned int i = 1; i < 11; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 4).c_str(), history->data[i]));
    }
}

TEST_F(HistoryTest, file2) {
    this->setHistFileSize(DS_HISTFILESIZE_LIMIT + 10);
    this->setHistSize(DS_HISTFILESIZE_LIMIT);
    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_HISTFILESIZE_LIMIT, history->capacity));

    for(unsigned int i = 0; i < DS_HISTFILESIZE_LIMIT; i++) {
        DSState_addHistory(this->state, std::to_string(i).c_str());
    }
    DSState_saveHistory(this->state);
    DSState_clearHistory(this->state);
    DSState_loadHistory(this->state);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_HISTFILESIZE_LIMIT, history->capacity));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_HISTFILESIZE_LIMIT, history->size));
    for(unsigned int i = 0; i < DS_HISTFILESIZE_LIMIT; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i).c_str(), history->data[i]));
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}