#include "gtest/gtest.h"

#include <cstring>

#include <misc/unicode.hpp>

using namespace ydsh;

class UnicodeTest : public ::testing::Test {
public:
    UnicodeTest() = default;

    void toCodePoint(const char *str, int &codePoint) {
        ASSERT_TRUE(str != nullptr);

        int code = 0;
        unsigned int len = strlen(str);

        unsigned int s = UnicodeUtil::utf8ToCodePoint(str, len == 0 ? 1 : len, code);
        ASSERT_NE(0u, s);
        ASSERT_NE(-1, code);

        codePoint = code;
    }

    template <unsigned int N>
    void assertByteSize(const unsigned int size, const char (&str)[N]) {
        ASSERT_EQ(size, UnicodeUtil::utf8ValidateChar(str, str + N));
    }

    void assertCodePoint(const int expect, const char *str) {
        int code = 0;
        this->toCodePoint(str, code);
        ASSERT_EQ(expect, code);
    }

    void assertWidth(const int width, const char *str, bool ambiguousWidth2 = false) {
        int codePoint = 0;
        this->toCodePoint(str, codePoint);

        auto e = ambiguousWidth2 ? UnicodeUtil::TWO_WIDTH : UnicodeUtil::ONE_WIDTH;
        ASSERT_EQ(width, UnicodeUtil::width(codePoint, e));
    }

    void assertIllegal(const char *const buf, unsigned int bufSize) {
        int codePoint = 0;
        ASSERT_EQ(0u, UnicodeUtil::utf8ToCodePoint(buf, bufSize, codePoint));
        ASSERT_EQ(-1, codePoint);

        ASSERT_EQ(-1, UnicodeUtil::utf8ToCodePoint(buf, bufSize));
    }

    template <unsigned int N>
    void assertCodePoint2Utf8(const char (&ch)[N]) {
        this->assertCodePoint2Utf8(ch, N - 1);
    }

    void assertCodePoint2Utf8(const char *ch, unsigned int byteSize) {
        char buf[4];
        int codePoint = UnicodeUtil::utf8ToCodePoint(ch, byteSize);
        unsigned int size = UnicodeUtil::codePointToUtf8(codePoint, buf);
        ASSERT_EQ(byteSize, size);
        std::string before(ch, byteSize);
        std::string after(buf, size);
        ASSERT_EQ(before, after);
    }
};

TEST_F(UnicodeTest, size) {
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(1, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(1, "1"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(1, "a"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(1, "\n"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(1, "\t"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(2, "å"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(2, "¶"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(2, "Ω"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(3, "あ"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(3, "解"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(3, "墨"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(3, "ｱ"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(4, "𪗱"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(4, "𣏤"));
    ASSERT_NO_FATAL_FAILURE(this->assertByteSize(4, "𣴀"));
}

TEST_F(UnicodeTest, codepoint2utf8) {
    char buf[4];
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, UnicodeUtil::codePointToUtf8(-3, buf)));

    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("\0"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("a"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("å"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("¶"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("あ"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("ｱ"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("𣏤"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint2Utf8("𣴀"));
}

TEST_F(UnicodeTest, base) {
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, UnicodeUtil::width(0)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, UnicodeUtil::width('\n')));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, UnicodeUtil::width('\t')));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, UnicodeUtil::width('1')));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, UnicodeUtil::width(' ')));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, UnicodeUtil::width('\\')));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, UnicodeUtil::width('~')));
}

TEST_F(UnicodeTest, codePoint) {
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x40, "@"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x7E, "~"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x0150, "Ő"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x305F, "た"));
    ASSERT_NO_FATAL_FAILURE(this->assertCodePoint(0x25E56, "𥹖"));
}

TEST_F(UnicodeTest, multi) {
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\n"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\n", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\r"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(-1, "\r", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(0, ""));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(0, "", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "a"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "a", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "å"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "å", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "◯"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "◯", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "■"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "■", true));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "ま"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(2, "ま", true));

    unsigned char b[] = {0xCC, 0x88, 0};   // combining character
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(0, (char *)b));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(0, (char *)b, true));

    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "ｱ"));
    ASSERT_NO_FATAL_FAILURE(this->assertWidth(1, "ｱ", true));
}

TEST_F(UnicodeTest, multi2) {
    int code = 0;
    this->toCodePoint("◯", code);

    // C
    const char *r = nullptr;
    r = setlocale(LC_CTYPE, "C");
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(r != nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1, UnicodeUtil::localeAwareWidth(code)));

    // ja
    r = setlocale(LC_CTYPE, "ja_JP.UTF-8");
    if(r != nullptr) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, UnicodeUtil::localeAwareWidth(code)));
    }

    // zh
    r = setlocale(LC_CTYPE, "zh_CN.UTF-8");
    if(r != nullptr) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, UnicodeUtil::localeAwareWidth(code)));
    }

    // ko
    r = setlocale(LC_CTYPE, "ko_KR.UTF-8");
    if(r != nullptr) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, UnicodeUtil::localeAwareWidth(code)));
    }


    // reset locale
    setlocale(LC_ALL, "");
}

TEST_F(UnicodeTest, illegal) {
    // 2byte
    {
        char b[] = {static_cast<char>(200)};
        ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
    }

    // 3byte
    {
        char b[] = {static_cast<char>(238), 32};
        ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
    }

    // 4byte
    {
        char b[] = {static_cast<char>(243), 32};
        ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
    }

    // illegal byte
    {
        char b[] = {static_cast<char>(144)};
        ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
    }

    {
        char b[] = {static_cast<char>(253)};
        ASSERT_NO_FATAL_FAILURE(this->assertIllegal(b, 1));
    }

    // illegal code point
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-2, UnicodeUtil::width(-1, UnicodeUtil::ONE_WIDTH)));
}

TEST_F(UnicodeTest, utf16) {
    unsigned short high = 0xD867;
    unsigned short low = 0xDE3D;

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isHighSurrogate(high)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(UnicodeUtil::isHighSurrogate(low)));

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isLowSurrogate(low)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(UnicodeUtil::isLowSurrogate(high)));

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isSurrogate(high)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isSurrogate(low)));

    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isBmpCodePoint(high)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isBmpCodePoint(low)));

    int code = UnicodeUtil::utf16ToCodePoint(high, low);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(UnicodeUtil::isSupplementaryCodePoint(code)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(UnicodeUtil::isBmpCodePoint(code)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0x29E3D, code));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}