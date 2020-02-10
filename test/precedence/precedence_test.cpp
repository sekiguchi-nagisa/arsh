#include "gtest/gtest.h"

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
    ~PrettyPrinter() override = default;

    std::vector<std::string> operator()(Node &node) {
        node.accept(*this);
        return std::move(this->out);
    }

    void append(const char *str) {
        this->out.emplace_back(str);
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
        auto *typeNode = node.getTargetTypeNode();
        assert(typeNode->typeKind == TypeNode::Base);
        this->append(static_cast<BaseTypeNode *>(typeNode)->getTokenText());
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

    void visitJumpNode(JumpNode &node) override {
        this->open();
        assert(node.getOpKind() == JumpNode::THROW_);
        this->append("throw");
        this->visit(node.getExprNode());
        this->close();
    }

    void visitWithNode(WithNode &node) override {
        this->open();
        this->visit(*node.getExprNode());
        this->append("with");
        this->visit(*node.getRedirNodes().back());
        this->close();
    }

    void visitRedirNode(RedirNode &node) override {
        this->append(TO_NAME(node.getRedirectOP()));
        this->visit(*node.getTargetNode());
    }

    void visitCmdArgNode(CmdArgNode &node) override {
        this->visit(*node.getSegmentNodes().back());
    }

    void visitStringNode(StringNode &node) override {
        this->append(node.getValue());
    }

    void visitPipelineNode(PipelineNode &node) override {
        this->open();
        for(unsigned int i = 0; i < node.getNodes().size(); i++) {
            if(i > 0) {
                this->append("|");
            }
            this->visit(*node.getNodes()[i]);
        }
        this->close();
    }

    void visitForkNode(ForkNode &node) override {
        this->open();
        if(node.getOpKind() == ForkKind::COPROC) {
            this->append("coproc");
        }
        this->visit(*node.getExprNode());
        if(node.getOpKind() == ForkKind::JOB) {
            this->append("&");
        }
        this->close();
    }
};


class PrecedenceTest : public ::testing::Test {
public:
    PrecedenceTest() = default;

    virtual void equalsTokens(const std::vector<std::string> &expected, const std::vector<std::string> &actual) {
        std::string eStr;
        for(auto &e : expected) {
            eStr += e;
        }

        std::string aStr;
        for(auto &e : actual) {
            aStr += e;
        }
        ASSERT_EQ(eStr, aStr);
    }

    virtual void equals(const char *expected, const char *input) {
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

TEST_F(PrecedenceTest, case10) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(55 < ((65 or 75) ?? 78))", "55 < 65 or 75 ?? 78"));
}

TEST_F(PrecedenceTest, case11) {
    ASSERT_NO_FATAL_FAILURE(this->equals("((throw (45 + 45)) && 54)", "throw 45 + 45 && 54"));
}

TEST_F(PrecedenceTest, case12) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(34 || ((45 | (56 and 67)) && 78))", "34 || 45 | 56 and 67 && 78"));
}

TEST_F(PrecedenceTest, case13) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(45 | ((56 as Int) with 2> 67))", "45 | 56 as Int with 2> 67"));
}

TEST_F(PrecedenceTest, case14) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(23 = (((45 | 56) && 78) &))", "23 = 45 | 56 && 78 &"));
}

TEST_F(PrecedenceTest, case15) {
    ASSERT_NO_FATAL_FAILURE(this->equals("((12 ? 23 : 34) &)", "12 ? 23 : 34 &"));
}

TEST_F(PrecedenceTest, case16) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(12 ?? (23 ?? 34))", "12 ?? 23 ?? 34"));
}

TEST_F(PrecedenceTest, case17) {
    ASSERT_NO_FATAL_FAILURE(this->equals("(12 = ((coproc ((34 | 45) && 56))&))", "12 = coproc 34 | 45 && 56 &"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
