#include "gtest/gtest.h"

#include <fstream>

#include "../test_common.h"

#ifndef BIN_PATH
#error require BIN_PATH
#endif

using namespace ydsh;

#define EACH_DUMP_OP(OP) \
    OP(untyped, "--dump-untyped-ast") \
    OP(typed,  "--dump-ast")


enum class DumpOp {
#define GEN_ENUM(E, S) E,
    EACH_DUMP_OP(GEN_ENUM)
#undef GEN_ENUM
};

static const char *toString(DumpOp op) {
    const char *table[] = {
#define GEN_STR(E, S) #E,
            EACH_DUMP_OP(GEN_STR)
#undef GEN_STR
    };
    return table[static_cast<unsigned int>(op)];
}

static const char *toOption(DumpOp op) {
    const char *table[] = {
#define GEN_STR(E, S) S,
        EACH_DUMP_OP(GEN_STR)
#undef GEN_STR
    };
    return table[static_cast<unsigned int>(op)];
}

struct NodeDumpParam {
    DumpOp op;
    const char *cmd;
    unsigned int local;
    unsigned int global;
    const char *dump;
};


class NodeDumpTest : public ::testing::TestWithParam<NodeDumpParam>, public TempFileFactory {
protected:
    static constexpr unsigned int GVAR_NUM = 48;

    NodeDumpParam param;

public:
    NodeDumpTest() {
        this->param = this->GetParam();
    }

    ~NodeDumpTest() override = default;

protected:
    void readContent(std::string &value) {
        std::ifstream input(this->getTempFileName());
        ASSERT_FALSE(!input);

        for(std::string line; std::getline(input, line); ) {
            value += line;
            value += '\n';
        }
    }

    void test() {
        std::string dump = toOption(this->param.op);
        dump += "=";
        dump += this->getTempFileName();

        auto ret = ProcBuilder {
                BIN_PATH,
                dump.c_str(),
                "-c",
                this->param.cmd
        }.exec();

        ASSERT_EQ(WaitStatus::EXITED, ret.kind);
        ASSERT_EQ(0, ret.value);

        auto expect = this->formatOutput();
        std::string actual;
        ASSERT_NO_FATAL_FAILURE(this->readContent(actual));

        ASSERT_EQ(expect, actual);
    }

private:
    static const char *skipFirstSpace(const char *str) {
        for(; *str != '\0' && (*str == ' ' || *str == '\t' || *str == '\n'); str++);
        return str;
    }

    std::string formatOutput() const {
        auto value = format(R"EOF(### dump %s AST ###
sourceName: "(string)"
)EOF", toString(this->param.op));

        value += skipFirstSpace(this->param.dump);

        // skip last spaces
        for(; !value.empty(); value.pop_back()) {
            char ch = value.back();
            if(ch != ' ' && ch != '\t' && ch != '\n') {
                break;
            }
        }

        value += format(R"EOF(
maxVarNum: "%d"
maxGVarNum: "%d"

)EOF", this->param.local, GVAR_NUM + this->param.global);

        return value;
    }
};


// test parameter definition
static constexpr NodeDumpParam paramTable[] = {
        {DumpOp::untyped, "1", 0, 0, R"(
nodes:
  - nodeKind: Number
    token:
      pos: 0
      size: 1
    type:
    kind: "Int32"
    intValue: "1"
)"},

        {DumpOp::typed, R"("hello")", 0, 0, R"(
nodes:
  - nodeKind: TypeOp
    token:
      pos: 0
      size: 7
    type: Void
    exprNode:
      nodeKind: StringExpr
      token:
        pos: 0
        size: 7
      type: String
      nodes:
        - nodeKind: String
          token:
            pos: 1
            size: 5
          type: String
          kind: "STRING"
          value: "hello"
    targetTypeToken:
    opKind: "TO_VOID"

)"},

        {DumpOp::typed, R"(var a = 'false')", 0, 1, R"(
nodes:
  - nodeKind: VarDecl
    token:
      pos: 0
      size: 15
    type: Void
    varName: "a"
    global: "1"
    varIndex: "48"
    exprNode:
      nodeKind: String
      token:
        pos: 8
        size: 7
      type: String
      kind: "STRING"
      value: "false"
    kind: "VAR"
)"},

        {DumpOp::typed, R"({;})", 0, 0, R"(
nodes:
  - nodeKind: Block
    token:
      pos: 0
      size: 3
    type: Void
    nodes:
      - nodeKind: Empty
        token:
          pos: 1
          size: 1
        type: Void
    baseIndex: "0"
    varSize: "0"
    maxVarSize: "0"
)"},

        {DumpOp::untyped, R"('hey'.size())", 0, 0, R"(
nodes:
  - nodeKind: Apply
    token:
      pos: 0
      size: 12
    type:
    exprNode:
      nodeKind: Access
      token:
        pos: 0
        size: 5
      type:
      recvNode:
        nodeKind: String
        token:
          pos: 0
          size: 5
        type:
        kind: "STRING"
        value: "hey"
      nameNode:
        nodeKind: Var
        token:
          pos: 6
          size: 4
        type:
        varName: "size"
        index: "0"
        attribute: ""
      index: "0"
      attribute: ""
      additionalOp: "NOP"
    argNodes:
    methodIndex: "0"
    kind: "UNRESOLVED"
)"},

        {DumpOp::untyped, R"(34+1)", 0, 0, R"(
nodes:
  - nodeKind: BinaryOp
    token:
      pos: 0
      size: 4
    type:
    leftNode:
      nodeKind: Number
      token:
        pos: 0
        size: 2
      type:
      kind: "Int32"
      intValue: "34"
    rightNode:
      nodeKind: Number
      token:
        pos: 3
        size: 1
      type:
      kind: "Int32"
      intValue: "1"
    op: "+"
    optNode:
)"},
};


TEST_P(NodeDumpTest, base) {
    ASSERT_NO_FATAL_FAILURE(this->test());
}

INSTANTIATE_TEST_CASE_P(NodeDumpTest, NodeDumpTest, ::testing::ValuesIn(paramTable));

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}