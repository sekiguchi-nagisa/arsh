#include "gtest/gtest.h"

#include <fstream>

#include "../test_common.h"

#ifndef BIN_PATH
#error require BIN_PATH
#endif

using namespace ydsh;

#define EACH_DUMP_OP(OP)                                                                           \
  OP(untyped, "--dump-untyped-ast")                                                                \
  OP(typed, "--dump-ast")

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
  static constexpr unsigned int GVAR_NUM = 60;

  NodeDumpParam param;

public:
  NodeDumpTest() : INIT_TEMP_FILE_FACTORY(node_test) { this->param = GetParam(); }

  ~NodeDumpTest() override = default;

protected:
  void readContent(std::string &value) {
    std::ifstream input(this->getTempFileName());
    ASSERT_FALSE(!input);

    for (std::string line; std::getline(input, line);) {
      value += line;
      value += '\n';
    }
  }

  void test() {
    std::string dump = toOption(this->param.op);
    dump += "=";
    dump += this->getTempFileName();

    auto ret = ProcBuilder{BIN_PATH, dump.c_str(), "--compile-only", "-c", this->param.cmd}.exec();

    ASSERT_EQ(WaitStatus::EXITED, ret.kind);
    ASSERT_EQ(0, ret.value);

    auto expect = this->formatOutput();
    std::string actual;
    ASSERT_NO_FATAL_FAILURE(this->readContent(actual));

    ASSERT_EQ(expect, actual);
  }

private:
  static const char *skipFirstSpace(const char *str) {
    for (; *str != '\0' && (*str == ' ' || *str == '\t' || *str == '\n'); str++)
      ;
    return str;
  }

  std::string formatOutput() const {
    auto value = format(R"EOF(### dump %s AST ###
sourceName: "(string)"
)EOF",
                        toString(this->param.op));

    value += skipFirstSpace(this->param.dump);

    // skip last spaces
    for (; !value.empty(); value.pop_back()) {
      char ch = value.back();
      if (ch != ' ' && ch != '\t' && ch != '\n') {
        break;
      }
    }

    value += format(R"EOF(
maxVarNum: %d
maxGVarNum: %d

)EOF",
                    this->param.local, GVAR_NUM + this->param.global);

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
    kind: "Int"
    intValue: 1
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
    targetTypeToken: null
    opKind: "TO_VOID"
)"},

    {DumpOp::untyped, R"(try { var a = 'false'; } catch $e {} finally {1; })", 1, 0, R"(
nodes:
  - nodeKind: Try
    token:
      pos: 0
      size: 50
    type:
    exprNode:
      nodeKind: Block
      token:
        pos: 4
        size: 20
      type:
      nodes:
        - nodeKind: VarDecl
          token:
            pos: 6
            size: 15
          type:
          varName:
            token:
              pos: 10
              size: 1
            name: "a"
          exprNode:
            nodeKind: String
            token:
              pos: 14
              size: 7
            type:
            kind: "STRING"
            value: "false"
          kind: "VAR"
          handle: null
      baseIndex: 0
      varSize: 0
      maxVarSize: 0
      firstDeferOffset: -1
    catchNodes:
      - nodeKind: Catch
        token:
          pos: 25
          size: 11
        type:
        exceptionName:
          token:
            pos: 31
            size: 2
          name: "e"
        typeNode:
          nodeKind: Type
          token:
            pos: 0
            size: 0
          type:
          typeKind: "Base"
          typeName: "Any"
          handle: null
        blockNode:
          nodeKind: Block
          token:
            pos: 34
            size: 2
          type:
          nodes:
          baseIndex: 0
          varSize: 0
          maxVarSize: 0
          firstDeferOffset: -1
        varIndex: 0
    finallyNode:
      nodeKind: Defer
      token:
        pos: 37
        size: 13
      type:
      blockNode:
        nodeKind: Block
        token:
          pos: 45
          size: 5
        type:
        nodes:
          - nodeKind: Number
            token:
              pos: 46
              size: 1
            type:
            kind: "Int"
            intValue: 1
        baseIndex: 0
        varSize: 0
        maxVarSize: 0
        firstDeferOffset: -1
      dropLocalSize: 0
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
    baseIndex: 0
    varSize: 0
    maxVarSize: 0
    firstDeferOffset: -1
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
        size: 10
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
        handle: null
      handle: null
      additionalOp: "NOP"
    argsNode:
      nodeKind: Args
      token:
        pos: 10
        size: 2
      type:
      nodes:
    handle: null
    kind: "METHOD_CALL"
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
      kind: "Int"
      intValue: 34
    rightNode:
      nodeKind: Number
      token:
        pos: 3
        size: 1
      type:
      kind: "Int"
      intValue: 1
    op: "+"
    optNode: null
)"},

    {DumpOp::untyped, R"(function f() : typeof($/d/) {return new Regex("$true", ""); })", 0, 1, R"(
nodes:
  - nodeKind: Function
    token:
      pos: 0
      size: 61
    type:
    funcName:
      token:
        pos: 9
        size: 1
      name: "f"
    paramNodes:
    returnTypeNode:
      nodeKind: Type
      token:
        pos: 15
        size: 12
      type:
      typeKind: "Return"
      typeNodes:
        - nodeKind: Type
          token:
            pos: 15
            size: 12
          type:
          typeKind: "TypeOf"
          exprNode:
            nodeKind: Regex
            token:
              pos: 22
              size: 4
            type:
            reStr: "d"
            reFlag: ""
    recvTypeNode: null
    blockNode:
      nodeKind: Block
      token:
        pos: 28
        size: 33
      type:
      nodes:
        - nodeKind: Jump
          token:
            pos: 29
            size: 29
          type:
          opKind: "RETURN"
          fieldOffset: 0
          fieldSize: 0
          tryDepth: 0
          exprNode:
            nodeKind: New
            token:
              pos: 36
              size: 22
            type:
            targetTypeNode:
              nodeKind: Type
              token:
                pos: 40
                size: 5
              type:
              typeKind: "Base"
              typeName: "Regex"
              handle: null
            argsNode:
              nodeKind: Args
              token:
                pos: 45
                size: 13
              type:
              nodes:
                - nodeKind: StringExpr
                  token:
                    pos: 46
                    size: 7
                  type:
                  nodes:
                    - nodeKind: Embed
                      token:
                        pos: 47
                        size: 5
                      type:
                      kind: "STR_EXPR"
                      exprNode:
                        nodeKind: Var
                        token:
                          pos: 47
                          size: 5
                        type:
                        varName: "true"
                        handle: null
                      handle: null
                - nodeKind: StringExpr
                  token:
                    pos: 55
                    size: 2
                  type:
                  nodes:
            handle: null
      baseIndex: 0
      varSize: 0
      maxVarSize: 0
      firstDeferOffset: -1
    maxVarNum: 0
    handle: null
    resolvedType: null
    kind: "FUNC"
)"},

    {DumpOp::untyped, R"(assert (!ls > 34 | 34 with < ${34.1} &).poll())", 1, 0, R"(
nodes:
  - nodeKind: Assert
    token:
      pos: 0
      size: 46
    type:
    condNode:
      nodeKind: Apply
      token:
        pos: 7
        size: 39
      type:
      exprNode:
        nodeKind: Access
        token:
          pos: 7
          size: 37
        type:
        recvNode:
          nodeKind: Fork
          token:
            pos: 7
            size: 32
          type:
          exprNode:
            nodeKind: Pipeline
            token:
              pos: 8
              size: 28
            type:
            nodes:
              - nodeKind: UnaryOp
                token:
                  pos: 8
                  size: 8
                type:
                op: "!"
                exprNode:
                  nodeKind: Cmd
                  token:
                    pos: 9
                    size: 7
                  type:
                  nameNode:
                    nodeKind: String
                    token:
                      pos: 9
                      size: 2
                    type:
                    kind: "STRING"
                    value: "ls"
                  argNodes:
                    - nodeKind: Redir
                      token:
                        pos: 14
                        size: 2
                      type:
                      op: "1>"
                      targetNode:
                        nodeKind: CmdArg
                        token:
                          pos: 14
                          size: 2
                        type:
                        globPathSize: 0
                        segmentNodes:
                          - nodeKind: String
                            token:
                              pos: 14
                              size: 2
                            type:
                            kind: "STRING"
                            value: "34"
                  redirCount: 1
                  needFork: true
                  handle: null
                methodCallNode: null
              - nodeKind: With
                token:
                  pos: 19
                  size: 17
                type:
                exprNode:
                  nodeKind: Number
                  token:
                    pos: 19
                    size: 2
                  type:
                  kind: "Int"
                  intValue: 34
                redirNodes:
                  - nodeKind: Redir
                    token:
                      pos: 29
                      size: 7
                    type:
                    op: "<"
                    targetNode:
                      nodeKind: CmdArg
                      token:
                        pos: 29
                        size: 7
                      type:
                      globPathSize: 0
                      segmentNodes:
                        - nodeKind: Embed
                          token:
                            pos: 29
                            size: 7
                          type:
                          kind: "CMD_ARG"
                          exprNode:
                            nodeKind: Number
                            token:
                              pos: 31
                              size: 4
                            type:
                            kind: "Float"
                            floatValue: 34.100000
                          handle: null
                baseIndex: 0
            baseIndex: 0
            inFork: true
          opKind: "ForkKind::JOB"
        nameNode:
          nodeKind: Var
          token:
            pos: 40
            size: 4
          type:
          varName: "poll"
          handle: null
        handle: null
        additionalOp: "NOP"
      argsNode:
        nodeKind: Args
        token:
          pos: 44
          size: 2
        type:
        nodes:
      handle: null
      kind: "METHOD_CALL"
    messageNode:
      nodeKind: String
      token:
        pos: 7
        size: 39
      type:
      kind: "STRING"
      value: "`(!ls > 34 | 34 with < ${34.1} &).poll()'"
)"},

    {DumpOp::typed, R"(case %'int' { %'int' => [34:34]; else => (34,)})", 0, 0, R"(
nodes:
  - nodeKind: Case
    token:
      pos: 0
      size: 47
    type: Void
    exprNode:
      nodeKind: Number
      token:
        pos: 5
        size: 6
      type: Signal
      kind: "Signal"
      intValue: 2
    armNodes:
      - nodeKind: Arm
        token:
          pos: 14
          size: 17
        type: Void
        this->patternNodes:
          - nodeKind: Number
            token:
              pos: 14
              size: 6
            type: Signal
            kind: "Signal"
            intValue: 2
        this->constPatternNodes:
          - nodeKind: Number
            token:
              pos: 14
              size: 6
            type: Signal
            kind: "Signal"
            intValue: 2
        this->actionNode:
          nodeKind: TypeOp
          token:
            pos: 24
            size: 7
          type: Void
          exprNode:
            nodeKind: Map
            token:
              pos: 24
              size: 7
            type: [Int : Int]
            keyNodes:
              - nodeKind: Number
                token:
                  pos: 25
                  size: 2
                type: Int
                kind: "Int"
                intValue: 34
            valueNodes:
              - nodeKind: Number
                token:
                  pos: 28
                  size: 2
                type: Int
                kind: "Int"
                intValue: 34
          targetTypeToken: null
          opKind: "TO_VOID"
      - nodeKind: Arm
        token:
          pos: 33
          size: 13
        type: Void
        this->patternNodes:
        this->constPatternNodes:
        this->actionNode:
          nodeKind: TypeOp
          token:
            pos: 41
            size: 5
          type: Void
          exprNode:
            nodeKind: Tuple
            token:
              pos: 41
              size: 5
            type: (Int,)
            nodes:
              - nodeKind: Number
                token:
                  pos: 42
                  size: 2
                type: Int
                kind: "Int"
                intValue: 34
          targetTypeToken: null
          opKind: "TO_VOID"
    caseKind: "MAP"
)"},

    {DumpOp::untyped, R"(typedef i = (Int) -> Int)", 0, 0, R"(
nodes:
  - nodeKind: TypeDef
    token:
      pos: 0
      size: 24
    type:
    nameInfo:
      token:
        pos: 8
        size: 1
      name: "i"
    targetTypeNode:
      nodeKind: Type
      token:
        pos: 12
        size: 12
      type:
      typeKind: "Func"
      returnTypeNode:
        nodeKind: Type
        token:
          pos: 21
          size: 3
        type:
        typeKind: "Base"
        typeName: "Int"
        handle: null
      paramTypeNodes:
        - nodeKind: Type
          token:
            pos: 13
            size: 3
          type:
          typeKind: "Base"
          typeName: "Int"
          handle: null
    kind: "ALIAS"
)"},

    {DumpOp::typed, R"(while($false){})", 0, 0, R"(
nodes:
  - nodeKind: Loop
    token:
      pos: 0
      size: 15
    type: Void
    initNode:
      nodeKind: Empty
      token:
        pos: 0
        size: 0
      type: Void
    condNode:
      nodeKind: Var
      token:
        pos: 5
        size: 8
      type: Boolean
      varName: "false"
      handle:
        index: 49
        kind: "VAR"
        attribute: "READ_ONLY | GLOBAL"
    iterNode:
      nodeKind: Empty
      token:
        pos: 0
        size: 0
      type: Void
    blockNode:
      nodeKind: Block
      token:
        pos: 13
        size: 2
      type: Nothing
      nodes:
        - nodeKind: Jump
          token:
            pos: 0
            size: 0
          type: Nothing
          opKind: "CONTINUE"
          fieldOffset: 0
          fieldSize: 0
          tryDepth: 0
          exprNode:
            nodeKind: Empty
            token:
              pos: 0
              size: 0
            type: Void
      baseIndex: 0
      varSize: 0
      maxVarSize: 0
      firstDeferOffset: -1
    asDoWhile: false
)"},

    {DumpOp::typed, R"(f() {})", 0, 1, R"(
nodes:
  - nodeKind: UserDefinedCmd
    token:
      pos: 0
      size: 6
    type: Void
    cmdName:
      token:
        pos: 0
        size: 1
      name: "f"
    udcIndex: 60
    returnTypeNode: null
    blockNode:
      nodeKind: Block
      token:
        pos: 4
        size: 2
      type: Nothing
      nodes:
        - nodeKind: Jump
          token:
            pos: 6
            size: 0
          type: Nothing
          opKind: "RETURN"
          fieldOffset: 0
          fieldSize: 0
          tryDepth: 0
          exprNode:
            nodeKind: Var
            token:
              pos: 6
              size: 0
            type: Int
            varName: "?"
            handle:
              index: 12
              kind: "VAR"
              attribute: "GLOBAL"
      baseIndex: 0
      varSize: 14
      maxVarSize: 14
      firstDeferOffset: -1
    maxVarNum: 14
)"},

    {DumpOp::untyped, R"(IFS=1234)", 0, 0, R"(
nodes:
  - nodeKind: PrefixAssign
    token:
      pos: 0
      size: 8
    type:
    declNodes:
      - nodeKind: Assign
        token:
          pos: 0
          size: 8
        type:
        leftNode:
          nodeKind: Var
          token:
            pos: 0
            size: 3
          type:
          varName: "IFS"
          handle: null
        rightNode:
          nodeKind: CmdArg
          token:
            pos: 4
            size: 4
          type:
          globPathSize: 0
          segmentNodes:
            - nodeKind: String
              token:
                pos: 4
                size: 4
              type:
              kind: "STRING"
              value: "1234"
        attributeSet: ""
    exprNode: null
    baseIndex: 0
)"},

    {DumpOp::typed, R"(echo *)", 0, 0, R"(
nodes:
  - nodeKind: TypeOp
    token:
      pos: 0
      size: 6
    type: Void
    exprNode:
      nodeKind: Cmd
      token:
        pos: 0
        size: 6
      type: Boolean
      nameNode:
        nodeKind: String
        token:
          pos: 0
          size: 4
        type: String
        kind: "STRING"
        value: "echo"
      argNodes:
        - nodeKind: CmdArg
          token:
            pos: 5
            size: 1
          type: [String]
          globPathSize: 1
          segmentNodes:
            - nodeKind: WildCard
              token:
                pos: 5
                size: 1
              type: String
              meta: "*"
      redirCount: 0
      needFork: true
      handle: null
    targetTypeToken: null
    opKind: "TO_VOID"
)"},

    {DumpOp::typed, R"(function f($a : Int, $b : Int) {})", 0, 1, R"(
nodes:
  - nodeKind: Function
    token:
      pos: 0
      size: 33
    type: Void
    funcName:
      token:
        pos: 9
        size: 1
      name: "f"
    paramNodes:
      - nodeKind: VarDecl
        token:
          pos: 11
          size: 8
        type: Void
        varName:
          token:
            pos: 11
            size: 2
          name: "a"
        exprNode:
          nodeKind: Type
          token:
            pos: 16
            size: 3
          type: Int
          typeKind: "Base"
          typeName: "Int"
          handle: null
        kind: "VAR"
        handle:
          index: 0
          kind: "VAR"
          attribute: ""
      - nodeKind: VarDecl
        token:
          pos: 21
          size: 8
        type: Void
        varName:
          token:
            pos: 21
            size: 2
          name: "b"
        exprNode:
          nodeKind: Type
          token:
            pos: 26
            size: 3
          type: Int
          typeKind: "Base"
          typeName: "Int"
          handle: null
        kind: "VAR"
        handle:
          index: 1
          kind: "VAR"
          attribute: ""
    returnTypeNode:
      nodeKind: Type
      token:
        pos: 0
        size: 0
      type: Void
      typeKind: "Base"
      typeName: "Void"
      handle: null
    recvTypeNode: null
    blockNode:
      nodeKind: Block
      token:
        pos: 31
        size: 2
      type: Nothing
      nodes:
        - nodeKind: Jump
          token:
            pos: 0
            size: 0
          type: Nothing
          opKind: "RETURN"
          fieldOffset: 0
          fieldSize: 0
          tryDepth: 0
          exprNode:
            nodeKind: Empty
            token:
              pos: 0
              size: 0
            type: Void
      baseIndex: 0
      varSize: 2
      maxVarSize: 2
      firstDeferOffset: -1
    maxVarNum: 2
    handle:
      index: 60
      kind: "VAR"
      attribute: "READ_ONLY | GLOBAL"
    resolvedType: "(Int, Int) -> Void"
    kind: "FUNC"
)"},
};

TEST_P(NodeDumpTest, base) { ASSERT_NO_FATAL_FAILURE(this->test()); }

INSTANTIATE_TEST_SUITE_P(NodeDumpTest, NodeDumpTest, ::testing::ValuesIn(paramTable));

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}