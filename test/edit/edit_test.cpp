#include <unistd.h>

#include "gtest/gtest.h"

#include <keybind.h>
#include <keycode.h>
#include <line_buffer.h>
#include <token_edit.h>

using namespace arsh;

TEST(CustomActionTest, base) {
  CustomActionMap actionMap;

  // add
  auto *r = actionMap.add("AAA", CustomActionType::INSERT);
  ASSERT_EQ(0, r->second.customActionIndex);
  r = actionMap.add("BBB", CustomActionType::HIST_SELCT);
  ASSERT_EQ(1, r->second.customActionIndex);
  r = actionMap.add("CCC", CustomActionType::REPLACE_WHOLE);
  ASSERT_EQ(2, r->second.customActionIndex);
  r = actionMap.add("DDD", CustomActionType::INSERT);
  ASSERT_EQ(3, r->second.customActionIndex);

  ASSERT_EQ(4, actionMap.size());
  ASSERT_EQ(0, actionMap.getEntries()[0].second.customActionIndex);
  ASSERT_EQ(1, actionMap.getEntries()[1].second.customActionIndex);
  ASSERT_EQ(2, actionMap.getEntries()[2].second.customActionIndex);
  ASSERT_EQ(3, actionMap.getEntries()[3].second.customActionIndex);

  // find
  r = actionMap.find("");
  ASSERT_FALSE(r);
  r = actionMap.find("afref");
  ASSERT_FALSE(r);
  r = actionMap.find("AAA");
  ASSERT_TRUE(r);
  ASSERT_EQ(0, r->second.customActionIndex);

  // find by index
  r = actionMap.findByIndex(2222);
  ASSERT_FALSE(r);
  r = actionMap.findByIndex(0);
  ASSERT_TRUE(r);
  ASSERT_STREQ("AAA", r->first.get());
  ASSERT_EQ(0, r->second.customActionIndex);

  // remove
  actionMap.remove("CCC");
  ASSERT_EQ(3, actionMap.size());
  ASSERT_EQ(0, actionMap.getEntries()[0].second.customActionIndex);
  ASSERT_EQ(1, actionMap.getEntries()[1].second.customActionIndex);
  ASSERT_EQ(3, actionMap.getEntries()[2].second.customActionIndex);

  actionMap.remove("WWWW");
  ASSERT_EQ(3, actionMap.size());

  actionMap.remove("AAA");
  ASSERT_EQ(2, actionMap.size());
  ASSERT_EQ(1, actionMap.getEntries()[0].second.customActionIndex);
  ASSERT_EQ(3, actionMap.getEntries()[1].second.customActionIndex);

  // re-add
  actionMap.add("EEE", CustomActionType::REPLACE_WHOLE_ACCEPT);
  ASSERT_EQ(3, actionMap.size());
  ASSERT_EQ(0, actionMap.getEntries()[0].second.customActionIndex);
  ASSERT_STREQ("EEE", actionMap.getEntries()[0].first.get());
  ASSERT_EQ(1, actionMap.getEntries()[1].second.customActionIndex);
  ASSERT_STREQ("BBB", actionMap.getEntries()[1].first.get());
  ASSERT_EQ(3, actionMap.getEntries()[2].second.customActionIndex);
  ASSERT_STREQ("DDD", actionMap.getEntries()[2].first.get());

  actionMap.add("FFF", CustomActionType::REPLACE_WHOLE);
  ASSERT_EQ(4, actionMap.size());
  ASSERT_EQ(0, actionMap.getEntries()[0].second.customActionIndex);
  ASSERT_STREQ("EEE", actionMap.getEntries()[0].first.get());
  ASSERT_EQ(1, actionMap.getEntries()[1].second.customActionIndex);
  ASSERT_STREQ("BBB", actionMap.getEntries()[1].first.get());
  ASSERT_EQ(2, actionMap.getEntries()[2].second.customActionIndex);
  ASSERT_STREQ("FFF", actionMap.getEntries()[2].first.get());
  ASSERT_EQ(3, actionMap.getEntries()[3].second.customActionIndex);
  ASSERT_STREQ("DDD", actionMap.getEntries()[3].first.get());
}

TEST(KillRingTest, base) {
  KillRing killRing;
  killRing.expand(3);
  ASSERT_FALSE(killRing);
  killRing.add("AAA");
  ASSERT_TRUE(killRing);
  ASSERT_EQ(1, killRing.get().size());
  ASSERT_EQ("AAA", killRing.get()[0]);

  // ignore empty string
  killRing.add("");
  ASSERT_EQ(1, killRing.get().size());
  ASSERT_EQ("AAA", killRing.get()[0]);

  killRing.add("BBB");
  ASSERT_EQ(2, killRing.get().size());
  ASSERT_EQ("AAA", killRing.get()[0]);
  ASSERT_EQ("BBB", killRing.get()[1]);

  killRing.add("CCC");
  ASSERT_EQ(3, killRing.get().size());
  ASSERT_EQ("AAA", killRing.get()[0]);
  ASSERT_EQ("BBB", killRing.get()[1]);
  ASSERT_EQ("CCC", killRing.get()[2]);

  // truncate old item
  killRing.add("DDD");
  ASSERT_EQ(3, killRing.get().size());
  ASSERT_EQ("BBB", killRing.get()[0]);
  ASSERT_EQ("CCC", killRing.get()[1]);
  ASSERT_EQ("DDD", killRing.get()[2]);

  killRing.add("EEE");
  ASSERT_EQ(3, killRing.get().size());
  ASSERT_EQ("CCC", killRing.get()[0]);
  ASSERT_EQ("DDD", killRing.get()[1]);
  ASSERT_EQ("EEE", killRing.get()[2]);

  killRing.add("FFF");
  ASSERT_EQ(3, killRing.get().size());
  ASSERT_EQ("DDD", killRing.get()[0]);
  ASSERT_EQ("EEE", killRing.get()[1]);
  ASSERT_EQ("FFF", killRing.get()[2]);
}

TEST(KillRingTest, pop) {
  KillRing killRing;
  killRing.expand(3);
  ASSERT_FALSE(killRing);
  killRing.add("AAA");
  killRing.add("BBB");
  killRing.add("CCC");
  ASSERT_TRUE(killRing);
  ASSERT_EQ(3, killRing.get().size());
  killRing.reset();
  ASSERT_EQ("CCC", killRing.getCurrent());
  killRing.rotate();
  ASSERT_EQ("BBB", killRing.getCurrent());
  killRing.rotate();
  ASSERT_EQ("AAA", killRing.getCurrent());
  killRing.rotate();
  ASSERT_EQ("CCC", killRing.getCurrent());
}

struct LineBufferTest : public ::testing::Test {
  struct LineIntervalSet {
    unsigned int cursor;
    unsigned int expectIndex;
    std::string line;
    std::string wholeLine;
  };

  static void checkLineInterval(const std::vector<LineIntervalSet> &testSets, LineBuffer &buffer) {
    for (auto &e : testSets) {
      SCOPED_TRACE("cursor: " + std::to_string(e.cursor));
      buffer.setCursor(e.cursor);
      unsigned int index = buffer.findCurNewlineIndex();
      ASSERT_EQ(e.expectIndex, index);

      auto actualLine = buffer.getCurLine(false).toString();
      ASSERT_EQ(e.line, actualLine);
      actualLine = buffer.getCurLine(true).toString();
      ASSERT_EQ(e.wholeLine, actualLine);
    }
  }

  struct TokenEditPattern {
    std::string before;
    unsigned int beforePos;
    std::string after;
    unsigned int afterPos;
  };

  static void testEditLeftToken(const TokenEditPattern &pattern) {
    ASSERT_LE(pattern.beforePos, pattern.before.size());
    ASSERT_LE(pattern.afterPos, pattern.after.size());

    const bool s = pattern.beforePos != pattern.afterPos;
    std::string storage;
    size_t size = std::max(pattern.before.size(), pattern.after.size());
    size += size >> 1;
    storage.resize(size, '@');
    LineBuffer buffer(storage.data(), storage.size());
    ASSERT_EQ(0, buffer.getUsedSize());
    ASSERT_EQ(0, buffer.getCursor());
    ASSERT_EQ("", buffer.get().toString());

    // delete prev token
    ASSERT_TRUE(buffer.insertToCursor(pattern.before));
    buffer.setCursor(pattern.beforePos);
    auto ret = deletePrevToken(buffer, nullptr);
    ASSERT_TRUE(ret.hasValue());
    ASSERT_EQ(s, ret.unwrap());
    ASSERT_EQ(pattern.after, buffer.get().toString());
    ASSERT_EQ(pattern.afterPos, buffer.getCursor());

    // move left
    buffer.deleteAll();
    ASSERT_TRUE(buffer.insertToCursor(pattern.before));
    buffer.setCursor(pattern.beforePos);
    ret = moveCursorToLeftByToken(buffer);
    ASSERT_TRUE(ret.hasValue());
    ASSERT_EQ(s, ret.unwrap());
    ASSERT_EQ(pattern.afterPos, buffer.getCursor());
  }

  static void testEditRightToken(const TokenEditPattern &pattern) {
    ASSERT_LE(pattern.beforePos, pattern.before.size());
    ASSERT_LE(pattern.afterPos, pattern.after.size());

    std::string storage;
    size_t size = std::max(pattern.before.size(), pattern.after.size());
    size += size >> 1;
    storage.resize(size, '@');
    LineBuffer buffer(storage.data(), storage.size());
    ASSERT_EQ(0, buffer.getUsedSize());
    ASSERT_EQ(0, buffer.getCursor());
    ASSERT_EQ("", buffer.get().toString());

    // delete next token
    std::string killed;
    ASSERT_TRUE(buffer.insertToCursor(pattern.before));
    buffer.setCursor(pattern.beforePos);
    auto ret = deleteNextToken(buffer, &killed);
    ASSERT_TRUE(ret.hasValue());
    ASSERT_EQ(!killed.empty(), ret.unwrap());
    ASSERT_EQ(pattern.after, buffer.get().toString());
    ASSERT_EQ(pattern.afterPos, buffer.getCursor());

    // move right
    buffer.deleteAll();
    ASSERT_TRUE(buffer.insertToCursor(pattern.before));
    buffer.setCursor(pattern.beforePos);
    ret = moveCursorToRightByToken(buffer);
    ASSERT_TRUE(ret.hasValue());
    ASSERT_EQ(!killed.empty(), ret.unwrap());
    ASSERT_EQ(pattern.afterPos, buffer.getCursor() - killed.size());
  }

  static void invalidEditLeftToken(const std::string &line, unsigned int pos) {
    std::string storage;
    size_t size = line.size();
    size += size >> 1;
    storage.resize(size, '@');
    LineBuffer buffer(storage.data(), storage.size());
    ASSERT_EQ(0, buffer.getUsedSize());
    ASSERT_EQ(0, buffer.getCursor());
    ASSERT_EQ("", buffer.get().toString());

    // delete prev token
    ASSERT_TRUE(buffer.insertToCursor(line));
    buffer.setCursor(pos);
    auto ret = deletePrevToken(buffer, nullptr);
    ASSERT_FALSE(ret.hasValue());

    // move left
    buffer.deleteAll();
    ASSERT_TRUE(buffer.insertToCursor(line));
    buffer.setCursor(pos);
    ret = moveCursorToLeftByToken(buffer);
    ASSERT_FALSE(ret.hasValue());
  }

  static void invalidEditRightToken(const std::string &line, unsigned int pos) {
    std::string storage;
    size_t size = line.size();
    size += size >> 1;
    storage.resize(size, '@');
    LineBuffer buffer(storage.data(), storage.size());
    ASSERT_EQ(0, buffer.getUsedSize());
    ASSERT_EQ(0, buffer.getCursor());
    ASSERT_EQ("", buffer.get().toString());

    // delete prev token
    ASSERT_TRUE(buffer.insertToCursor(line));
    buffer.setCursor(pos);
    auto ret = deleteNextToken(buffer, nullptr);
    ASSERT_FALSE(ret.hasValue());

    // move left
    buffer.deleteAll();
    ASSERT_TRUE(buffer.insertToCursor(line));
    buffer.setCursor(pos);
    ret = moveCursorToRightByToken(buffer);
    ASSERT_FALSE(ret.hasValue());
  }
};

TEST_F(LineBufferTest, base) {
  std::string storage;
  storage.resize(16, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ(0, buffer.getUsedSize());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_EQ("", buffer.get().toString());

  // insert
  ASSERT_TRUE(buffer.insertToCursor("1234"));
  ASSERT_EQ(4, buffer.getUsedSize());
  ASSERT_EQ(4, buffer.getCursor());
  ASSERT_EQ("1234", buffer.get().toString());
  ASSERT_EQ("1234", buffer.getToCursor().toString());
  ASSERT_EQ("", buffer.getFromCursor().toString());

  // insert large data
  ASSERT_FALSE(buffer.insertToCursor("QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQqqqq"));
  ASSERT_EQ(4, buffer.getUsedSize());
  ASSERT_EQ(4, buffer.getCursor());

  // move cursor
  buffer.setCursor(1);
  ASSERT_EQ(1, buffer.getCursor());
  ASSERT_EQ("1234", buffer.get().toString());
  ASSERT_EQ("1", buffer.getToCursor().toString());
  ASSERT_EQ("234", buffer.getFromCursor().toString());

  // delete right of cursor
  ASSERT_TRUE(buffer.deleteFromCursor(1));
  ASSERT_EQ(3, buffer.getUsedSize());
  ASSERT_EQ(1, buffer.getCursor());
  ASSERT_EQ("134", buffer.get().toString());
  ASSERT_EQ("1", buffer.getToCursor().toString());
  ASSERT_EQ("34", buffer.getFromCursor().toString());

  // delete left of cursor
  ASSERT_TRUE(buffer.deleteToCursor(1));
  ASSERT_EQ(2, buffer.getUsedSize());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_EQ("34", buffer.get().toString());
  ASSERT_EQ("", buffer.getToCursor().toString());
  ASSERT_EQ("34", buffer.getFromCursor().toString());

  // delete fail
  ASSERT_FALSE(buffer.deleteToCursor(0));
  ASSERT_FALSE(buffer.deleteToCursor(4));
  ASSERT_FALSE(buffer.deleteFromCursor(0));
  ASSERT_FALSE(buffer.deleteFromCursor(4));
}

TEST_F(LineBufferTest, charOp) {
  std::string storage;
  storage.resize(16, '@');
  LineBuffer buffer(storage.data(), storage.size());

  ASSERT_TRUE(buffer.insertToCursor("あいう"));
  ASSERT_EQ(9, buffer.getUsedSize());
  ASSERT_EQ(9, buffer.getCursor());

  // char op
  auto retSize = buffer.nextCharBytes();
  ASSERT_EQ(0, retSize);
  retSize = buffer.prevCharBytes();
  ASSERT_EQ(3, retSize);
  buffer.setCursor(buffer.getCursor() - retSize);
  ASSERT_EQ("あい", buffer.getToCursor().toString());
  ASSERT_EQ("う", buffer.getFromCursor().toString());
  retSize = buffer.nextCharBytes();
  ASSERT_EQ(3, retSize);
  buffer.setCursor(0);
  ASSERT_EQ(0, buffer.prevCharBytes());

  buffer.deleteAll();
  ASSERT_EQ(0, buffer.getUsedSize());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_TRUE(buffer.insertToCursor("あ111い"));
  ASSERT_EQ(9, buffer.getCursor());

  // word op
  retSize = buffer.nextWordBytes();
  ASSERT_EQ(0, retSize);
  retSize = buffer.prevWordBytes();
  ASSERT_EQ(3, retSize);
  buffer.setCursor(buffer.getCursor() - retSize);
  ASSERT_EQ("あ111", buffer.getToCursor().toString());
  ASSERT_EQ("い", buffer.getFromCursor().toString());
  retSize = buffer.prevWordBytes();
  ASSERT_EQ(3, retSize);
  retSize = buffer.nextWordBytes();
  ASSERT_EQ(3, retSize);
  buffer.setCursor(0);
  ASSERT_EQ(0, buffer.prevWordBytes());
}

TEST_F(LineBufferTest, cursor1) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\nあいうえ\n456"));

  // left char
  buffer.setCursor(2222);
  ASSERT_EQ(20, buffer.getCursor());
  ASSERT_TRUE(buffer.moveCursorToLeftByChar());
  ASSERT_EQ(19, buffer.getCursor());
  buffer.setCursor(0);
  ASSERT_FALSE(buffer.moveCursorToLeftByChar());

  // right char
  ASSERT_TRUE(buffer.moveCursorToRightByChar());
  ASSERT_EQ(1, buffer.getCursor());
  buffer.setCursor(2222);
  ASSERT_FALSE(buffer.moveCursorToRightByChar());

  // left word
  ASSERT_TRUE(buffer.moveCursorToLeftByWord());
  ASSERT_EQ(17, buffer.getCursor());
  buffer.setCursor(0);
  ASSERT_FALSE(buffer.moveCursorToLeftByWord());

  // right word
  ASSERT_TRUE(buffer.moveCursorToRightByWord());
  ASSERT_EQ(3, buffer.getCursor());
  buffer.setCursor(2222);
  ASSERT_FALSE(buffer.moveCursorToRightByWord());
}

TEST_F(LineBufferTest, cursor2) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\nあいうえ\n456"));
  buffer.syncNewlinePosList();
  ASSERT_EQ(20, buffer.getCursor());

  // move to start
  ASSERT_TRUE(buffer.moveCursorToStartOfLine());
  ASSERT_EQ(17, buffer.getCursor());

  ASSERT_TRUE(buffer.moveCursorToLeftByChar());
  ASSERT_EQ(16, buffer.getCursor());
  ASSERT_TRUE(buffer.moveCursorToStartOfLine());
  ASSERT_EQ(4, buffer.getCursor());

  ASSERT_TRUE(buffer.moveCursorToLeftByChar());
  ASSERT_EQ(3, buffer.getCursor());
  ASSERT_TRUE(buffer.moveCursorToStartOfLine());
  ASSERT_EQ(0, buffer.getCursor());

  ASSERT_FALSE(buffer.moveCursorToStartOfLine());

  // move to end
  ASSERT_TRUE(buffer.moveCursorToEndOfLine());
  ASSERT_EQ(3, buffer.getCursor());

  ASSERT_TRUE(buffer.moveCursorToRightByChar());
  ASSERT_TRUE(buffer.moveCursorToRightByChar());
  ASSERT_EQ(7, buffer.getCursor());
  ASSERT_TRUE(buffer.moveCursorToEndOfLine());
  ASSERT_EQ(16, buffer.getCursor());

  ASSERT_TRUE(buffer.moveCursorToRightByChar());
  ASSERT_EQ(17, buffer.getCursor());
  ASSERT_TRUE(buffer.moveCursorToEndOfLine());
  ASSERT_EQ(20, buffer.getCursor());

  ASSERT_FALSE(buffer.moveCursorToEndOfLine());
}

TEST_F(LineBufferTest, cursor3) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\nあいうえ\n456"));
  buffer.syncNewlinePosList();
  ASSERT_EQ(20, buffer.getCursor());

  // up
  ASSERT_TRUE(buffer.moveCursorUpDown(true));
  ASSERT_EQ(13, buffer.getCursor());

  ASSERT_TRUE(buffer.moveCursorUpDown(true));
  ASSERT_EQ(3, buffer.getCursor());

  ASSERT_FALSE(buffer.moveCursorUpDown(true));

  // down
  ASSERT_TRUE(buffer.moveCursorUpDown(false));
  ASSERT_EQ(13, buffer.getCursor());

  buffer.setCursor(2);
  ASSERT_TRUE(buffer.moveCursorUpDown(false));
  ASSERT_EQ(10, buffer.getCursor());

  ASSERT_TRUE(buffer.moveCursorUpDown(false));
  ASSERT_EQ(19, buffer.getCursor());

  ASSERT_FALSE(buffer.moveCursorUpDown(false));
}

TEST_F(LineBufferTest, deleteOut) {
  std::string storage;
  storage.resize(16, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("あいう"));
  buffer.setCursor(3);
  ASSERT_EQ("あ", buffer.getToCursor().toString());
  ASSERT_EQ("いう", buffer.getFromCursor().toString());

  std::string out;
  ASSERT_TRUE(buffer.deleteToCursor(3, &out));
  ASSERT_EQ("あ", out);
  ASSERT_EQ("いう", buffer.getFromCursor().toString());
  ASSERT_TRUE(buffer.deleteFromCursor(3, &out));
  ASSERT_EQ("い", out);
  ASSERT_EQ("う", buffer.getFromCursor().toString());
}

TEST_F(LineBufferTest, newline1) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123")); // not end with newline
  ASSERT_EQ(3, buffer.getCursor());
  ASSERT_EQ(3, buffer.getUsedSize());
  buffer.syncNewlinePosList();
  ASSERT_TRUE(buffer.isSingleLine());
  auto &newlinePosList = buffer.getNewlinePosList();
  ASSERT_EQ(0, newlinePosList.size());

  unsigned int r = buffer.findCurNewlineIndex();
  ASSERT_EQ(0, r);

  std::vector<LineIntervalSet> table = {
      {0, 0, "", "123"},
      {1, 0, "1", "123"},
      {2, 0, "12", "123"},
      {3, 0, "123", "123"},
  };
  ASSERT_NO_FATAL_FAILURE(checkLineInterval(table, buffer));
}

TEST_F(LineBufferTest, newline2) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\n456\n789")); // not end with newline
  ASSERT_EQ(11, buffer.getCursor());
  ASSERT_EQ(11, buffer.getUsedSize());
  buffer.syncNewlinePosList();
  ASSERT_FALSE(buffer.isSingleLine());
  auto &newlinePosList = buffer.getNewlinePosList();
  ASSERT_EQ(2, newlinePosList.size());
  ASSERT_EQ(3, newlinePosList[0]);
  ASSERT_EQ(7, newlinePosList[1]);

  unsigned int r = buffer.findCurNewlineIndex();
  ASSERT_EQ(2, r);

  std::vector<LineIntervalSet> table = {
      {0, 0, "", "123"}, {1, 0, "1", "123"}, {2, 0, "12", "123"},  {3, 0, "123", "123"},
      {4, 1, "", "456"}, {5, 1, "4", "456"}, {6, 1, "45", "456"},  {7, 1, "456", "456"},
      {8, 2, "", "789"}, {9, 2, "7", "789"}, {10, 2, "78", "789"}, {11, 2, "789", "789"},
  };
  ASSERT_NO_FATAL_FAILURE(checkLineInterval(table, buffer));
}

TEST_F(LineBufferTest, newline3) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_TRUE(buffer.insertToCursor("123\n456\n789\n")); // end with a newline
  ASSERT_EQ(12, buffer.getCursor());
  ASSERT_EQ(12, buffer.getUsedSize());
  buffer.syncNewlinePosList();
  ASSERT_FALSE(buffer.isSingleLine());
  auto &newlinePosList = buffer.getNewlinePosList();
  ASSERT_EQ(3, newlinePosList.size());
  ASSERT_EQ(3, newlinePosList[0]);
  ASSERT_EQ(7, newlinePosList[1]);
  ASSERT_EQ(11, newlinePosList[2]);

  unsigned int r = buffer.findCurNewlineIndex();
  ASSERT_EQ(3, r);

  std::vector<LineIntervalSet> table = {
      {0, 0, "", "123"}, {1, 0, "1", "123"}, {2, 0, "12", "123"},  {3, 0, "123", "123"},
      {4, 1, "", "456"}, {5, 1, "4", "456"}, {6, 1, "45", "456"},  {7, 1, "456", "456"},
      {8, 2, "", "789"}, {9, 2, "7", "789"}, {10, 2, "78", "789"}, {11, 2, "789", "789"},
      {12, 3, "", ""},
  };
  ASSERT_NO_FATAL_FAILURE(checkLineInterval(table, buffer));
}

TEST_F(LineBufferTest, undoInsert1) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());
  ASSERT_FALSE(buffer.undo());

  // insert
  ASSERT_TRUE(buffer.insertToCursor("123\n456\n"));
  ASSERT_EQ(8, buffer.getCursor());
  buffer.setCursor(3);
  ASSERT_TRUE(buffer.insertToCursor("789"));
  ASSERT_EQ("123789\n456\n", buffer.get().toString());
  ASSERT_EQ(6, buffer.getCursor());
  buffer.setCursor(0);

  // undo
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123\n456\n", buffer.get().toString());
  ASSERT_EQ(3, buffer.getCursor());
  buffer.setCursor(2);

  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("", buffer.get().toString());
  ASSERT_EQ(0, buffer.getCursor());

  ASSERT_FALSE(buffer.undo());

  // redo
  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123\n456\n", buffer.get().toString());
  ASSERT_EQ(8, buffer.getCursor());
  buffer.setCursor(5);

  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123789\n456\n", buffer.get().toString());
  ASSERT_EQ(6, buffer.getCursor());

  ASSERT_FALSE(buffer.redo());
}

TEST_F(LineBufferTest, undoInsert2) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());
  ASSERT_FALSE(buffer.undo());

  ASSERT_TRUE(buffer.insertToCursor(""));
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_EQ("", buffer.get().toString());
  ASSERT_TRUE(buffer.undo());
  ASSERT_FALSE(buffer.undo());

  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ(0, buffer.getCursor());
  ASSERT_EQ("", buffer.get().toString());
}

TEST_F(LineBufferTest, undoDelete) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());

  ASSERT_TRUE(buffer.insertToCursor("123\n456\n789"));
  buffer.setCursor(4);
  ASSERT_TRUE(buffer.deleteFromCursor(3));
  ASSERT_EQ("123\n\n789", buffer.get().toString());
  ASSERT_EQ(4, buffer.getCursor());
  buffer.setCursor(6);
  ASSERT_TRUE(buffer.deleteToCursor(2));
  ASSERT_EQ("123\n89", buffer.get().toString());
  ASSERT_EQ(4, buffer.getCursor());
  buffer.setCursor(0);

  // undo
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123\n\n789", buffer.get().toString());
  ASSERT_EQ(6, buffer.getCursor());
  buffer.setCursor(8);

  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123\n456\n789", buffer.get().toString());
  ASSERT_EQ(4, buffer.getCursor());

  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("", buffer.get().toString());
  ASSERT_EQ(0, buffer.getCursor());

  ASSERT_FALSE(buffer.undo());

  // redo
  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123\n456\n789", buffer.get().toString());
  ASSERT_EQ(11, buffer.getCursor());
  buffer.setCursor(1);

  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123\n\n789", buffer.get().toString());
  ASSERT_EQ(4, buffer.getCursor());
  buffer.setCursor(7);

  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123\n89", buffer.get().toString());
  ASSERT_EQ(4, buffer.getCursor());

  ASSERT_FALSE(buffer.redo());
}

TEST_F(LineBufferTest, mergeInsert) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());

  // merge
  ASSERT_TRUE(buffer.insertToCursor("123"));
  ASSERT_TRUE(buffer.insertToCursor("456", true));
  ASSERT_TRUE(buffer.insertToCursor("7", true));
  ASSERT_TRUE(buffer.insertToCursor("8", false));
  ASSERT_EQ("12345678", buffer.get().toString());

  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("1234567", buffer.get().toString());
  ASSERT_EQ(7, buffer.getCursor());
  buffer.setCursor(6);

  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123", buffer.get().toString());
  ASSERT_EQ(3, buffer.getCursor());

  ASSERT_TRUE(buffer.insertToCursor("@@", true));
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123", buffer.get().toString());
  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123@@", buffer.get().toString());
  ASSERT_TRUE(buffer.insertToCursor("!!", true));
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123@@", buffer.get().toString());
  ASSERT_TRUE(buffer.redo());
  ASSERT_EQ("123@@!!", buffer.get().toString());
}

TEST_F(LineBufferTest, mergeDelete) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());

  ASSERT_TRUE(buffer.insertToCursor("123\n456\n789"));
  ASSERT_TRUE(buffer.deletePrevChar(nullptr, true));
  ASSERT_TRUE(buffer.deleteToCursor(3, nullptr, true));
  ASSERT_EQ("123\n456", buffer.get().toString());
  buffer.setCursor(2);
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123\n456\n789", buffer.get().toString());

  buffer.setCursor(4);
  ASSERT_TRUE(buffer.deleteNextWord(nullptr));
  ASSERT_EQ("123\n\n789", buffer.get().toString());
  ASSERT_TRUE(buffer.deleteNextChar(nullptr, true));
  ASSERT_TRUE(buffer.deleteFromCursor(1, nullptr, true));
  ASSERT_EQ("123\n89", buffer.get().toString());
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123\n\n789", buffer.get().toString());
  ASSERT_TRUE(buffer.undo());
  ASSERT_EQ("123\n456\n789", buffer.get().toString());
}

TEST_F(LineBufferTest, insertingSuffix) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());

  ASSERT_TRUE(buffer.insertToCursor("$(ll && ll"));
  ASSERT_EQ(10, buffer.getCursor());

  // match prefix
  StringRef prefix = "llvm-";
  ASSERT_EQ(8, buffer.resolveInsertingSuffix(prefix, true));
  ASSERT_EQ("vm-", prefix.toString());
  prefix = "llvm-";
  ASSERT_EQ(8, buffer.resolveInsertingSuffix(prefix, false));
  ASSERT_EQ("vm-", prefix.toString());

  prefix = "l";
  ASSERT_EQ(9, buffer.resolveInsertingSuffix(prefix, true));
  ASSERT_EQ("", prefix.toString());
  prefix = "l";
  ASSERT_EQ(9, buffer.resolveInsertingSuffix(prefix, false));
  ASSERT_EQ("", prefix.toString());

  prefix = "";
  ASSERT_EQ(10, buffer.resolveInsertingSuffix(prefix, true));
  ASSERT_EQ("", prefix.toString());
  prefix = "";
  ASSERT_EQ(10, buffer.resolveInsertingSuffix(prefix, false));
  ASSERT_EQ("", prefix.toString());

  // unmatch prefix
  prefix = "123";
  ASSERT_EQ(10, buffer.resolveInsertingSuffix(prefix, true));
  ASSERT_EQ("123", prefix.toString());
  prefix = "123";
  ASSERT_EQ(10, buffer.resolveInsertingSuffix(prefix, false));
  ASSERT_EQ("", prefix.toString());

  buffer.deleteAll();
  buffer.insertToCursor("$(ll && ll ");
  prefix = "llvm";
  ASSERT_EQ(11, buffer.resolveInsertingSuffix(prefix, true));
  ASSERT_EQ("llvm", prefix.toString());
  prefix = "llvm";
  ASSERT_EQ(11, buffer.resolveInsertingSuffix(prefix, false));
  ASSERT_EQ("", prefix.toString());
}

TEST_F(LineBufferTest, tokenEditInvalid) {
  std::string storage;
  storage.resize(32, '@');
  LineBuffer buffer(storage.data(), storage.size());
  ASSERT_EQ("", buffer.get().toString());

  ASSERT_TRUE(buffer.insertToCursor("var $123"));
  ASSERT_EQ(8, buffer.getCursor());

  ASSERT_FALSE(deletePrevToken(buffer, nullptr).hasValue());
  ASSERT_FALSE(deleteNextToken(buffer, nullptr).hasValue());
  ASSERT_FALSE(moveCursorToLeftByToken(buffer).hasValue());
  ASSERT_FALSE(moveCursorToRightByToken(buffer).hasValue());

  ASSERT_EQ("var $123", buffer.get());
  ASSERT_EQ(8, buffer.getCursor());
}

TEST_F(LineBufferTest, tokenEditLeftPrev1) {
  const TokenEditPattern patterns[] = {
      {"echo a", 6, "echo ", 5},
      {"echo aa", 7, "echo ", 5},
      {"echo aa 123", 10, "echo aa 3", 8},
      {"echo aa 123", 9, "echo aa 23", 8},
      {"echo aa 123", 8, "echo 123", 5},
      {"echo aa 123", 7, "echo  123", 5},
      {"echo aa 123", 6, "echo a 123", 5},
      {"echo aa 123", 5, "aa 123", 0},
      {"echo aa 123", 4, " aa 123", 0},
      {"   echo aa 123", 3, "echo aa 123", 0},
      {"echo aa 123", 0, "echo aa 123", 0},
      // edit within comment
      {"echo # this is\nls", 5, "# this is\nls", 0},
      // command/command-argument with separator '/'
      {"ho/AAA/BBB", 0, "ho/AAA/BBB", 0},
      {"ho/AAA/BBB", 1, "o/AAA/BBB", 0},
      {"ho/AAA/BBB", 2, "/AAA/BBB", 0},
      {"ho/AAA/BBB", 3, "hoAAA/BBB", 2},
      {"ho/AAA/BBB", 4, "ho/AA/BBB", 3},
      {"ho/AAA/BBB", 5, "ho/A/BBB", 3},
      {"ho/AAA/BBB", 6, "ho//BBB", 3},
      {"ho/AAA/BBB", 7, "ho/AAABBB", 6},
      {"q /ho/   w", 9, "q /ho/w", 6},
      {"q /ho/w", 6, "q /how", 5},
      {"q /how", 5, "q /w", 3},
  };

  for (auto &p : patterns) {
    SCOPED_TRACE("\n>>> " + p.before + "\npos: " + std::to_string(p.beforePos));
    ASSERT_NO_FATAL_FAILURE(testEditLeftToken(p));
  }

  // edit within comment
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 14));
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 13));
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 11));
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 8));
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 7));
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 6));
}

TEST_F(LineBufferTest, tokenEditRightNext1) {
  const TokenEditPattern patterns[] = {
      {"echo aa 123", 0, " aa 123", 0},
      {"echo aa 123", 1, "e aa 123", 1},
      {"echo aa 123", 2, "ec aa 123", 2},
      {"echo aa 123", 3, "ech aa 123", 3},
      {"echo aa 123", 4, "echo 123", 4},
      {"echo aa 123", 5, "echo  123", 5},
      {"echo aa 123", 6, "echo a 123", 6},
      {"echo aa 123", 7, "echo aa", 7},
      {"echo aa 123", 8, "echo aa ", 8},
      {"echo aa 123", 9, "echo aa 1", 9},
      {"echo aa 123", 10, "echo aa 12", 10},
      {"echo aa 123", 11, "echo aa 123", 11},
      // edit within comment
      {"ls # this is\nls", 0, " # this is\nls", 0},
      {"ls # this is\nls", 1, "l # this is\nls", 1},
      {"ls # this is\nls", 2, "ls# this is\nls", 2},
      {"ls    # this is\nls", 2, "ls# this is\nls", 2},
      {"ls    # this is\nls", 3, "ls # this is\nls", 3},
      {"ls # this is\nls", 12, "ls # this isls", 12},
      // command/command-argument with separator '/'
      {"  /ls", 0, "/ls", 0},
      {"  23/ls", 0, "/ls", 0},
      {"ec    /ls", 3, "ec /ls", 3},
      {"ec /ls", 0, " /ls", 0},
      {"ec /ls", 1, "e /ls", 1},
      {"ec /ls", 2, "ec/ls", 2},
      {"ec /ls", 3, "ec ", 3},
      {"ec /ls", 4, "ec /", 4},
      {"ec   12/ls", 2, "ec/ls", 2},
      {"s /ls/fre", 3, "s //fre", 3},
  };

  for (auto &p : patterns) {
    SCOPED_TRACE("\n>>> " + p.before + "\npos: " + std::to_string(p.beforePos));
    ASSERT_NO_FATAL_FAILURE(testEditRightToken(p));
  }

  // edit within comment
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 3));
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 4));
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 5));
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 6));
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 9));
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 10));
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("ls # this is\nls", 11));
}

TEST_F(LineBufferTest, tokenEditInvalidRecover1) {
  const TokenEditPattern patterns[] = {
      {"var ab = 1234", 13, "var ab = ", 9},
      {"var ab = ", 9, "var ab ", 7},
      {"var ab ", 7, "var ", 4},
      {"var ", 4, "", 0},
      {"var a", 5, "var ", 4},
      {"var $", 5, "var ", 4},
      {"var a = '1 @", 12, "var a = ", 8},
      {"var a = '1 @", 3, " a = '1 @", 0},
      {"var a = '1 @", 4, "a = '1 @", 0},
      {"var a = '1 @", 5, "var  = '1 @", 4},
      {"var a = $/12", 12, "var a = ", 8},
      {"var a = `12", 11, "var a = ", 8},
  };

  for (auto &p : patterns) {
    SCOPED_TRACE("\n>>> " + p.before + "\npos: " + std::to_string(p.beforePos));
    ASSERT_NO_FATAL_FAILURE(testEditLeftToken(p));
  }

  // test invalid edit
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("var $AAA", 8));
}

TEST_F(LineBufferTest, tokenEditInvalidRecover2) {
  const TokenEditPattern patterns[] = {
      {"var ab = $", 0, " ab = $", 0},     {"var ab = $", 1, "v ab = $", 1},
      {"var ab = $", 2, "va ab = $", 2},   {"var ab = $", 3, "var = $", 3},
      {"var ab = $", 4, "var  = $", 4},    {"var ab = $", 5, "var a = $", 5},
      {"var ab = $", 6, "var ab $", 6},    {"var ab = $", 7, "var ab  $", 7},
      {"var ab = $", 8, "var ab =", 8},    {"var ab = $", 9, "var ab = ", 9},
      {"var a = $/js", 0, " a = $/js", 0}, {"var a = 'fa", 0, " a = 'fa", 0},
      {"var a = `12", 0, " a = `12", 0},
  };

  for (auto &p : patterns) {
    SCOPED_TRACE("\n>>> " + p.before + "\npos: " + std::to_string(p.beforePos));
    ASSERT_NO_FATAL_FAILURE(testEditRightToken(p));
  }

  // test invalid edit
  ASSERT_NO_FATAL_FAILURE(invalidEditRightToken("var $AAA", 7));
  ASSERT_NO_FATAL_FAILURE(invalidEditLeftToken("echo # this is\nls", 6));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}