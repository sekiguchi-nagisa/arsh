#include "gtest/gtest.h"

#include <fstream>

#include <constant.h>
#include <ydsh/ydsh.h>

#include "../test_common.h"

#ifndef HISTORY_MOD_PATH
#error "require HISTORY_MOD_PATH"
#endif

using namespace ydsh;

class HistoryTest : public ExpectOutput, public TempFileFactory {
protected:
  DSState *state{nullptr};

public:
  HistoryTest() : INIT_TEMP_FILE_FACTORY(history_test) {
    this->state = DSState_create();
    DSState_loadModule(this->state, HISTORY_MOD_PATH, DS_MOD_FULLPATH, nullptr);

    std::string value;
    value += '"';
    value += this->tmpFileName;
    value += '"';
    this->assignValue(VAR_HISTFILE, std::move(value));
  }

  ~HistoryTest() override { DSState_delete(&this->state); }

  void setHistSize(unsigned int size, bool sync = true) {
    this->assignUintValue(VAR_HISTSIZE, size);
    if (sync) {
      this->addHistory(nullptr);
    }
  }

  void setHistFileSize(unsigned int size) { this->assignUintValue(VAR_HISTFILESIZE, size); }

  unsigned int historySize() {
    DSLineEdit edit{};
    DSState_lineEdit(this->state, DS_EDIT_HIST_SIZE, &edit);
    return edit.out;
  }

  const char *getHistory(unsigned int index) {
    DSLineEdit edit{};
    edit.index = index;
    DSState_lineEdit(this->state, DS_EDIT_HIST_GET, &edit);
    return edit.data;
  }

  void setHistory(unsigned int index, const char *line) {
    DSLineEdit edit{};
    edit.index = index;
    edit.data = line;
    DSState_lineEdit(this->state, DS_EDIT_HIST_SET, &edit);
  }

  void delHistory(unsigned int index) {
    DSLineEdit edit{};
    edit.index = index;
    DSState_lineEdit(this->state, DS_EDIT_HIST_DEL, &edit);
  }

  void addHistory(const char *value) {
    DSLineEdit edit{};
    edit.data = value;
    DSState_lineEdit(this->state, DS_EDIT_HIST_ADD, &edit);
  }

  void clearHistory() {
    DSLineEdit edit{};
    DSState_lineEdit(this->state, DS_EDIT_HIST_CLEAR, &edit);
  }

  void loadHistory(const char *fileName = nullptr) {
    DSLineEdit edit{};
    edit.data = fileName;
    DSState_lineEdit(this->state, DS_EDIT_HIST_LOAD, &edit);
  }

  void saveHistory(const char *fileName = nullptr) {
    DSLineEdit edit{};
    edit.data = fileName;
    DSState_lineEdit(this->state, DS_EDIT_HIST_SAVE, &edit);
  }

private:
  void assignValue(const char *varName, std::string &&value) {
    std::string str = "$";
    str += varName;
    str += " = ";
    str += value;

    auto r = DSState_eval(this->state, "(dummy)", str.c_str(), str.size(), nullptr);
    ASSERT_TRUE(r == 0);
  }

  void assignUintValue(const char *varName, unsigned int value) {
    std::string str = std::to_string(value);
    this->assignValue(varName, std::move(str));
  }
};

#define DS_HISTFILESIZE_LIMIT ((unsigned int)4096)

TEST_F(HistoryTest, add) {
  this->setHistSize(2);
  this->addHistory("aaa");
  this->addHistory("bbb");

  ASSERT_EQ(2u, this->historySize());
  ASSERT_STREQ("aaa", this->getHistory(0));
  ASSERT_STREQ("bbb", this->getHistory(1));

  this->addHistory("ccc");
  ASSERT_EQ(2u, this->historySize());
  ASSERT_STREQ("bbb", this->getHistory(0));
  ASSERT_STREQ("ccc", this->getHistory(1));
  ASSERT_EQ(nullptr, this->getHistory(100));

  this->addHistory("ccc");
  ASSERT_EQ(2u, this->historySize());

  // null
  ASSERT_EQ(nullptr, this->getHistory(100));

  // skip empty line
  this->addHistory("");
  ASSERT_EQ(2u, this->historySize());
  ASSERT_STREQ("bbb", this->getHistory(0));
  ASSERT_STREQ("ccc", this->getHistory(1));

  // skip starts with space
  this->addHistory(" fafra");
  ASSERT_EQ(2u, this->historySize());
  ASSERT_STREQ("bbb", this->getHistory(0));
  ASSERT_STREQ("ccc", this->getHistory(1));

  this->addHistory("\tfafra");
  ASSERT_EQ(2u, this->historySize());
  ASSERT_STREQ("bbb", this->getHistory(0));
  ASSERT_STREQ("ccc", this->getHistory(1));

  // skip contains newlines
  this->addHistory("ddddd\neeee");
  ASSERT_EQ(2u, this->historySize());
  ASSERT_STREQ("bbb", this->getHistory(0));
  ASSERT_STREQ("ccc", this->getHistory(1));
}

TEST_F(HistoryTest, set) {
  this->setHistSize(10);
  this->addHistory("aaa");
  this->addHistory("bbb");

  ASSERT_EQ(2u, this->historySize());

  this->setHistory(1, "ccc");
  ASSERT_STREQ("ccc", this->getHistory(1));

  this->setHistory(3, "ccc"); // do nothing, if out of range
  ASSERT_EQ(2u, this->historySize());
  ASSERT_EQ(nullptr, this->getHistory(3));

  this->setHistory(1000, "ccc"); // do nothing, if out of range
  ASSERT_EQ(2u, this->historySize());

  this->setHistory(0, "eeee\nwwww"); // do nothing, if contains newlines
  ASSERT_STREQ("aaa", this->getHistory(0));
}

TEST_F(HistoryTest, remove) {
  this->setHistSize(10);
  this->addHistory("aaa");
  this->addHistory("bbb");
  this->addHistory("ccc");
  this->addHistory("ddd");
  this->addHistory("eee");

  this->delHistory(2);
  ASSERT_EQ(4u, this->historySize());
  ASSERT_STREQ("aaa", this->getHistory(0));
  ASSERT_STREQ("bbb", this->getHistory(1));
  ASSERT_STREQ("ddd", this->getHistory(2));
  ASSERT_STREQ("eee", this->getHistory(3));

  this->delHistory(3);
  ASSERT_EQ(3u, this->historySize());
  ASSERT_STREQ("aaa", this->getHistory(0));
  ASSERT_STREQ("bbb", this->getHistory(1));
  ASSERT_STREQ("ddd", this->getHistory(2));

  // do nothing, if out of range
  this->delHistory(6);
  ASSERT_EQ(3u, this->historySize());
  ASSERT_STREQ("aaa", this->getHistory(0));
  ASSERT_STREQ("bbb", this->getHistory(1));
  ASSERT_STREQ("ddd", this->getHistory(2));

  // do nothing, if out of range
  this->delHistory(600);
  ASSERT_EQ(3u, this->historySize());
  ASSERT_STREQ("aaa", this->getHistory(0));
  ASSERT_STREQ("bbb", this->getHistory(1));
  ASSERT_STREQ("ddd", this->getHistory(2));
}

TEST_F(HistoryTest, clear) {
  this->setHistSize(10);
  this->addHistory("aaa");
  this->addHistory("bbb");
  this->addHistory("ccc");
  this->addHistory("ddd");
  this->addHistory("eee");

  ASSERT_EQ(5u, this->historySize());

  this->clearHistory();
  ASSERT_EQ(0u, this->historySize());
}

TEST_F(HistoryTest, resize) {
  this->setHistSize(10);
  for (unsigned int i = 0; i < 10; i++) {
    this->addHistory(std::to_string(i).c_str());
  }

  ASSERT_EQ(10u, this->historySize());

  this->setHistSize(5);
  ASSERT_EQ(5u, this->historySize());
  ASSERT_STREQ("0", this->getHistory(0));
  ASSERT_STREQ("1", this->getHistory(1));
  ASSERT_STREQ("2", this->getHistory(2));
  ASSERT_STREQ("3", this->getHistory(3));
  ASSERT_STREQ("4", this->getHistory(4));
}

TEST_F(HistoryTest, file) {
  this->setHistSize(10);
  for (unsigned int i = 0; i < 10; i++) {
    this->addHistory(std::to_string(i).c_str());
  }

  this->setHistFileSize(15);
  /**
   * 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
   */
  this->saveHistory();
  this->clearHistory();
  this->loadHistory();
  ASSERT_EQ(10u, this->historySize());

  for (unsigned int i = 0; i < 10; i++) {
    ASSERT_EQ(std::to_string(i), this->getHistory(i));
  }

  for (unsigned int i = 0; i < 5; i++) {
    this->addHistory(std::to_string(i + 10).c_str());
  }

  for (unsigned int i = 0; i < 10; i++) {
    ASSERT_EQ(std::to_string(i + 5), this->getHistory(i));
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
  this->saveHistory();
  this->clearHistory();
  this->setHistSize(15);
  this->loadHistory();
  ASSERT_EQ(10u, this->historySize());
  for (unsigned int i = 0; i < 10; i++) {
    ASSERT_EQ(std::to_string(i + 5), this->getHistory(i));
  }

  // not overwrite history file when buffer size is 0
  this->clearHistory();
  this->saveHistory();
  this->loadHistory();
  ASSERT_EQ(10u, this->historySize());
  for (unsigned int i = 0; i < 10; i++) {
    ASSERT_EQ(std::to_string(i + 5), this->getHistory(i));
  }

  // not overwrite history file when hist file size is 0
  this->setHistFileSize(0);
  this->clearHistory();
  this->addHistory("hoge");
  this->saveHistory();
  this->loadHistory();
  ASSERT_EQ(11u, this->historySize());
  ASSERT_STREQ("hoge", this->getHistory(0));
  for (unsigned int i = 1; i < 11; i++) {
    ASSERT_EQ(std::to_string(i + 4), this->getHistory(i));
  }
}

// TEST_F(HistoryTest, file2) {
//   this->setHistFileSize(DS_HISTFILESIZE_LIMIT + 10);
//   this->setHistSize(DS_HISTFILESIZE_LIMIT);
//
//   for (unsigned int i = 0; i < DS_HISTFILESIZE_LIMIT; i++) {
//     this->addHistory(std::to_string(i).c_str());
//   }
//
//   ASSERT_EQ(DS_HISTFILESIZE_LIMIT, this->historySize());
//
//   this->saveHistory();
//   this->clearHistory();
//   this->loadHistory();
//
//   ASSERT_EQ(DS_HISTFILESIZE_LIMIT, this->historySize());
//   for (unsigned int i = 0; i < DS_HISTFILESIZE_LIMIT; i++) {
//     ASSERT_EQ(std::to_string(i), this->getHistory(i));
//   }
// }

TEST_F(HistoryTest, file3) {
  this->setHistSize(10);
  for (unsigned int i = 0; i < 10; i++) {
    this->addHistory(std::to_string(i).c_str());
  }
  this->setHistFileSize(15);

  std::string fileName = this->getTempDirName();
  fileName += "/hogehogehgoe";

  this->saveHistory(fileName.c_str());
  this->clearHistory();
  this->loadHistory(fileName.c_str());

  ASSERT_EQ(10, this->historySize());
  for (unsigned int i = 0; i < 10; i++) {
    ASSERT_EQ(std::to_string(i), this->getHistory(i));
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}