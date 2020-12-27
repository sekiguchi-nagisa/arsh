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

#ifndef YDSH_CODEGEN_H
#define YDSH_CODEGEN_H

#include <cmath>
#include <utility>
#include <functional>

#include "node.h"
#include "object.h"
#include "opcode.h"
#include "cgerror.h"
#include "misc/emitter.hpp"

#define ASSERT_BYTE_SIZE(op, size) assert(getByteSize(op) == (size))

namespace ydsh {

class CatchBuilder {
private:
    Label begin;  // inclusive
    Label end;    // exclusive
    const DSType *type{nullptr};
    unsigned int address{0};   // start index of catch block.
    unsigned short localOffset{0};
    unsigned short localSize{0};

public:
    CatchBuilder() = default;
    CatchBuilder(Label begin, Label end, const DSType &type,
                 unsigned int address, unsigned short localOffset, unsigned short localSize) :
            begin(std::move(begin)), end(std::move(end)), type(&type),
            address(address), localOffset(localOffset), localSize(localSize) {}

    ~CatchBuilder() = default;

    ExceptionEntry toEntry() const {
        assert(this->begin);
        assert(this->end);
        assert(this->address > 0);
        assert(this->type != nullptr);

        return ExceptionEntry {
                .type = this->type,
                .begin = this->begin->getIndex(),
                .end = this->end->getIndex(),
                .dest = this->address,
                .localOffset = this->localOffset,
                .localSize = this->localSize,
        };
    }
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

struct CodeBuilder : public CodeEmitter<true> {
    const Lexer &lexer;

    CodeKind kind;

    unsigned char localVarNum;

    std::vector<DSValue> constBuffer;
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

    std::vector<Label> finallyLabels;

    signed short stackDepthCount{0};
    signed short maxStackDepth{0};

    explicit CodeBuilder(const Lexer &lexer, CodeKind kind, unsigned char localVarNum) :
            lexer(lexer), kind(kind), localVarNum(localVarNum) {}

    CodeKind getCodeKind() const {
        return this->kind;
    }

    /**
     * after build, remove allocated buffer.
     */
    CompiledCode build(const std::string &name);
};

class ModuleCommon {
private:
    DSValue scriptName;
    DSValue scriptDir;

public:
    ModuleCommon() = default;

    /**
     *
     * @param name
     * @param scriptDir
     */
    ModuleCommon(const std::string &name, const char *scriptDir) :
            scriptName(DSValue::createStr(name)),
            scriptDir(DSValue::createStr(scriptDir)) {}

    DSValue getScriptName() const {
        return this->scriptName;
    }

    DSValue getScriptDir() const {
        return this->scriptDir;
    }
};

class ByteCodeGenerator : protected NodeVisitor {
private:
    TypePool &typePool;

    bool assertion;

    const MethodHandle *handle_STR{nullptr};

    std::vector<CodeBuilder> builders;

    std::vector<ModuleCommon> commons;

    CodeGenError error;

public:
    ByteCodeGenerator(TypePool &pool, bool assertion) : typePool(pool), assertion(assertion) { }

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

    bool inUDC() const {
        return this->curBuilder().getCodeKind() == CodeKind::USER_DEFINED_CMD;
    }

    bool inFunc() const {
        return this->curBuilder().getCodeKind() == CodeKind::FUNCTION;
    }

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
        assert(op == OpCode::RECLAIM_LOCAL || op == OpCode::PUSH_STR2);
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
        assert(op == OpCode::CALL_FUNC || op == OpCode::CALL_METHOD ||
                op == OpCode::CALL_BUILTIN2 || op == OpCode::ADD_GLOBBING);
        this->curBuilder().append8(static_cast<unsigned char>(op));
        this->curBuilder().append8(paramSize);

        int size = static_cast<int>(paramSize) + restSize;
        this->curBuilder().stackDepthCount -= static_cast<short>(size);
    }

    void emitFuncCallIns(unsigned char paramSize, bool hasRet) {
        this->emitValIns(OpCode::CALL_FUNC, paramSize, hasRet ? 0 : 1);
    }

    void emitNativeCallIns(unsigned char paramSize, unsigned short index, bool hasRet) {
        assert(index <= UINT8_MAX);
        this->emitValIns(OpCode::CALL_BUILTIN2, paramSize, hasRet ? -1 : 0);
        this->curBuilder().append8(index);
    }

    /**
     *
     * @param paramSize
     * not include receiver
     * @param handle
     * target method
     */
    void emitMethodCallIns(unsigned int paramSize, const MethodHandle &handle);

    void emitGlobIns(unsigned char paramSize, bool tilde) {
        this->emitValIns(OpCode::ADD_GLOBBING, paramSize, 1);
        this->curBuilder().append8(tilde ? 1 : 0);
    }

    /**
     * write instruction having type. (ex. PRINT).
     */
    void emitTypeIns(OpCode op, const DSType &type) {
        assert(isTypeOp(op));
        this->emit3byteIns(op, type.typeId());
    }

    unsigned int currentCodeOffset() const {
        return this->curBuilder().codeBuffer.size();
    }

    unsigned int emitConstant(DSValue &&value);

    void emitLdcIns(const DSValue &value) {
        this->emitLdcIns(DSValue(value));
    }

    void emitLdcIns(DSValue &&value);
    void emitToString();
    void emitBranchIns(OpCode op, const Label &label);

    void emitBranchIns(const Label &label) {
        this->emitBranchIns(OpCode::BRANCH, label);
    }

    void emitForkIns(ForkKind kind, const Label &label);

    void emitJumpIns(const Label &label);
    void markLabel(Label &label);

    void pushLoopLabels(Label breakLabel, Label continueLabel, Label breakWithValueLabel);

    void popLoopLabels() {
        this->curBuilder().loopLabels.pop_back();
    }

    const LoopState &peekLoopLabels() const {
        return this->curBuilder().loopLabels.back();
    }

    /**
     *
     * @param node
     * not empty block
     * @return
     */
    bool needReclaim(const BlockNode &node) const {
        if(node.getNodes().empty()) {
            return false;
        }

        if((*node.getNodes().rbegin())->getType().isNothingType()) {
            return false;
        }

        if(node.getVarSize() == 0) {
            return false;
        }

        // when toplevel block of function or udc
        if((this->inFunc() || this->inUDC()) && this->curBuilder().localVars.empty()) {
            return false;
        }
        return true;
    }

    template <typename Func>
    void generateBlock(unsigned short localOffset, unsigned short localSize, bool needReclaim, Func func) {
        this->curBuilder().localVars.emplace_back(localOffset, localSize);

        func();

        if(needReclaim) {
            this->emit2byteIns(OpCode::RECLAIM_LOCAL, localOffset, localSize);
        }
        this->curBuilder().localVars.pop_back();
    }

    /**
     * for line number
     */
    void emitSourcePos(unsigned int pos);

    /**
     * begin and end have already been marked.
     */
    void catchException(const Label &begin, const Label &end, const DSType &type,
                        unsigned short localOffset = 0, unsigned short localSize = 0);
    void enterFinally(const Label &label);
    void enterMultiFinally();
    void generateCmdArg(CmdArgNode &node);
    void generatePipeline(PipelineNode &node, ForkKind forkKind);
    void emitPipelineIns(const std::vector<Label> &labels, bool lastPipe, ForkKind forkKind);

    void generateConcat(Node &node, bool fragment = false);

    void generateBreakContinue(JumpNode &node);

    void generateMapCase(CaseNode &node);
    void generateCaseLabels(const ArmNode &node, MapObject &obj);
    void generateIfElseCase(CaseNode &node);
    void generateIfElseArm(ArmNode &node, const MethodHandle &eqHandle,
            const MethodHandle &matchHandle, const Label &mergeLabel);

    void initToplevelCodeBuilder(const Lexer &lex, unsigned short localVarNum) {
        assert(lex.getScriptDir());
        this->commons.emplace_back(lex.getSourceName(), lex.getScriptDir());
        this->initCodeBuilder(CodeKind::TOPLEVEL, lex, localVarNum);
    }

    void initCodeBuilder(CodeKind kind, unsigned short localVarNum) {
        auto &lex = this->builders.back().lexer;
        this->initCodeBuilder(kind, lex, localVarNum);
    }

    void initCodeBuilder(CodeKind kind, const Lexer &lex, unsigned short localVarNum) {
        this->builders.emplace_back(lex, kind, localVarNum);
        this->curBuilder().constBuffer.push_back(this->commons.back().getScriptName());
        this->curBuilder().constBuffer.push_back(this->commons.back().getScriptDir());
    }

    CompiledCode finalizeCodeBuilder(const std::string &name) {
        auto code = this->curBuilder().build(name);
        this->builders.pop_back();
        return code;
    }

    void reportErrorImpl(Token token, const char *kind,
                                const char *fmt, ...) __attribute__ ((format(printf, 4, 5)));

    template <typename T, typename ... Arg, typename = base_of_t<T, CGError>>
    void reportError(const Node &node, Arg && ...arg) {
        return this->reportErrorImpl(node.getToken(), T::kind, T::value, std::forward<Arg>(arg)...);
    }

    template <typename T, typename ... Arg, typename = base_of_t<T, CGError>>
    void reportError(Token token, Arg && ...arg) {
        return this->reportErrorImpl(token, T::kind, T::value, std::forward<Arg>(arg)...);
    }

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
    void visitRedirNode(RedirNode &node) override;
    void visitWildCardNode(WildCardNode &node) override;
    void visitPipelineNode(PipelineNode &node) override;
    void visitWithNode(WithNode &node) override;
    void visitForkNode(ForkNode &node) override;
    void visitAssertNode(AssertNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitTypeAliasNode(TypeAliasNode &node) override;
    void visitLoopNode(LoopNode &node) override;
    void visitIfNode(IfNode &node) override;
    void visitCaseNode(CaseNode &node) override;
    void visitArmNode(ArmNode &node) override;
    void visitJumpNode(JumpNode &node) override;
    void visitCatchNode(CatchNode &node) override;
    void visitTryNode(TryNode &node) override;
    void visitVarDeclNode(VarDeclNode &node) override;
    void visitAssignNode(AssignNode &node) override;
    void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
    void visitEnvCtxNode(EnvCtxNode &node) override;
    void visitFunctionNode(FunctionNode &node) override;
    void visitInterfaceNode(InterfaceNode &node) override;
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
    void visitSourceNode(SourceNode &node) override;
    void visitSourceListNode(SourceListNode &node) override;
    void visitCodeCompNode(CodeCompNode &node) override;
    void visitEmptyNode(EmptyNode &node) override;

public:
    bool hasError() const {
        return static_cast<bool>(this->error);
    }

    const CodeGenError &getError() const {
        return this->error;
    }

    void initialize(const Lexer &lexer) {
        this->initToplevelCodeBuilder(lexer, 0);
    }

    bool generate(Node *node) {
        this->visit(*node);
        return !this->hasError();
    }

    CompiledCode finalize(unsigned int maxVarIndex);

    void enterModule(const Lexer &lexer) {
        this->initToplevelCodeBuilder(lexer, 0);
    }

    bool exitModule(const SourceNode &node);
};

class ByteCodeDumper {
private:
    FILE *fp;

    const TypePool &typePool;
    unsigned int maxGVarIndex;

    std::vector<std::reference_wrapper<const CompiledCode>> mods;
    std::vector<std::reference_wrapper<const CompiledCode>> funcs;

public:
    ByteCodeDumper(FILE *fp, const TypePool &pool, unsigned int maxGVarIndex) :
            fp(fp), typePool(pool), maxGVarIndex(maxGVarIndex) {}

    void operator()(const CompiledCode &code);

private:
    void dumpModule(const CompiledCode &code);

    void dumpCode(const CompiledCode &c);
};

} // namespace ydsh

#endif //YDSH_CODEGEN_H
