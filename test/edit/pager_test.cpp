#include "pager_test_base.hpp"

#include <line_buffer.h>
#include <line_renderer.h>
#include <pager.h>

using namespace arsh;

TEST_F(PagerTest, small1) { // less than pager length
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_TRUE(pager.getRows() < pager.getWinSize().rows);
  ASSERT_TRUE(pager.getRows() > array->size() / 2);
  ASSERT_EQ(0, pager.getCurRow());

  const char *expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  std::string out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor up
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor up
  expect = "AAA DDD \r\nBBB \x1b[7mEEE \x1b[0m\r\nCCC FFF \r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor up+up
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToForward();
  pager.moveCursorToForward();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor down+down+down+down
  expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // cursor down
  expect = "AAA DDD \r\n\x1b[7mBBB \x1b[0mEEE \r\nCCC FFF \r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, small2) { // less than pager length
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());

  const char *expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  std::string out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA DDD \r\nBBB \x1b[7mEEE \x1b[0m\r\nCCC FFF \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(4, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "AAA DDD \r\nBBB EEE \r\n\x1b[7mCCC \x1b[0mFFF \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "AAA DDD \r\nBBB EEE \r\nCCC \x1b[7mFFF \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "\x1b[7mAAA \x1b[0mDDD \r\nBBB EEE \r\nCCC FFF \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, small3) { // less than pager length
  auto array = this->create("AAAAA", "BBBBB", "CCCCC", "DDDDD", "EEEEE");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 20});
  ASSERT_EQ(2, pager.getPanes());

  /**
   * AAAAA   DDDDD
   * BBBBB   EEEEE
   * CCCCC
   */
  const char *expect = "\x1b[7mAAAAA   \x1b[0mDDDDD   \r\nBBBBB   EEEEE   \r\nCCCCC   \r\n";
  std::string out = this->render(pager);
  ASSERT_EQ(expect, out);

  // prev
  expect = "AAAAA   DDDDD   \r\nBBBBB   \x1b[7mEEEEE   \x1b[0m\r\nCCCCC   \r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "AAAAA   DDDDD   \r\nBBBBB   EEEEE   \r\n\x1b[7mCCCCC   \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "\x1b[7mAAAAA   \x1b[0mDDDDD   \r\nBBBBB   EEEEE   \r\nCCCCC   \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAAAA   DDDDD   \r\nBBBBB   EEEEE   \r\n\x1b[7mCCCCC   \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, large1) { // larger than pager length
  auto array = this->create("AAA", "BBB", "CC\nC", "DDD", "EEE", "FFF", "GG\t", "HHH");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 5, .cols = 20});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_TRUE(pager.getRows() < array->size() / 2);
  ASSERT_EQ(0, pager.getCurRow());

  const char *expect = "\x1b[7mAAA     \x1b[0mEEE     \r\nBBB     FFF     \r\n"
                       "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // up
  expect = "CCC     GG      \r\nDDD     \x1b[7mHHH     \x1b[0m\r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // up
  expect = "CCC     \x1b[7mGG      \x1b[0m\r\nDDD     HHH     \r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // up
  expect = "BBB     \x1b[7mFFF     \x1b[0m\r\nCCC     GG      \r\n"
           "\x1b[7mrows 2-3/4\x1b[0m\r\n";
  pager.moveCursorToForward();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "BBB     FFF     \r\nCCC     \x1b[7mGG      \x1b[0m\r\n"
           "\x1b[7mrows 2-3/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "CCC     GG      \r\nDDD     \x1b[7mHHH     \x1b[0m\r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "\x1b[7mAAA     \x1b[0mEEE     \r\nBBB     FFF     \r\n"
           "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down+down+down
  expect = "CCC     GG      \r\n\x1b[7mDDD     \x1b[0mHHH     \r\n"
           "\x1b[7mrows 3-4/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // down
  expect = "AAA     \x1b[7mEEE     \x1b[0m\r\nBBB     FFF     \r\n"
           "\x1b[7mrows 1-2/4\x1b[0m\r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(0, pager.getCurRow());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, large2) { // larger than pager length
  /**
   * AAA EEE
   * BBB FFF
   * CCC GGG
   * DDD HHH
   */
  auto array = this->create("AAA", "BBB", "CC\nC", "DDD", "EEE", "FFF", "GGG", "HHH");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 5, .cols = 10});
  ASSERT_EQ(2, pager.getPanes());

  const char *expect = "\x1b[7mAAA \x1b[0mEEE \r\nBBB FFF \r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "CCC GGG \r\nDDD \x1b[7mHHH \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(7, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left+left+left
  expect = "\x1b[7mCCC \x1b[0mGGG \r\nDDD HHH \r\n";
  pager.moveCursorToLeft();
  pager.moveCursorToLeft();
  pager.moveCursorToLeft();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "BBB \x1b[7mFFF \x1b[0m\r\nCCC GGG \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(5, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "BBB FFF \r\n\x1b[7mCCC \x1b[0mGGG \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "BBB FFF \r\nCCC \x1b[7mGGG \x1b[0m\r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(6, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "CCC GGG \r\n\x1b[7mDDD \x1b[0mHHH \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right+right
  expect = "\x1b[7mAAA \x1b[0mEEE \r\nBBB FFF \r\n";
  pager.moveCursorToRight();
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, single) { // larger than pager length
  /**
   * AAA
   * BBB
   * CCC
   * DDD
   */
  auto array = this->create("AAA", "BBB", "CCC", "DDD");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 100, .cols = 5});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());

  const char *expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // next
  expect = "AAA \r\n\x1b[7mBBB \x1b[0m\r\nCCC \r\nDDD \r\n";
  pager.moveCursorToNext();
  ASSERT_EQ(1, pager.getCurRow());
  ASSERT_EQ(1, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right+right
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  pager.moveCursorToRight();
  pager.moveCursorToRight();
  ASSERT_EQ(3, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // right
  expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  pager.moveCursorToRight();
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(3, pager.getCurRow());
  ASSERT_EQ(3, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // left
  expect = "AAA \r\nBBB \r\n\x1b[7mCCC \x1b[0m\r\nDDD \r\n";
  pager.moveCursorToLeft();
  ASSERT_EQ(2, pager.getCurRow());
  ASSERT_EQ(2, pager.getIndex());
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, truncate) {
  this->ps.zwjSeqFallback = true;
  auto array = this->create("@@@", "ABCD123456", "ABCD987\r", "ABCDEああ", "123456\t\t",
                            "12345👩🏼‍🏭111");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 100, .cols = 10});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(8, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect = "@@@     \r\nABCD1234\r\nABCD987.\r\nABCDEあ.\r\n123456  \r\n12345...\r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, shrinkRow) {
  /**
   * AAA
   * BBB
   * CCC
   * DDD
   * EEE
   * FFF
   */
  auto array = this->create("AAA", "BBB", "CCC", "DDD", "EEE", "FFF");
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 5});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(0, pager.getCurRow());
  ASSERT_EQ(0, pager.getIndex());

  const char *expect = "\x1b[7mAAA \x1b[0m\r\nBBB \r\nCCC \r\nDDD \r\n";
  std::string out;
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  pager.moveCursorToNext();
  pager.moveCursorToNext();
  pager.moveCursorToNext();
  expect = "AAA \r\nBBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // shrink
  pager.updateWinSize({.rows = 9, .cols = 5});
  expect = "BBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // shrink (no change)
  pager.updateWinSize({.rows = 8, .cols = 5});
  expect = "BBB \r\nCCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // shrink
  pager.updateWinSize({.rows = 7, .cols = 5});
  expect = "CCC \r\n\x1b[7mDDD \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  pager.moveCursorToNext();
  pager.updateWinSize({.rows = 7, .cols = 5});
  expect = "DDD \r\n\x1b[7mEEE \x1b[0m\r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);

  // expand
  pager.updateWinSize({.rows = 9, .cols = 5});
  expect = "DDD \r\n\x1b[7mEEE \x1b[0m\r\nFFF \r\n";
  out = this->render(pager);
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, shrinkRatio) {
  const auto array = this->createWith({
      {"AAAAAA", "command"},
      {"BBBBBB", "command"},
      {"CCCCCC", "command"},
      {"DDDDDD", "command"},
      {"EEEEEE", "command"},
      {"FFFFFF", "command"},
  });
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30});
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(40, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\nDDDDDD     (command)    \r\n"
                         "\x1B[7mrows 1-4/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  // change ratio (shrink)
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 30);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(30, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "\x1B[7mrows 1-3/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 10);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(10, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\n"
                         "\x1B[7mrows 1-1/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 0);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(0, pager.getRowRatio());

    const char *expect = "";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }
}

TEST_F(PagerTest, expandRatio) {
  const auto array = this->createWith({
      {"AAAAAA", "command"},
      {"BBBBBB", "command"},
      {"CCCCCC", "command"},
      {"DDDDDD", "command"},
      {"EEEEEE", "command"},
      {"FFFFFF", "command"},
  });
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30});
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(40, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\nDDDDDD     (command)    \r\n"
                         "\x1B[7mrows 1-4/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  // change ratio (expand)
  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 50);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(50, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\n"
                         "BBBBBB     (command)    \r\n"
                         "CCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "\x1B[7mrows 1-5/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 10, .cols = 30}, 100);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(100, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "FFFFFF     (command)    \r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 9, .cols = 30}, 100);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(100, pager.getRowRatio());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "FFFFFF     (command)    \r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }

  {
    auto pager = ArrayPager::create(*array, this->ps, {.rows = 8, .cols = 30}, 100);
    ASSERT_EQ(1, pager.getPanes());
    ASSERT_EQ(0, pager.getCurRow());
    ASSERT_EQ(0, pager.getIndex());
    ASSERT_EQ(100, pager.getRowRatio());
    ASSERT_EQ(6, pager.getRenderedRows());

    const char *expect = "\x1b[7mAAAAAA     (command)    \x1b[0m\r\nBBBBBB     (command)    "
                         "\r\nCCCCCC     (command)    \r\n"
                         "DDDDDD     (command)    \r\n"
                         "EEEEEE     (command)    \r\n"
                         "\x1B[7mrows 1-5/6\x1B[0m\r\n";
    std::string out;
    out = this->render(pager);
    ASSERT_EQ(expect, out);
  }
}

TEST_F(PagerTest, desc1) {
  // single pane
  auto array = this->createWith({
      {"AAAAA", "regular file"},
      {"BBBBB", "executable"},
      {"CCCCC", "directory"},
      {"DDD", "named pipe"},
      {"EEEE", ""},
  });
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 30});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(28, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect = "AAAAA     (regular file)    \r\n"
                       "BBBBB       (executable)    \r\n"
                       "CCCCC        (directory)    \r\n"
                       "DDD         (named pipe)    \r\n"
                       "EEEE                        \r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, desc2) {
  // multi pane
  auto array = this->createWith({
      {"AAAAA", "regular file"},
      {"BBBBB", "executable"},
      {"CCCCC", "directory"},
      {"DDD", "named pipe"},
      {"EEEE", ""},
  });
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 60});
  ASSERT_EQ(2, pager.getPanes());
  ASSERT_EQ(28, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect = "AAAAA     (regular file)    DDD         (named pipe)    \r\n"
                       "BBBBB       (executable)    EEEE                        \r\n"
                       "CCCCC        (directory)    \r\n";
  ASSERT_EQ(expect, out);

  // shrink (hide description)
  pager.updateWinSize({.rows = 24, .cols = 9});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(8, pager.getPaneLen());
  out = this->render(pager);
  expect = "AAAAA   \r\n"
           "BBBBB   \r\n"
           "CCCCC   \r\n"
           "DDD     \r\n"
           "EEEE    \r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, sig) {
  auto array = this->createWith(
      {
          {"OSTYPE", ": String"},
          {"PID", ": Int"},
          {"COMP_HOOK", ": ((Module, [String], Int) -> Candidates?)?"},
      },
      {CandidateAttr::Kind::TYPE_SIGNATURE, false});
  auto pager = ArrayPager::create(*array, this->ps, {.rows = 24, .cols = 80});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(56, pager.getPaneLen());
  pager.setShowCursor(false);

  std::string out = this->render(pager);
  const char *expect =
      "OSTYPE\x1b[90m : String                                         \x1b[0m\r\n"
      "PID\x1b[90m : Int                                               \x1b[0m\r\n"
      "COMP_HOOK\x1b[90m : ((Module, [String], Int) -> Candidates?)?   \x1b[0m\r\n";
  ASSERT_EQ(expect, out);

  // shrink (hide signature)
  pager.updateWinSize({.rows = 24, .cols = 14});
  ASSERT_EQ(1, pager.getPanes());
  ASSERT_EQ(12, pager.getPaneLen());
  out = this->render(pager);
  expect = "OSTYPE      \r\n"
           "PID         \r\n"
           "COMP_HOOK   \r\n";
  ASSERT_EQ(expect, out);
}

TEST_F(PagerTest, candidate) {
  auto obj = this->createWith({});
  ASSERT_EQ(0, obj->size());
  obj->addNewCandidateWith(*this->state, "mkdir", "command",
                           {CandidateAttr::Kind::CMD_EXTERNAL, true});
  obj->addNewCandidateWith(*this->state, "mkdir", "dynamic", {CandidateAttr::Kind::CMD_DYNA, true});
  ASSERT_EQ(2, obj->size());
  obj->sortAndDedup(0);
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ(CandidateAttr::Kind::CMD_DYNA, obj->getAttrAt(0).kind);
}

TEST(HistRotatorTest, base) {
  auto value =
      createObject<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray), std::vector<Value>());
  auto &obj = *value;
  obj.append(Value::createStr("AAA"));
  obj.append(Value::createStr("BBB"));
  obj.append(Value::createStr("CCC"));
  obj.append(Value::createStr("DDD"));
  obj.append(Value::createStr("EEE"));

  HistRotator rotate(value);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  ASSERT_EQ(6, obj.size());
  ASSERT_EQ("AAA", obj[0].asStrRef().toString());
  ASSERT_EQ("BBB", obj[1].asStrRef().toString());
  ASSERT_EQ("CCC", obj[2].asStrRef().toString());
  ASSERT_EQ("DDD", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", obj[4].asStrRef().toString());
  ASSERT_EQ("", obj[5].asStrRef().toString()); // reserved for current editing buffer

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate next
  r = rotate.rotate(ref, HistRotator::Op::NEXT);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("@@@", ref.toString());

  // rotate prev+prev
  ref = "$$$$";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("DDD", ref.toString());

  // rotate prev
  ref = "&&&&";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // rotate prev
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // rotate prev
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("$$$$", obj[3].asStrRef().toString());
  ASSERT_EQ("CCC", ref.toString());

  // revert
  rotate.revertAll();
  ASSERT_EQ(3, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
}

TEST(HistRotatorTest, broken1) {
  auto value =
      createObject<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray), std::vector<Value>());
  auto &obj = *value;
  obj.append(Value::createStr("AAA"));
  obj.append(Value::createStr("BBB"));
  obj.append(Value::createStr("CCC"));
  obj.append(Value::createStr("DDD"));
  obj.append(Value::createStr("EEE"));

  HistRotator rotate(value);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref);

  obj.erase(obj.begin() + 1);
  ref = "&&&";
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(3, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("&&&", obj[1].asStrRef().toString());
  ASSERT_EQ("@@@", obj[2].asStrRef().toString());
  ASSERT_EQ("CCC", ref);

  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);

  obj.clear();
  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);

  // revert
  rotate.revertAll();
  ASSERT_EQ(0, obj.size());
}

TEST(HistRotator, broken2) {
  auto value =
      createObject<ArrayObject>(static_cast<unsigned int>(TYPE::StringArray), std::vector<Value>());
  auto &obj = *value;
  obj.append(Value::createStr("AAA"));
  obj.append(Value::createStr("BBB"));
  obj.append(Value::createStr("CCC"));
  obj.append(Value::createStr("DDD"));
  obj.append(Value::createStr("EEE"));

  HistRotator rotate(value);
  rotate.setMaxSize(4);
  ASSERT_EQ(4, rotate.getMaxSize());

  // rotate prev
  StringRef ref = "@@@"; // current editing content
  bool r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_TRUE(r);
  ASSERT_EQ(4, obj.size());
  ASSERT_EQ("CCC", obj[0].asStrRef().toString());
  ASSERT_EQ("DDD", obj[1].asStrRef().toString());
  ASSERT_EQ("EEE", obj[2].asStrRef().toString());
  ASSERT_EQ("@@@", obj[3].asStrRef().toString());
  ASSERT_EQ("EEE", ref);

  // remove history and rotate prev
  ref = "%%%";
  obj.erase(obj.begin(), obj.begin() + 2);
  ASSERT_EQ(2, obj.size());
  ASSERT_EQ("EEE", obj[0].asStrRef().toString());
  ASSERT_EQ("@@@", obj[1].asStrRef().toString());

  r = rotate.rotate(ref, HistRotator::Op::PREV);
  ASSERT_FALSE(r);
  ASSERT_EQ(2, obj.size());
  ASSERT_EQ("EEE", obj[0].asStrRef().toString());
  ASSERT_EQ("@@@", obj[1].asStrRef().toString());
  ASSERT_EQ("%%%", ref);

  rotate.revertAll();
  ASSERT_EQ(1, obj.size());
  ASSERT_EQ("EEE", obj[0].asStrRef().toString());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}