#include "gtest/gtest.h"

#include <fstream>

#include <ydsh/ydsh.h>
#include <constant.h>
#include <vm.h>

#include "../test_common.h"


class HistoryTest : public ExpectOutput, public TempFileFactory {
protected:
    DSState *state{nullptr};
    bool evaled{false};

public:
    HistoryTest() {
        this->state = DSState_create();
        std::string value;
        value += '"';
        value += this->tmpFileName;
        value += '"';
        this->assignValue(VAR_HISTFILE, std::move(value));
    }

    ~HistoryTest() override {
        DSState_delete(&this->state);
    }

    void setHistSize(unsigned int size, bool sync = true) {
        this->assignUintValue(VAR_HISTSIZE, size);
        if(sync) {
            DSState_syncHistorySize(this->state);
        }
    }

    void setHistFileSize(unsigned int size) {
        this->assignUintValue(VAR_HISTFILESIZE, size);
    }

    Output evalInChild(const std::string &code) {
        IOConfig config{IOConfig::INHERIT, IOConfig::INHERIT, IOConfig::INHERIT};
        return ProcBuilder::spawn(config, [&] {
            int ret = DSState_eval(this->state, nullptr, code.c_str(), code.size(), nullptr);
            return ret;
        }).waitAndGetResult(true);
    }

    void eval(const char *code) {
        if(!this->evaled) {
            this->evaled = true;
            const char *func = R"EOF(
            function assertEach($expect : [String], $actual : [String]) {
                assert $expect.size() == $actual.size() : "size: ${$expect.size()} != ${$actual.size()}"
                let size = $expect.size()
                for(var i = 0; $i < $size; $i++) {
                    assert $expect[$i] == $actual[$i] : "expect[$i] = ${$expect[$i]}, actual[$i] = ${$actual[$i]}"
                }
            }
            )EOF";
            DSError e;
            DSState_eval(this->state, "(builtin)", func, strlen(func), &e);
            auto kind = e.kind;
            DSError_release(&e);
            ASSERT_EQ(DS_ERROR_KIND_SUCCESS, kind);
        }
        std::string c(code);
        c += "\n";
        c += "exit $?";

        auto result = this->evalInChild(c);
        ExpectOutput::expect(result, 0, WaitStatus::EXITED);
    }

private:
    void assignValue(const char *varName, std::string &&value) {
        std::string str = "$";
        str += varName;
        str += " = ";
        str += value;

        auto r = DSState_eval(this->state, "(dummy)", str.c_str(), str.size(), nullptr);
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
    auto *history = DSState_history(this->state);

    // default size
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, DSHistory_size(history)));

    // after synchronization (call DSState_syncHistory)
    this->setHistSize(100, false);
    DSState_syncHistorySize(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, DSHistory_size(history)));
}

TEST_F(HistoryTest, add) {
    this->setHistSize(2);
    DSState_addHistory(this->state, "aaa");
    DSState_addHistory(this->state, "bbb");

    auto *history = DSState_history(this->state);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", DSHistory_get(history, 1)));

    DSState_addHistory(this->state, "ccc");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ccc", DSHistory_get(history, 1)));

    DSState_addHistory(this->state, "ccc");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, DSHistory_size(history)));
}


TEST_F(HistoryTest, set) {
    SCOPED_TRACE("");

    this->setHistSize(10);
    DSState_addHistory(this->state, "aaa");
    DSState_addHistory(this->state, "bbb");

    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, DSHistory_size(history)));

    DSHistory_set(history, 1, "ccc");
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ccc", DSHistory_get(history, 1)));

    DSHistory_set(history, 3, "ccc");    // do nothing, if out of range
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, DSHistory_size(history)));

    DSHistory_set(history, 1000, "ccc");    // do nothing, if out of range
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, DSHistory_size(history)));
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
    DSHistory_delete(history, 2);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", DSHistory_get(history, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", DSHistory_get(history, 2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("eee", DSHistory_get(history, 3)));

    DSHistory_delete(history, 3);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", DSHistory_get(history, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", DSHistory_get(history, 2)));

    // do nothing, if out of range
    DSHistory_delete(history, 6);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", DSHistory_get(history, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", DSHistory_get(history, 2)));

    // do nothing, if out of range
    DSHistory_delete(history, 600);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("aaa", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("bbb", DSHistory_get(history, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ddd", DSHistory_get(history, 2)));
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
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, DSHistory_size(history)));

    DSState_clearHistory(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, DSHistory_size(history)));
}

TEST_F(HistoryTest, resize) {
    this->setHistSize(10);
    for(unsigned int i = 0; i < 10; i++) {
        DSState_addHistory(this->state, std::to_string(i).c_str());
    }

    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, DSHistory_size(history)));

    this->setHistSize(5);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("0", DSHistory_get(history, 0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("1", DSHistory_get(history, 1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("2", DSHistory_get(history, 2)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("3", DSHistory_get(history, 3)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("4", DSHistory_get(history, 4)));
}

TEST_F(HistoryTest, file) {
    this->setHistSize(10);
    for(unsigned int i = 0; i < 10; i++) {
        DSState_addHistory(this->state, std::to_string(i).c_str());
    }

    auto *history = DSState_history(this->state);
    this->setHistFileSize(15);
    /**
     * 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
     */
    DSState_saveHistory(this->state, nullptr);
    DSState_clearHistory(this->state);
    DSState_loadHistory(this->state, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, DSHistory_size(history)));

    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i).c_str(), DSHistory_get(history, i)));
    }

    for(unsigned int i = 0; i < 5; i++) {
        DSState_addHistory(this->state, std::to_string(i + 10).c_str());
    }

    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 5).c_str(), DSHistory_get(history, i)));
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
    DSState_saveHistory(this->state, nullptr);
    DSState_clearHistory(this->state);
    this->setHistSize(15);
    DSState_loadHistory(this->state, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, DSHistory_size(history)));
    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 5).c_str(), DSHistory_get(history, i)));
    }

    // not overwrite history file when buffer size is 0
    DSState_clearHistory(this->state);
    DSState_saveHistory(this->state, nullptr);
    DSState_loadHistory(this->state, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(10u, DSHistory_size(history)));
    for(unsigned int i = 0; i < 10; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 5).c_str(), DSHistory_get(history, i)));
    }

    // not overwrite history file when hist file size is 0
    this->setHistFileSize(0);
    DSState_clearHistory(this->state);
    DSState_addHistory(this->state, "hoge");
    DSState_saveHistory(this->state, nullptr);
    DSState_loadHistory(this->state, nullptr);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(11u, DSHistory_size(history)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hoge", DSHistory_get(history, 0)));
    for(unsigned int i = 1; i < 11; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i + 4).c_str(), DSHistory_get(history, i)));
    }
}

TEST_F(HistoryTest, file2) {
    this->setHistFileSize(DS_HISTFILESIZE_LIMIT + 10);
    this->setHistSize(DS_HISTFILESIZE_LIMIT);

    for(unsigned int i = 0; i < DS_HISTFILESIZE_LIMIT; i++) {
        DSState_addHistory(this->state, std::to_string(i).c_str());
    }
    
    auto *history = DSState_history(this->state);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_HISTFILESIZE_LIMIT, DSHistory_size(history)));

    DSState_saveHistory(this->state, nullptr);
    DSState_clearHistory(this->state);
    DSState_loadHistory(this->state, nullptr);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(DS_HISTFILESIZE_LIMIT, DSHistory_size(history)));
    for(unsigned int i = 0; i < DS_HISTFILESIZE_LIMIT; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ(std::to_string(i).c_str(), DSHistory_get(history, i)));
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}