#include <gtest/gtest.h>

#include <node.h>
#include <parser.h>
#include <misc/fatal.h>

using namespace ydsh;

static std::vector<std::string> tokenize(const char *str) {
    std::vector<std::string> tokens;
    std::string tokenBuf;

    for(unsigned int i = 0; str[i] != '\0'; i++) {
        char ch = str[i];
        switch(ch) {
        case '(':
        case ')': {
            if(!tokenBuf.empty()) {
                tokens.push_back(std::move(tokenBuf));
            }

            tokenBuf += ch;
            tokens.push_back(std::move(tokenBuf));
            break;
        }
        case ' ':
        case '\t': {
            if(!tokenBuf.empty()) {
                tokens.push_back(std::move(tokenBuf));
            }
            break;
        }
        default:
            tokenBuf += ch;
            break;
        }
    }
    if(!tokenBuf.empty()) {
        tokens.push_back(std::move(tokenBuf));
    }

    return tokens;
}

class PrettyPrinter : public BaseVisitor {
private:
    std::vector<std::string> out;

public:
    PrettyPrinter() = default;
    ~PrettyPrinter() = default;

    std::vector<std::string> operator()(RootNode &rootNode) {
        this->visitRootNode(rootNode);
        return std::move(this->out);
    }

    void append(const char *str) {
        this->out.push_back(str);
    }

    void append(const std::string &str) {
        this->out.push_back(str);
    }

    void open() {
        this->append("(");
    }

    void close() {
        this->append(")");
    }

    void visitDefault(Node &) override {
        fatal("unsupported\n");
    }

    void visitNumberNode(NumberNode &node) override {
        this->append(std::to_string(node.getIntValue()));
    }

    void visitTypeOpNode(TypeOpNode &node) override {
        this->open();
        this->visit(*node.getExprNode());
        this->append(node.isCastOp() ? "as" : "is");
        this->append(dynamic_cast<BaseTypeNode *>(node.getTargetTypeNode())->getTokenText()); //FIXME:
        this->close();
    }

    void visitBinaryOpNode(BinaryOpNode &node) override {
        this->open();
        this->visit(*node.getLeftNode());
        this->append(TO_NAME(node.getOp()));
        this->visit(*node.getRightNode());
        this->close();
    }

    void visitIfNode(IfNode &node) override {
        this->open();
        this->visit(*node.getCondNode());
        this->append("?");
        this->visit(*node.getThenNode());
        this->append(":");
        this->visit(*node.getElseNode());
        this->close();
    }

    void visitAssignNode(AssignNode &node) override {
        this->open();
        this->visit(*node.getLeftNode());
        this->append("=");
        this->visit(*node.getRightNode());
        this->close();
    }

    void visitRootNode(RootNode &node) override {
        if(node.getNodes().size() != 1) {
            fatal("must be 1\n");
        }

        this->visit(*node.getNodes().front());
    }
};


class PrecedenceTest : public ::testing::Test {
public:
    PrecedenceTest() = default;
    virtual ~PrecedenceTest() = default;

    virtual void SetUp() { }

    virtual void TearDown() { }

    virtual void equalsTokens(const std::vector<std::string> &expected, const std::vector<std::string> &actual) {
        SCOPED_TRACE("");

        // check size
        unsigned int size = expected.size();
        ASSERT_EQ(size, actual.size());

        // check each
        for(unsigned int i = 0; i < size; i++) {
            ASSERT_EQ(expected[i], actual[i]);
        }
    }

    virtual void equals(const char *expected, const char *input) {
        SCOPED_TRACE("");

        ASSERT_TRUE(expected != nullptr);
        ASSERT_TRUE(input != nullptr);

        // parse
        Lexer lexer("(string)", input);
        Parser parser(lexer);
        auto rootNode = parser();
        ASSERT_FALSE(parser.hasError());

        std::vector<std::string> actualTokens = PrettyPrinter()(*rootNode);
        std::vector<std::string> expectedTokens = tokenize(expected);

        this->equalsTokens(expectedTokens, actualTokens);
    }
};

TEST_F(PrecedenceTest, base1) {
    ASSERT_NO_FATAL_FAILURE(this->equals("1", "1"));
}

TEST_F(PrecedenceTest, base2) {
    ASSERT_NO_FATAL_FAILURE(this->equals("  1  ", "(1)"));
}

TEST_F(PrecedenceTest, base3) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(1 + 2)", "1+2"));
}

TEST_F(PrecedenceTest, case1) {
    ASSERT_NO_FATAL_FAILURE(this->equals("((1 as Int) is Int)", "1 as Int is Int"));
}

TEST_F(PrecedenceTest, case2) {
    ASSERT_NO_FATAL_FAILURE(this->equals("((1 is Int) as Int)", "1 is Int as Int"));
}

TEST_F(PrecedenceTest, case3) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(12 / (23 as Int))", "12 / 23 as Int"));
}

TEST_F(PrecedenceTest, case4) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(((1 / 2) * 3) % 4)", "1 / 2 * 3 % 4"));
}

TEST_F(PrecedenceTest, case5) {
    ASSERT_NO_FATAL_FAILURE(this->equals("((1 + (2 * 3)) - 4)", "1 + 2 * 3 - 4"));
}

TEST_F(PrecedenceTest, case6) {
    ASSERT_NO_FATAL_FAILURE(this->equals("((1 and 2) or (3 xor (4 + 3)))", "1 and 2 or 3 xor 4 + 3"));
}

TEST_F(PrecedenceTest, case7) {
    ASSERT_NO_FATAL_FAILURE(
        this->equals("((((((((1 < 2) > 3) == 4) >= 5) !~ 6) <= 7) != 8) =~ 9)",
                     "1 < 2 > 3 == 4 >= 5 !~ 6 <= 7 != 8 =~ 9"));
}

TEST_F(PrecedenceTest, case8) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(1 = (((2 == 3) && (4 + 5)) || 6))", "1 = 2 == 3 && 4 + 5 || 6"));
}

TEST_F(PrecedenceTest, case9) {
    ASSERT_NO_FATAL_FAILURE(
            this->equals("((1 == 2) ? ((3 > 4) ? (5 + 6) : (7 xor 8)) : (9 && 10))",
                         "1 == 2 ? 3 > 4 ? 5 + 6 : 7 xor 8 : 9 && 10"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
