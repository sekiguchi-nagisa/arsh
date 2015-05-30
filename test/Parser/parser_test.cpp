#include <gtest/gtest.h>

#include <parser/Lexer.h>
#include <parser/Parser.h>
#include <parser/ParseError.h>
#include <misc/files.h>

#ifndef PARSER_TEST_DIR
#define PARSER_TEST_DIR "."
#endif

using namespace ydsh::parser;

class ParserTest : public ::testing::TestWithParam<const char *> {
private:
    std::string targetName;
    Parser parser;

public:
    ParserTest() = default;
    virtual ~ParserTest() = default;

    virtual void SetUp() {
        this->targetName += PARSER_TEST_DIR;
        this->targetName += "/";
        this->targetName += this->GetParam();
    }

    virtual void TearDown() {
        this->targetName.clear();
    }

    virtual const char *getSourceName() {
        return this->targetName.c_str();
    }

    virtual void doTest(RootNode &rootNode) {
        SCOPED_TRACE("");

        FILE *fp = fopen(this->getSourceName(), "r");
        ASSERT_NE(nullptr, fp);

        Lexer lexer(fp);

        bool status = true;
        try {
            this->parser.parse(lexer, rootNode);
        } catch(const ParseError &e) {
            status = false;
        }

        ASSERT_TRUE(status);
    }
};

TEST_P(ParserTest, baseTest) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        RootNode rootNode(this->getSourceName());
        this->doTest(rootNode);
    });
}

INSTANTIATE_TEST_CASE_P(ParserTest, ParserTest, ::testing::ValuesIn(getFileList(PARSER_TEST_DIR)));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}