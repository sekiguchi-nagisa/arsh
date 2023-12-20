/*
 * Copyright (C) 2016-2018 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ARSH_CODEGEN_H
#define ARSH_CODEGEN_H

#include <cmath>
#include <functional>
#include <utility>

#include "cgerror.h"
#include "lexer.h"
#include "misc/emitter.hpp"
#include "node.h"
#include "object.h"
#include "opcode.h"
#include "ordered_map.h"

#define ASSERT_BYTE_SIZE(op, size) assert(getByteSize(op) == (size))

namespace arsh {

class CatchBuilder {
private:
  Label begin; // inclusive
  Label end;   // exclusive
  const DSType *type{nullptr};
  unsigned int address{0}; // start index of catch block.
  unsigned short localOffset{0};
  unsigned short localSize{0};
  unsigned int guardLevel{0};

public:
  CatchBuilder() = default;
  CatchBuilder(Label begin, Label end, const DSType &type, unsigned int address,
               unsigned short localOffset, unsigned short localSize, unsigned int level)
      : begin(std::move(begin)), end(std::move(end)), type(&type), address(address),
        localOffset(localOffset), localSize(localSize), guardLevel(level) {}

  ~CatchBuilder() = default;

  ExceptionEntry toEntry() const {
    assert(this->begin);
    assert(this->end);
    assert(this->begin->getIndex() < this->end->getIndex());
    assert(this->address > 0);
    assert(this->type != nullptr);

    return ExceptionEntry{
        .typeId = this->type->typeId(),
        .begin = this->begin->getIndex(),
        .end = this->end->getIndex(),
        .dest = this->address,
        .localOffset = this->localOffset,
        .localSize = this->localSize,
        .guardLevel = this->guardLevel,
    };
  }
};

struct TryFinallyState {
  Label beginLabel;   // try-begin
  Label endLabel;     // try-end
  Label finallyLabel; // may be null

  unsigned short localOffset; // for reclaim local variables
  unsigned short localSize;   // for reclaim local variables
  bool defer;
};

struct LoopState {
  Label breakLabel;

  Label continueLabel;

  Label breakWithValueLabel;

  /**
   * for local variable reclaim
   */
  unsigned int blockIndex;
};

class ConstBuffer {
private:
  std::unique_ptr<Value[]> values;
  unsigned int size{0};
  unsigned int cap{8};

public:
  ConstBuffer() : values(new Value[8]) {}

  unsigned int getSize() const { return this->size; }

  void append(Value &&value);

  Value *take() && {
    this->size = 0;
    this->cap = 0;
    return this->values.release();
  }
};

enum class CmdCallCtx : unsigned char {
  STMT,
  EXPR,
  AUTO, // propagate current context
};

struct CodeBuilder : public CodeEmitter<true> {
  LexerPtr lexer;

  const CodeKind kind;

  unsigned char localVarNum;

  ModId modId;

  signed short stackDepthCount{0};
  signed short maxStackDepth{0};

  ConstBuffer constBuffer;
  FlexBuffer<LineNumEntry> lineNumEntries;
  std::vector<CatchBuilder> catchBuilders;

  /**
   * first is local offset, second is local size
   */
  std::vector<std::pair<unsigned short, unsigned short>> localVars;

  /**
   * first is break label, second is continue label
   */
  std::vector<LoopState> loopLabels;

  std::vector<TryFinallyState> tryFinallyLabels;

  std::vector<std::unique_ptr<DeferNode>> toplevelDeferNodes;

  /**
   * for command call
   * if true, current is statement context (may ignore return value of command)
   */
  std::vector<bool> cmdCallCtxs;

  explicit CodeBuilder(ModId modId, LexerPtr lexer, CodeKind kind, unsigned char localVarNum)
      : lexer(std::move(lexer)), kind(kind), localVarNum(localVarNum), modId(modId) {
    this->cmdCallCtxs.push_back(true);
  }

  CodeKind getCodeKind() const { return this->kind; }

  /**
   * after build, remove allocated buffer.
   */
  CompiledCode build(const std::string &name);
};

class ByteCodeGenerator : protected NodeVisitor {
private:
  TypePool &typePool;

  const MethodHandle *handle_STR{nullptr};

  std::vector<CodeBuilder> builders;

  CodeGenError error;

public:
  explicit ByteCodeGenerator(TypePool &pool) : typePool(pool) {}

  ~ByteCodeGenerator() override = default;

private:
  CodeBuilder &curBuilder() noexcept {
    assert(!this->builders.empty());
    return this->builders.back();
  }

  const CodeBuilder &curBuilder() const noexcept {
    assert(!this->builders.empty());
    return this->builders.back();
  }

  auto &tryFinallyLabels() { return this->curBuilder().tryFinallyLabels; }

  auto &toplevelDeferNodes() { return this->curBuilder().toplevelDeferNodes; }

  bool inUDC() const { return this->curBuilder().getCodeKind() == CodeKind::USER_DEFINED_CMD; }

  bool inFunc() const { return this->curBuilder().getCodeKind() == CodeKind::FUNCTION; }

  bool inStmtCtx() const { return this->curBuilder().cmdCallCtxs.back(); }

  void emitIns(OpCode op);

  void emit0byteIns(OpCode op) {
    ASSERT_BYTE_SIZE(op, 0);
    this->emitIns(op);
  }

  void emit1byteIns(OpCode op, unsigned char v) {
    ASSERT_BYTE_SIZE(op, 1);
    this->emitIns(op);
    this->curBuilder().append8(v);
  }

  void emit2byteIns(OpCode op, unsigned short v) {
    ASSERT_BYTE_SIZE(op, 2);
    this->emitIns(op);
    this->curBuilder().append16(v);
  }

  void emit2byteIns(OpCode op, unsigned char v1, unsigned char v2) {
    assert(op == OpCode::RECLAIM_LOCAL || op == OpCode::PUSH_STR2 || op == OpCode::INIT_FIELDS ||
           op == OpCode::PUSH_META || op == OpCode::ADD_REDIR_OP);
    ASSERT_BYTE_SIZE(op, 2);
    this->emitIns(op);
    this->curBuilder().append8(v1);
    this->curBuilder().append8(v2);
  }

  void emit3byteIns(OpCode op, unsigned int v) {
    ASSERT_BYTE_SIZE(op, 3);
    this->emitIns(op);
    this->curBuilder().append24(v);
  }

  void emit3byteIns(OpCode op, unsigned char v1, unsigned char v2, unsigned char v3) {
    assert(op == OpCode::PUSH_STR3);
    ASSERT_BYTE_SIZE(op, 3);
    this->emitIns(op);
    this->curBuilder().append8(v1);
    this->curBuilder().append8(v2);
    this->curBuilder().append8(v3);
  }

  void emit4byteIns(OpCode op, unsigned int v) {
    ASSERT_BYTE_SIZE(op, 4);
    this->emitIns(op);
    this->curBuilder().append32(v);
  }

  /**
   * for variable length operands
   * @param op
   * @param paramSize
   * variable length operands size
   * @param restSize
   * rest operands size
   */
  void emitValIns(OpCode op, unsigned char paramSize, short restSize) {
    assert(op == OpCode::CALL_FUNC || op == OpCode::CALL_METHOD || op == OpCode::CALL_BUILTIN ||
           op == OpCode::ADD_EXPANDING || op == OpCode::NEW_CLOSURE);
    this->curBuilder().append8(toUnderlying(op));
    this->curBuilder().append8(paramSize);

    int size = static_cast<int>(paramSize) + restSize;
    this->curBuilder().stackDepthCount -= static_cast<short>(size);
  }

  void emitFuncCallIns(unsigned char paramSize, bool hasRet) {
    this->emitValIns(OpCode::CALL_FUNC, paramSize, 0);
    if (!hasRet) {
      this->emit0byteIns(OpCode::POP);
    }
  }

  void emitNativeCallIns(unsigned char paramSize, unsigned short index, bool hasRet) {
    assert(index <= UINT8_MAX);
    this->emitValIns(OpCode::CALL_BUILTIN, paramSize, -1);
    this->curBuilder().append8(index);
    if (!hasRet) {
      this->emit0byteIns(OpCode::POP);
    }
  }

  /**
   * @param handle
   * target method
   */
  void emitMethodCallIns(const MethodHandle &handle);

  void emitExpandIns(unsigned char paramSize, ExpandOp op) {
    this->emitValIns(OpCode::ADD_EXPANDING, paramSize, 1);
    this->curBuilder().append8(toUnderlying(op));
  }

  void emitNewClosureIns(unsigned char capturedSize) {
    this->emitValIns(OpCode::NEW_CLOSURE, capturedSize, 0);
  }

  /**
   * write instruction having type. (ex. PRINT).
   */
  void emitTypeIns(OpCode op, const DSType &type) {
    assert(isTypeOp(op));
    this->emit3byteIns(op, type.typeId());
  }

  unsigned int currentCodeOffset() const { return this->curBuilder().codeBuffer.size(); }

  unsigned int emitConstant(Value &&value);

  void emitLdcIns(const Value &value) { this->emitLdcIns(Value(value)); }

  void emitLdcIns(Value &&value);

  void emitInt(int64_t v) {
    if (v >= 0 && v <= UINT8_MAX) {
      this->emit1byteIns(OpCode::PUSH_INT, static_cast<unsigned char>(v));
    } else {
      this->emitLdcIns(Value::createInt(v));
    }
  }

  void emitString(std::string &&value);

  void emitToString();
  void emitBranchIns(OpCode op, const Label &label);

  void emitBranchIns(const Label &label) { this->emitBranchIns(OpCode::BRANCH, label); }

  void emitForkIns(ForkKind kind, const Label &label);

  void emitJumpIns(const Label &label, OpCode op = OpCode::GOTO);
  void markLabel(Label &label);

  void pushLoopLabels(Label breakLabel, Label continueLabel, Label breakWithValueLabel);

  void popLoopLabels() { this->curBuilder().loopLabels.pop_back(); }

  const LoopState &peekLoopLabels() const { return this->curBuilder().loopLabels.back(); }

  /**
   *
   * @param node
   * not empty block
   * @return
   */
  bool needReclaim(const BlockNode &node) const {
    if (node.getNodes().empty() || node.getVarSize() == 0) {
      return false;
    }

    if ((*node.getNodes().rbegin())->getType().isNothingType()) {
      return false;
    }

    if (node.getFirstDeferOffset() > -1) {
      if (static_cast<unsigned int>(node.getFirstDeferOffset()) - node.getBaseIndex() == 0) {
        return false;
      }
    }

    // when toplevel block of function or udc
    if ((this->inFunc() || this->inUDC()) && this->curBuilder().localVars.empty()) {
      return false;
    }
    return true;
  }

  template <typename Func>
  void generateBlock(unsigned short localOffset, unsigned short localSize, bool needReclaim,
                     Func func) {
    this->curBuilder().localVars.emplace_back(localOffset, localSize);

    func();

    if (needReclaim) {
      this->emit2byteIns(OpCode::RECLAIM_LOCAL, localOffset, localSize);
    }
    this->curBuilder().localVars.pop_back();
  }

  /**
   * for line number
   */
  void emitSourcePos(unsigned int pos);

  void emitTryGuard(unsigned int level) {
    if (level == 1) {
      this->emit0byteIns(OpCode::TRY_GUARD0);
    } else if (level <= UINT8_MAX) {
      this->emit1byteIns(OpCode::TRY_GUARD1, static_cast<unsigned char>(level));
    } else {
      this->emit4byteIns(OpCode::TRY_GUARD, level);
    }
  }

  /**
   * generate catch-block
   * if begin == end (try-block is empty), omit catch block
   * @param begin
   * begin of try-block (inclusive)
   * must be marked
   * @param end
   * end of try-block (exclusive)
   * must be marked
   * @param type
   * catching exception type
   * @param localOffset
   * try block local variable offset (for stack unwinding)
   * @param localSize
   * try block local variable size (for stack unwinding)
   * @param level
   * try block guard level
   */
  void catchException(const Label &begin, const Label &end, const DSType &type,
                      unsigned short localOffset, unsigned short localSize, unsigned int level);

  void guardChildProc(const Label &begin, const Label &end, const DSType &type) {
    this->catchException(begin, end, type, 0, 0, 0);
  }

  void enterFinally(const Label &label) { this->emitJumpIns(label, OpCode::ENTER_FINALLY); }

  /**
   *
   * @param depth
   * @param localOffset
   * for reclaim local variables
   * @param localSize
   * for reclaim local variables
   */
  void enterMultiFinally(unsigned int depth, unsigned int localOffset = 0,
                         unsigned int localSize = 0);

  unsigned int concatCmdArgSegment(CmdArgNode &node, unsigned int index);

  void generateCmdArg(CmdArgNode &node) {
    unsigned int size = node.getSegmentNodes().size();
    for (unsigned int index = 0; index < size; index = this->concatCmdArgSegment(node, index))
      ;
  }

  void generatePipeline(PipelineNode &node, ForkKind forkKind);
  void emitPipelineIns(const std::vector<Label> &labels, bool lastPipe, ForkKind forkKind);

  void generateConcat(Node &node, bool fragment = false);

  void generateBreakContinue(JumpNode &node);

  void generateMapCase(CaseNode &node);
  void generateCaseLabels(const ArmNode &node, OrderedMapObject &obj) const;
  void generateIfElseCase(CaseNode &node);
  void generateIfElseArm(ArmNode &node, const MethodHandle &eqHandle,
                         const MethodHandle &matchHandle, const Label &mergeLabel);

  void initToplevelCodeBuilder(ModId modId, LexerPtr lex, unsigned short localVarNum) {
    assert(lex->getScriptDir());
    this->initCodeBuilder(CodeKind::TOPLEVEL, modId, std::move(lex), localVarNum);
  }

  /**
   * for function or user-defined command
   * @param kind
   * @param localVarNum
   */
  void initFuncCodeBuilder(CodeKind kind, unsigned short localVarNum) {
    auto lex = this->builders.back().lexer;
    auto modId = this->builders.back().modId;
    this->initCodeBuilder(kind, modId, lex, localVarNum);
  }

  void initCodeBuilder(CodeKind kind, ModId modId, LexerPtr lex, unsigned short localVarNum) {
    this->builders.emplace_back(modId, std::move(lex), kind, localVarNum);
    if (kind == CodeKind::TOPLEVEL) {
      auto &lexer = *this->curBuilder().lexer;
      this->curBuilder().constBuffer.append(Value::createStr(lexer.getSourceName()));
      this->curBuilder().constBuffer.append(Value::createStr(lexer.getScriptDir()));
    }
  }

  CompiledCode finalizeCodeBuilder(const std::string &name) {
    auto code = this->curBuilder().build(name);
    this->builders.pop_back();
    return code;
  }

  /**
   *
   * @param token
   * for error reporting
   * @param maxVarIndex
   * @param modType
   * @return
   * if code generation failed, report error and return null
   */
  ObjPtr<FuncObject> finalizeToplevelCodeBuilder(Token token, unsigned int maxVarIndex,
                                                 const ModType &modType);

  void reportErrorImpl(Token token, const char *kind, const char *fmt, ...)
      __attribute__((format(printf, 4, 5)));

  template <typename T, typename... Arg, typename = base_of_t<T, CGError>>
  void reportError(const Node &node, Arg &&...arg) {
    return this->reportErrorImpl(node.getToken(), T::kind, T::value, std::forward<Arg>(arg)...);
  }

  template <typename T, typename... Arg, typename = base_of_t<T, CGError>>
  void reportError(Token token, Arg &&...arg) {
    return this->reportErrorImpl(token, T::kind, T::value, std::forward<Arg>(arg)...);
  }

  /**
   * actual visitor entry point
   * @param node
   * @param cmdCallCtx
   */
  void visit(Node &node, CmdCallCtx cmdCallCtx);

  // visitor api
  void visit(Node &node) override;
  void visitTypeNode(TypeNode &node) override;
  void visitNumberNode(NumberNode &node) override;
  void visitStringNode(StringNode &node) override;
  void visitStringExprNode(StringExprNode &node) override;
  void visitRegexNode(RegexNode &node) override;
  void visitArrayNode(ArrayNode &node) override;
  void visitMapNode(MapNode &node) override;
  void visitTupleNode(TupleNode &node) override;
  void visitVarNode(VarNode &node) override;
  void visitAccessNode(AccessNode &node) override;
  void visitTypeOpNode(TypeOpNode &node) override;
  void visitUnaryOpNode(UnaryOpNode &node) override;
  void visitBinaryOpNode(BinaryOpNode &node) override;
  void visitArgsNode(ArgsNode &node) override;
  void visitApplyNode(ApplyNode &node) override;
  void visitNewNode(NewNode &node) override;
  void visitEmbedNode(EmbedNode &node) override;
  void visitCmdNode(CmdNode &node) override;
  void visitCmdArgNode(CmdArgNode &node) override;
  void visitArgArrayNode(ArgArrayNode &node) override;
  void visitRedirNode(RedirNode &node) override;
  void visitWildCardNode(WildCardNode &node) override;
  void visitBraceSeqNode(BraceSeqNode &node) override;
  void visitPipelineNode(PipelineNode &node) override;
  void visitWithNode(WithNode &node) override;
  void visitForkNode(ForkNode &node) override;
  void visitAssertNode(AssertNode &node) override;
  void visitBlockNode(BlockNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
  void visitDeferNode(DeferNode &node) override;
  void visitLoopNode(LoopNode &node) override;
  void visitIfNode(IfNode &node) override;
  void visitCaseNode(CaseNode &node) override;
  void visitArmNode(ArmNode &node) override;
  void visitJumpNode(JumpNode &node) override;
  void visitCatchNode(CatchNode &node) override;
  void visitTryNode(TryNode &node) override;
  void visitVarDeclNode(VarDeclNode &node) override;
  void visitAttributeNode(AttributeNode &node) override;
  void visitAssignNode(AssignNode &node) override;
  void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
  void visitPrefixAssignNode(PrefixAssignNode &node) override;
  void visitTimeNode(TimeNode &node) override;
  void visitFunctionNode(FunctionNode &node) override;
  void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
  void visitFuncListNode(FuncListNode &node) override;
  void visitSourceNode(SourceNode &node) override;
  void visitSourceListNode(SourceListNode &node) override;
  void visitCodeCompNode(CodeCompNode &node) override;
  void visitErrorNode(ErrorNode &node) override;
  void visitEmptyNode(EmptyNode &node) override;

public:
  bool hasError() const { return static_cast<bool>(this->error); }

  const CodeGenError &getError() const { return this->error; }

  void initialize(ModId modId, LexerPtr lexer) {
    this->initToplevelCodeBuilder(modId, std::move(lexer), 0);
  }

  bool generate(std::unique_ptr<Node> &&node);

  ObjPtr<FuncObject> finalize(unsigned int maxVarIndex, const ModType &modType) {
    return this->finalizeToplevelCodeBuilder({0, 0}, maxVarIndex, modType);
  }

  void enterModule(ModId modId, LexerPtr lexer) {
    this->initToplevelCodeBuilder(modId, std::move(lexer), 0);
  }

  bool exitModule(const SourceNode &node);
};

class ByteCodeDumper {
private:
  /**
   * may be null
   */
  FILE *fp;

  const TypePool &typePool;
  unsigned int maxGVarIndex{0};

  std::vector<std::reference_wrapper<const CompiledCode>> mods;
  std::vector<std::reference_wrapper<const CompiledCode>> funcs;

public:
  ByteCodeDumper(FILE *fp, const TypePool &pool) : fp(fp), typePool(pool) {}

  void operator()(const CompiledCode &code, unsigned int maxGVarIndex);

  explicit operator bool() const { return this->fp != nullptr; }

private:
  void dumpModule(const CompiledCode &code);

  void dumpCode(const CompiledCode &c);
};

} // namespace arsh

#endif // ARSH_CODEGEN_H
