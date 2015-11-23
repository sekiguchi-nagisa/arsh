#include <gtest/gtest.h>

#include <parser/Lexer.h>

using namespace ydsh::parser;

TEST(LineNumTest, case1) {
    SourceInfo info("dummy");
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        ASSERT_EQ(std::string("dummy"), info.getSourceName());
    });
}

TEST(LineNumTest, case2) {
    SourceInfo info("dummy");
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        ASSERT_EQ(1u, info.getLineNum(12));  // empty
    });
}

TEST(LineNumTest, case3) {
    SourceInfo info("dummy");
    info.addNewlinePos(5);
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        ASSERT_EQ(1u, info.getLineNum(3));
        ASSERT_EQ(1u, info.getLineNum(4));
        ASSERT_EQ(1u, info.getLineNum(5));
        ASSERT_EQ(2u, info.getLineNum(6));
    });
}

TEST(LineNumTest, case4) {
    SourceInfo info("dummy");
    info.addNewlinePos(5);
    info.addNewlinePos(4);  // overwrite
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        ASSERT_EQ(1u, info.getLineNum(3));
        ASSERT_EQ(1u, info.getLineNum(4));
        ASSERT_EQ(1u, info.getLineNum(5));
        ASSERT_EQ(2u, info.getLineNum(6));
    });
}

TEST(LineNumTest, case5) {
    SourceInfo info("dummy");
    info.setLineNumOffset(4);
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        ASSERT_EQ(4u, info.getLineNum(5));
        info.addNewlinePos(10);
        ASSERT_EQ(4u, info.getLineNum(5));
        ASSERT_EQ(5u, info.getLineNum(13));
    });
}