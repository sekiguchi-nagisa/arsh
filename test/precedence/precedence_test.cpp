#include <gtest/gtest.h>

#include <ast/Node.h>
#include <parser/Parser.h>
#include <misc/fatal.h>

using namespace ydsh::parser;


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

class PrettyPrinter : public NodeVisitor {
private:
    std::vector<std::string> out;

public:
    PrettyPrinter() = default;
    ~PrettyPrinter() = default;

    std::vector<std::string> operator()(RootNode &rootNode) {
        this->visitRootNode(&rootNode);
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

    void visitDefault(Node *node) {
        fatal("unsupported\n");
    }

    void visitIntValueNode(IntValueNode *node) {
        this->append(std::to_string(node->getTempValue()));
    }

    void visitLongValueNode(LongValueNode *node) { this->visitDefault(node); }
    void visitFloatValueNode(FloatValueNode *node) { this->visitDefault(node); }
    void visitStringValueNode(StringValueNode *node) { this->visitDefault(node); }
    void visitObjectPathNode(ObjectPathNode *node) { this->visitDefault(node); }
    void visitStringExprNode(StringExprNode *node) { this->visitDefault(node); }
    void visitArrayNode(ArrayNode *node) { this->visitDefault(node); }
    void visitMapNode(MapNode *node) { this->visitDefault(node); }
    void visitTupleNode(TupleNode *node) { this->visitDefault(node); }
    void visitVarNode(VarNode *node) { this->visitDefault(node); }
    void visitAccessNode(AccessNode *node) { this->visitDefault(node); }

    void visitCastNode(CastNode *node) {
        this->open();
        this->visit(node->getExprNode());
        this->append("as");
        this->append(node->getTargetTypeToken()->toTokenText());
        this->close();
    }

    void visitInstanceOfNode(InstanceOfNode *node) {
        this->open();
        this->visit(node->getTargetNode());
        this->append("is");
        this->append(node->getTargetTypeToken()->toTokenText());
        this->close();
    }

    void visitUnaryOpNode(UnaryOpNode *node) { this->visitDefault(node); }

    void visitBinaryOpNode(BinaryOpNode *node) {
        this->open();
        this->visit(node->getLeftNode());
        this->append(TO_NAME(node->getOp()));
        this->visit(node->getRightNode());
        this->close();
    }

    void visitArgsNode(ArgsNode *node) { this->visitDefault(node); }
    void visitApplyNode(ApplyNode *node) { this->visitDefault(node); }
    void visitMethodCallNode(MethodCallNode *node) { this->visitDefault(node); }
    void visitNewNode(NewNode *node) { this->visitDefault(node); }

    void visitGroupNode(GroupNode *node) {
        this->open();
        this->visit(node->getExprNode());
        this->close();
    }

    void visitCondOpNode(CondOpNode *node) {
        this->open();
        this->visit(node->getLeftNode());
        this->append(node->isAndOp() ? "&&" : "||");
        this->visit(node->getRightNode());
        this->close();
    }

    void visitCmdNode(CmdNode *node) { this->visitDefault(node); }
    void visitCmdArgNode(CmdArgNode *node) { this->visitDefault(node); }
    void visitRedirNode(RedirNode *node) { this->visitDefault(node); }
    void visitTildeNode(TildeNode *node) { this->visitDefault(node); }
    void visitPipedCmdNode(PipedCmdNode *node) { this->visitDefault(node); }
    void visitCmdContextNode(CmdContextNode *node) { this->visitDefault(node); }
    void visitAssertNode(AssertNode *node) { this->visitDefault(node); }
    void visitBlockNode(BlockNode *node) { this->visitDefault(node); }
    void visitBreakNode(BreakNode *node) { this->visitDefault(node); }
    void visitContinueNode(ContinueNode *node) { this->visitDefault(node); }
    void visitExportEnvNode(ExportEnvNode *node) { this->visitDefault(node); }
    void visitImportEnvNode(ImportEnvNode *node) { this->visitDefault(node); }
    void visitTypeAliasNode(TypeAliasNode *node) { this->visitDefault(node); }
    void visitForNode(ForNode *node) { this->visitDefault(node); }
    void visitWhileNode(WhileNode *node) { this->visitDefault(node); }
    void visitDoWhileNode(DoWhileNode *node) { this->visitDefault(node); }
    void visitIfNode(IfNode *node) { this->visitDefault(node); }
    void visitReturnNode(ReturnNode *node) { this->visitDefault(node); }
    void visitThrowNode(ThrowNode *node) { this->visitDefault(node); }
    void visitCatchNode(CatchNode *node) { this->visitDefault(node); }
    void visitTryNode(TryNode *node) { this->visitDefault(node); }
    void visitVarDeclNode(VarDeclNode *node) { this->visitDefault(node); }

    void visitAssignNode(AssignNode *node) {
        this->open();
        this->visit(node->getLeftNode());
        this->append("=");
        this->visit(node->getRightNode());
        this->close();
    }

    void visitElementSelfAssignNode(ElementSelfAssignNode *node) { this->visitDefault(node); }
    void visitFunctionNode(FunctionNode *node) { this->visitDefault(node); }
    void visitInterfaceNode(InterfaceNode *node) { this->visitDefault(node); }
    void visitBindVarNode(BindVarNode *node) { this->visitDefault(node); }
    void visitEmptyNode(EmptyNode *node) { this->visitDefault(node); }
    void visitDummyNode(DummyNode *node) { this->visitDefault(node); }

    void visitRootNode(RootNode *node) {
        if(node->getNodeList().size() != 1) {
            fatal("must be 1\n");
        }

        this->visit(node->getNodeList().front());
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
        Lexer lexer(input);
        RootNode rootNode;
        Parser parser;
        try {
            parser.parse(lexer, rootNode);
        } catch(const ParseError &e) {
            ASSERT_TRUE(false);
        }

        std::vector<std::string> actualTokens = PrettyPrinter()(rootNode);
        std::vector<std::string> expectedTokens = tokenize(expected);

        this->equalsTokens(expectedTokens, actualTokens);
    }
};

TEST_F(PrecedenceTest, base1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("1", "1");
    });
}

TEST_F(PrecedenceTest, base2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("(  1  )", "(1)");
    });
}

TEST_F(PrecedenceTest, base3) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("(1 + 2)", "1+2");
    });
}

TEST_F(PrecedenceTest, case1) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("((1 as Int) is Int)", "1 as Int is Int");
    });
}

TEST_F(PrecedenceTest, case2) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("((1 is Int) as Int)", "1 is Int as Int");
    });
}

TEST_F(PrecedenceTest, case3) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("(12 / (23 as Int))", "12 / 23 as Int");
    });
}

TEST_F(PrecedenceTest, case4) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("(((1 / 2) * 3) % 4)", "1 / 2 * 3 % 4");
   });
}

TEST_F(PrecedenceTest, case5) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("((1 + (2 * 3)) - 4)", "1 + 2 * 3 - 4");
    });
}

TEST_F(PrecedenceTest, case6) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("((1 & 2) | (3 ^ (4 + 3)))", "1 & 2 | 3 ^ 4 + 3");
    });
}

TEST_F(PrecedenceTest, case7) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("((((((((1 < 2) > 3) == 4) >= 5) !~ 6) <= 7) != 8) =~ 9)",
                     "1 < 2 > 3 == 4 >= 5 !~ 6 <= 7 != 8 =~ 9");
    });
}

TEST_F(PrecedenceTest, case8) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->equals("(1 = (((2 == 3) && (4 + 5)) || 6))", "1 = 2 == 3 && 4 + 5 || 6");
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
