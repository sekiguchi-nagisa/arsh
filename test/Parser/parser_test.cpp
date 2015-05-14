#include <gtest/gtest.h>

#include <parser/Lexer.h>
#include <parser/Parser.h>
#include <parser/ParseError.h>
#include <ast/Node.h>

#include <stdio.h>
#include <dirent.h>

#ifndef PARSER_TEST_DIR
#define PARSER_TEST_DIR "."
#endif

using namespace ydsh::parser;

static std::vector<const char *> getFileList(const char *path) {
    std::vector<const char *> fileList;

    DIR *dir = opendir(path);
    if(dir == nullptr) {
        exit(1);
    }

    dirent *entry;

    do {
        entry = readdir(dir);
        if(entry == nullptr) {
            break;
        }
        if(entry->d_type == DT_REG) {
            fileList.push_back(entry->d_name);
        }
    } while(true);

    return fileList;
}


class ParserTest : public ::testing::TestWithParam<const char *> {
private:
    std::string targetName;
    Parser parser;

public:
    ParserTest() : targetName(), parser() {
    }

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

        Lexer<LexerDef, TokenKind> lexer(fp);

        bool status = false;
        try {
            this->parser.parse(lexer, rootNode);
            status = true;
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