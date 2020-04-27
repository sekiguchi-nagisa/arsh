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
#include "misc/resource.hpp"

#define ASSERT_BYTE_SIZE(op, size) assert(getByteSize(op) == (size))

namespace ydsh {

class LabelImpl : public RefCount<LabelImpl> {
private:
    /**
     * indicate labeled address
     */
    unsigned int index{0};

    ~LabelImpl() = default;

    friend struct RefCountOp<LabelImpl>;

public:
    bool operator==(const LabelImpl &l) const noexcept {
        return reinterpret_cast<unsigned long>(this) == reinterpret_cast<unsigned long>(&l);
    }

    void setIndex(unsigned int index) {
        this->index = index;
    }

    unsigned int getIndex() const {
        return this->index;
    }
};

using Label = IntrusivePtr<LabelImpl>;

inline Label makeLabel() {
    return Label::create();
}

using CodeBuffer = FlexBuffer<unsigned char>;

template <bool T>
struct CodeEmitter {
    static_assert(true, "not allow instantiation");

    struct Compare {
        bool operator()(const Label &x, const Label &y) const noexcept {
            return *x == *y;
        }
    };

    struct GenHash {
        std::size_t operator()(const Label &l) const noexcept {
            return std::hash<unsigned long>()(reinterpret_cast<unsigned long>(l.get()));
        }
    };

    struct LabelTarget {
        unsigned int targetIndex;
        unsigned int baseIndex;
        enum OffsetLen {
            _8, _16, _32,
        };

        OffsetLen offsetLen;
    };

    std::unordered_map<Label, std::vector<LabelTarget>, GenHash, Compare> labelMap;

    CodeBuffer codeBuffer;

    void emit8(unsigned int index, unsigned char b) noexcept {
        ydsh::write8(this->codeBuffer.begin() + index, b);
    }

    void emit16(unsigned int index, unsigned short b) noexcept {
        ydsh::write16(this->codeBuffer.begin() + index, b);
    }

    void emit24(unsigned int index, unsigned int b) noexcept {
        ydsh::write24(this->codeBuffer.begin() + index, b);
    }

    void emit32(unsigned int index, unsigned int b) noexcept {
        ydsh::write32(this->codeBuffer.begin() + index, b);
    }

    void emit64(unsigned int index, unsigned long b) noexcept {
        ydsh::write64(this->codeBuffer.begin() + index, b);
    }

    void emit(unsigned int index, unsigned long b) noexcept {
        if(b <= UINT8_MAX) {
            this->emit8(index, static_cast<unsigned char>(b));
        } else if(b <= UINT16_MAX) {
            this->emit16(index, static_cast<unsigned short>(b));
        } else if(b <= UINT32_MAX) {
            this->emit32(index, static_cast<unsigned int>(b));
        } else {
            this->emit64(index, b);
        }
    }

    void append8(unsigned char b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(1, 0);
        this->emit8(index, b);
    }

    void append16(unsigned short b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(2, 0);
        this->emit16(index, b);
    }

    void append24(unsigned int b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(3, 0);
        this->emit24(index, b);
    }

    void append32(unsigned int b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(4, 0);
        this->emit32(index, b);
    }

    void append64(unsigned long b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(8, 0);
        this->emit64(index, b);
    }

    void append(unsigned long b) {
        if(b <= UINT8_MAX) {
            this->append8(static_cast<unsigned char>(b));
        } else if(b <= UINT16_MAX) {
            this->append16(static_cast<unsigned short>(b));
        } else if(b <= UINT32_MAX) {
            this->append32(static_cast<unsigned int>(b));
        } else {
            this->append64(b);
        }
    }

    void markLabel(unsigned int index, Label &label) {
        label->setIndex(index);
        this->labelMap[label];
    }

    void writeLabel(unsigned int index, const Label &label,
                    unsigned int baseIndex, typename LabelTarget::OffsetLen len) {
        this->labelMap[label].push_back({index, baseIndex, len});
    }

    void finalize() {
        // write labeled index.
        for(auto &e : this->labelMap) {
            const unsigned int location = e.first->getIndex();
            for(auto &u : e.second) {
                const unsigned int targetIndex = u.targetIndex;
                if(u.baseIndex > location) {
                    fatal("base index: %u, label location: %u\n", u.baseIndex, location);
                }
                const unsigned int offset = location - u.baseIndex;
                switch(u.offsetLen) {
                case LabelTarget::_8:
                    if(offset > UINT8_MAX) {
                        fatal("offset is greater than UINT8_MAX\n");
                    }
                    this->emit8(targetIndex, offset);
                    break;
                case LabelTarget::_16:
                    if(offset > UINT16_MAX) {
                        fatal("offset is greater than UINT16_MAX\n");
                    }
                    this->emit16(targetIndex, offset);
                    break;
                case LabelTarget::_32:
                    this->emit32(targetIndex, offset);
                    break;
                }
            }
        }
        this->labelMap.clear();
    }
};

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

//        assert(this->begin->getIndex() > -1);
//        assert(this->end->getIndex() > -1);
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
    SourceInfo srcInfo;

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

    explicit CodeBuilder(SourceInfo info, CodeKind kind, unsigned char localVarNum) :
            srcInfo(std::move(info)), kind(kind), localVarNum(localVarNum) {}

    CodeKind getCodeKind() const {
        return this->kind;
    }

    /**
     * after build, remove allocated buffer.
     */
    CompiledCode build(const std::string &name);
};

class ByteCodeGenerator : protected NodeVisitor {
private:
    SymbolTable &symbolTable;

    bool assertion;

    const MethodHandle *handle_STR{nullptr};

    std::vector<CodeBuilder> builders;

public:
    ByteCodeGenerator(SymbolTable &symbolTable, bool assertion) :
            symbolTable(symbolTable), assertion(assertion) { }

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
        assert(op == OpCode::RECLAIM_LOCAL);
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
                op == OpCode::CALL_NATIVE2 || op == OpCode::ADD_GLOBBING);
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
        this->emitValIns(OpCode::CALL_NATIVE2, paramSize, hasRet ? -1 : 0);
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
        this->emit3byteIns(op, type.getTypeID());
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
    void enterFinally();
    void generateCmdArg(CmdArgNode &node);
    void emitPipelineIns(const std::vector<Label> &labels, bool lastPipe);

    void generateConcat(Node &node, bool fragment = false);

    void generateBreakContinue(JumpNode &node);

    void generateMapCase(CaseNode &node);
    void generateCaseLabels(const ArmNode &node, MapObject &obj);
    void generateIfElseCase(CaseNode &node);
    void generateIfElseArm(ArmNode &node, const MethodHandle &eqHandle,
            const MethodHandle &matchHandle, const Label &mergeLabel);

    void initCodeBuilder(CodeKind kind, unsigned short localVarNum) {
        auto info = this->builders.back().srcInfo;
        this->initCodeBuilder(kind, info, localVarNum);
    }

    void initCodeBuilder(CodeKind kind, const SourceInfo &srcInfo, unsigned short localVarNum) {
        this->builders.emplace_back(srcInfo, kind, localVarNum);
    }

    CompiledCode finalizeCodeBuilder(const std::string &name) {
        auto code = this->curBuilder().build(name);
        this->builders.pop_back();
        return code;
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
    void visitFunctionNode(FunctionNode &node) override;
    void visitInterfaceNode(InterfaceNode &node) override;
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
    void visitSourceNode(SourceNode &node) override;
    void visitEmptyNode(EmptyNode &node) override;

public:
    void initialize(const SourceInfo &srcInfo) {
        this->initCodeBuilder(CodeKind::TOPLEVEL, srcInfo, 0);
    }

    void generate(Node *node) {
        this->visit(*node);
    }

    CompiledCode finalize();

    void enterModule(const SourceInfo &srcInfo) {
        this->initCodeBuilder(CodeKind::TOPLEVEL, srcInfo, 0);
    }

    void exitModule(SourceNode &node);
};

class ByteCodeDumper {
private:
    FILE *fp;

    const SymbolTable &symbolTable;

    std::vector<std::reference_wrapper<const CompiledCode>> mods;
    std::vector<std::reference_wrapper<const CompiledCode>> funcs;

public:
    ByteCodeDumper(FILE *fp, const SymbolTable &symbolTable) :
            fp(fp), symbolTable(symbolTable) {}

    void operator()(const CompiledCode &code);

private:
    void dumpModule(const CompiledCode &code);

    void dumpCode(const CompiledCode &c);
};

} // namespace ydsh

#endif //YDSH_CODEGEN_H
