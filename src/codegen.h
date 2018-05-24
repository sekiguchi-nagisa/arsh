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

#include "node.h"
#include "object.h"
#include "opcode.h"
#include "handle.h"
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

        assert(this->begin->getIndex() > 0);
        assert(this->end->getIndex() > 0);
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
    SourceInfoPtr srcInfo;

    std::vector<DSValue> constBuffer;
    FlexBuffer<SourcePosEntry> sourcePosEntries;
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

    explicit CodeBuilder(const SourceInfoPtr &info) : srcInfo(info) {}

    CodeKind getCodeKind() const {
        return static_cast<CodeKind>(this->codeBuffer[0]);
    }
};

class ByteCodeGenerator : protected NodeVisitor {
private:
    SymbolTable &symbolTable;

    bool assertion;

    MethodHandle *handle_STR{nullptr};

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
        return this->curBuilder().getCodeKind() == CodeKind::USER_DEFINED_CMD;;
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

    void emit4byteIns(OpCode op, unsigned int v) {
        ASSERT_BYTE_SIZE(op, 4);
        this->emitIns(op);
        this->curBuilder().append32(v);
    }

    void emitCallIns(OpCode op, unsigned short paramSize) {
        assert(op == OpCode::CALL_FUNC || op == OpCode::CALL_INIT
               || op == OpCode::CALL_METHOD);
        this->curBuilder().stackDepthCount -= paramSize + 1;
        this->emitIns(op);
        this->curBuilder().append16(paramSize);
    }

    void emitCallIns(OpCode op, unsigned short paramSize, unsigned short index) {
        assert(op == OpCode::CALL_METHOD);
        this->emitCallIns(op, paramSize);
        this->curBuilder().append16(index);
    }

    /**
     * write instruction having type. (ex. PRINT).
     */
    void emitTypeIns(OpCode op, const DSType &type) {
        this->emit4byteIns(op, type.getTypeID());
    }

    unsigned int emitConstant(DSValue &&value);

    void emitLdcIns(const DSValue &value) {
        this->emitLdcIns(DSValue(value));
    }

    void emitLdcIns(DSValue &&value);
    void emitDescriptorIns(OpCode op, std::string &&desc);
    void generateToString();
    void emitNumCastIns(const DSType &beforeType, const DSType &afterType);
    void emitBranchIns(OpCode op, const Label &label);

    void emitBranchIns(const Label &label) {
        this->emitBranchIns(OpCode::BRANCH, label);
    }

    void emitForkIns(ForkKind kind, const Label &label);

    void emitJumpIns(const Label &label);
    void markLabel(Label &label);

    void pushLoopLabels(Label breakLabel, Label continueLabel, Label breakWithValueLabel);
    void popLoopLabels();
    const LoopState &peekLoopLabels();

    /**
     *
     * @param node
     * not empty block
     * @return
     */
    bool needReclaim(const BlockNode &node) {
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
    void emitPipelineIns(const std::vector<Label> &labels);
    void generateStringExpr(StringExprNode &node, bool fragment);
    void generateBreakContinue(JumpNode &node);

    void initCodeBuilder(CodeKind kind, unsigned short localVarNum) {
        auto info = this->builders.back().srcInfo;
        this->initCodeBuilder(kind, info, localVarNum);
    }

    void initCodeBuilder(CodeKind kind, const SourceInfoPtr &srcInfo, unsigned short localVarNum);

    CompiledCode finalizeCodeBuilder(const std::string &name);

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
    void visitCmdNode(CmdNode &node) override;
    void visitCmdArgNode(CmdArgNode &node) override;
    void visitRedirNode(RedirNode &node) override;
    void visitPipelineNode(PipelineNode &node) override;
    void visitWithNode(WithNode &node) override;
    void visitForkNode(ForkNode &node) override;
    void visitAssertNode(AssertNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitTypeAliasNode(TypeAliasNode &node) override;
    void visitLoopNode(LoopNode &node) override;
    void visitIfNode(IfNode &node) override;
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
    void initialize(const SourceInfoPtr &srcInfo) {
        this->initCodeBuilder(CodeKind::TOPLEVEL, srcInfo, 0);
    }

    void generate(Node *node) {
        this->visit(*node);
    }

    CompiledCode finalize(unsigned char maxLocalSize) {
        this->curBuilder().emit8(5, maxLocalSize);
        this->emitIns(OpCode::HALT);
        return this->finalizeCodeBuilder("");
    }

    void enterModule(const SourceInfoPtr &srcInfo) {
        this->initCodeBuilder(CodeKind::TOPLEVEL, srcInfo, 0);
    }

    void exitModule(SourceNode &node);
};

/**
 * for debugging
 */
void dumpCode(FILE *fp, DSState &ctx, const CompiledCode &c);

} // namespace ydsh

#endif //YDSH_CODEGEN_H
