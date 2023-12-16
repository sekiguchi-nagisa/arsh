#include "gtest/gtest.h"

#include <misc/fatal.h>
#include <node.h>
#include <parser.h>

using namespace arsh;

static std::vector<std::string> tokenize(const char *str) {
  std::vector<std::string> tokens;
  std::string tokenBuf;

  for (unsigned int i = 0; str[i] != '\0'; i++) {
    char ch = str[i];
    switch (ch) {
    case '(':
    case ')': {
      if (!tokenBuf.empty()) {
        tokens.push_back(std::move(tokenBuf));
        tokenBuf = "";
      }

      tokenBuf += ch;
      tokens.push_back(std::move(tokenBuf));
      break;
    }
    case ' ':
    case '\t': {
      if (!tokenBuf.empty()) {
        tokens.push_back(std::move(tokenBuf));
        tokenBuf = "";
      }
      break;
    }
    default:
      tokenBuf += ch;
      break;
    }
  }
  if (!tokenBuf.empty()) {
    tokens.push_back(std::move(tokenBuf));
  }

  return tokens;
}

class PrettyPrinter : public BaseVisitor {
private:
  const Lexer &lexer;
  std::vector<std::string> out;

public:
  explicit PrettyPrinter(const Lexer &lexer) : lexer(lexer) {}
  ~PrettyPrinter() override = default;

  std::vector<std::string> operator()(Node &node) {
    node.accept(*this);
    return std::move(this->out);
  }

  void append(const char *str) { this->out.emplace_back(str); }

  void append(const std::string &str) { this->out.push_back(str); }

  void open() { this->append("("); }

  void close() { this->append(")"); }

  void visitDefault(Node &) override { fatal("unsupported\n"); }

  void visitNumberNode(NumberNode &node) override {
    this->append(this->lexer.toTokenText(node.getActualToken()));
  }

  void visitTypeOpNode(TypeOpNode &node) override {
    this->open();
    this->visit(node.getExprNode());
    this->append(node.isCastOp() ? "as" : "is");
    auto &typeNode = node.getTargetTypeNode();
    assert(typeNode->typeKind == TypeNode::Base);
    this->append(cast<BaseTypeNode>(*typeNode).getTokenText());
    this->close();
  }

  void visitBinaryOpNode(BinaryOpNode &node) override {
    this->open();
    this->visit(*node.getLeftNode());
    this->append(toString(node.getOp()));
    this->visit(*node.getRightNode());
    this->close();
  }

  void visitIfNode(IfNode &node) override {
    this->open();
    this->visit(node.getCondNode());
    this->append("?");
    this->visit(node.getThenNode());
    this->append(":");
    this->visit(node.getElseNode());
    this->close();
  }

  void visitAssignNode(AssignNode &node) override {
    this->open();
    this->visit(node.getLeftNode());
    this->append("=");
    this->visit(node.getRightNode());
    this->close();
  }

  void visitJumpNode(JumpNode &node) override {
    this->open();
    assert(node.getOpKind() == JumpNode::THROW);
    this->append("throw");
    this->visit(node.getExprNode());
    this->close();
  }

  void visitWithNode(WithNode &node) override {
    this->open();
    this->visit(node.getExprNode());
    this->append("with");
    this->visit(*node.getRedirNodes().back());
    this->close();
  }

  void visitTimeNode(TimeNode &node) override {
    this->open();
    this->append("time");
    this->visit(node.getExprNode());
    this->close();
  }

  void visitRedirNode(RedirNode &node) override {
    this->append(">"); // FIXME:
    this->visit(node.getTargetNode());
  }

  void visitCmdArgNode(CmdArgNode &node) override { this->visit(*node.getSegmentNodes().back()); }

  void visitStringNode(StringNode &node) override { this->append(node.getValue()); }

  void visitPipelineNode(PipelineNode &node) override {
    this->open();
    for (unsigned int i = 0; i < node.getNodes().size(); i++) {
      if (i > 0) {
        this->append("|");
      }
      this->visit(*node.getNodes()[i]);
    }
    this->close();
  }

  void visitForkNode(ForkNode &node) override {
    this->open();
    if (node.getOpKind() == ForkKind::COPROC) {
      this->append("coproc");
    }
    this->visit(node.getExprNode());
    if (node.getOpKind() == ForkKind::JOB) {
      this->append("&");
    }
    this->close();
  }

  void visitPrefixAssignNode(PrefixAssignNode &node) override {
    this->open();
    for (auto &e : node.getAssignNodes()) {
      std::string name = cast<VarNode>(e->getLeftNode()).getVarName();
      name += "=";
      this->append(name);
      this->visit(e->getRightNode());
    }
    this->visit(*node.getExprNode());
    this->close();
  }
};

class PrecedenceTest : public ::testing::Test {
public:
  PrecedenceTest() = default;

  virtual void equalsTokens(const std::vector<std::string> &expected,
                            const std::vector<std::string> &actual) const {
    std::string eStr;
    for (auto &e : expected) {
      eStr += e;
    }

    std::string aStr;
    for (auto &e : actual) {
      aStr += e;
    }
    ASSERT_EQ(eStr, aStr);
  }

  virtual void equals(const char *expected, const char *input) const {
    ASSERT_TRUE(expected != nullptr);
    ASSERT_TRUE(input != nullptr);

    // parse
    Lexer lexer("(string)", ByteBuffer(input, input + strlen(input)), nullptr);
    Parser parser(lexer);
    auto rootNode = parser();
    ASSERT_FALSE(parser.hasError());
    ASSERT_EQ(1, rootNode.size());

    std::vector<std::string> actualTokens = PrettyPrinter(lexer)(*rootNode.back());
    std::vector<std::string> expectedTokens = tokenize(expected);

    this->equalsTokens(expectedTokens, actualTokens);
  }
};

TEST_F(PrecedenceTest, base) {
  ASSERT_NO_FATAL_FAILURE(this->equals("1", "1"));
  ASSERT_NO_FATAL_FAILURE(this->equals("  1  ", "(1)"));
  ASSERT_NO_FATAL_FAILURE(this->equals("(1 + 2)", "1+2"));
  ASSERT_NO_FATAL_FAILURE(this->equals("(AAA=12 BBB=34 34)", "AAA=12  BBB=34   \t  34"));
}

TEST_F(PrecedenceTest, asis) {
  ASSERT_NO_FATAL_FAILURE(this->equals("((1 as Int) is Int)", "1 as Int is Int"));
  ASSERT_NO_FATAL_FAILURE(this->equals("((1 is Int) as Int)", "1 is Int as Int"));
  ASSERT_NO_FATAL_FAILURE(this->equals("(12 / (23 as Int))", "12 / 23 as Int"));
}

TEST_F(PrecedenceTest, arith) {
  ASSERT_NO_FATAL_FAILURE(this->equals("(((1 / 2) * 3) % 4)", "1 / 2 * 3 % 4"));
  ASSERT_NO_FATAL_FAILURE(this->equals("((1 + (2 * 3)) - 4)", "1 + 2 * 3 - 4"));
}

TEST_F(PrecedenceTest, logical) {
  ASSERT_NO_FATAL_FAILURE(this->equals("((1 and 2) or (3 xor (4 + 3)))", "1 and 2 or 3 xor 4 + 3"));
}

TEST_F(PrecedenceTest, match) {
  ASSERT_NO_FATAL_FAILURE(this->equals("((((((((1 < 2) > 3) == 4) >= 5) !~ 6) <= 7) != 8) =~ 9)",
                                       "1 < 2 > 3 == 4 >= 5 !~ 6 <= 7 != 8 =~ 9"));
}

TEST_F(PrecedenceTest, cond) {
  ASSERT_NO_FATAL_FAILURE(
      this->equals("(1 = (((2 == 3) && (4 + 5)) || 6))", "1 = 2 == 3 && 4 + 5 || 6"));
  ASSERT_NO_FATAL_FAILURE(
      this->equals("(34 || ((45 | (56 and 67)) && 78))", "34 || 45 | 56 and 67 && 78"));
}

TEST_F(PrecedenceTest, ternary) {
  ASSERT_NO_FATAL_FAILURE(this->equals("((1 == 2) ? ((3 > 4) ? (5 + 6) : (7 xor 8)) : (9 && 10))",
                                       "1 == 2 ? 3 > 4 ? 5 + 6 : 7 xor 8 : 9 && 10"));
}

TEST_F(PrecedenceTest, nulcoa) {
  ASSERT_NO_FATAL_FAILURE(this->equals("(55 < ((65 or 75) ?? 78))", "55 < 65 or 75 ?? 78"));
  ASSERT_NO_FATAL_FAILURE(this->equals("(12 ?? (23 ?? 34))", "12 ?? 23 ?? 34"));
}

TEST_F(PrecedenceTest, throw_) {
  ASSERT_NO_FATAL_FAILURE(this->equals("(throw ((45 + 45) && 54))", "throw 45 + 45 && 54"));
}

TEST_F(PrecedenceTest, with) {
  ASSERT_NO_FATAL_FAILURE(
      this->equals("(45 | ((56 as Int) with > 67))", "45 | 56 as Int with > 67"));
}

TEST_F(PrecedenceTest, bg) {
  ASSERT_NO_FATAL_FAILURE(this->equals("(23 = (((45 | 56) && 78) &))", "23 = 45 | 56 && 78 &"));
  ASSERT_NO_FATAL_FAILURE(this->equals("((12 ? 23 : 34) &)", "12 ? 23 : 34 &"));
}

TEST_F(PrecedenceTest, coproc) {
  ASSERT_NO_FATAL_FAILURE(
      this->equals("(12 = (((coproc (34 | 45)) && 56) &))", "12 = coproc 34 | 45 && 56 &"));
}

TEST_F(PrecedenceTest, time) {
  ASSERT_NO_FATAL_FAILURE(this->equals("(time (12 | (34 with > 56)))", "time 12 | 34 with > 56"));
  ASSERT_NO_FATAL_FAILURE(
      this->equals("(time ((12 && 34) with > 56))", "time (12 && 34) with > 56"));
  ASSERT_NO_FATAL_FAILURE(this->equals("(((time (coproc 12)) || 34) &)", "time coproc 12 || 34 &"));
}

TEST_F(PrecedenceTest, envAssign) {
  ASSERT_NO_FATAL_FAILURE(
      this->equals("((AAA=12 BBB=34 34) = 56)", "AAA=12  BBB=34   \t  34 = 56"));
  ASSERT_NO_FATAL_FAILURE(
      this->equals("(AAA=12 BBB=34 (throw ((34 + 45) &)))", "AAA=12  BBB=34 throw 34 + 45 &"));
  ASSERT_NO_FATAL_FAILURE(this->equals("((AAA=BBB 12) && 34)", "AAA=BBB 12 && 34"));
  ASSERT_NO_FATAL_FAILURE(this->equals("((AAA=BBB 12) | 34)", "AAA=BBB 12 | 34"));
  ASSERT_NO_FATAL_FAILURE(this->equals("(AAA=BBB (12 with > 34))", "AAA=BBB 12 with > 34"));
  ASSERT_NO_FATAL_FAILURE(
      this->equals("((AAA=BBB 12) | (CCC=DDD (34 and 56)))", "AAA=BBB 12 | CCC=DDD 34 and 56"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
