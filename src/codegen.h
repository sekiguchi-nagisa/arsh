/*
 * Copyright (C) 2016-2017 Nagisa Sekiguchi
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

#include "node.h"
#include "object.h"
#include "opcode.h"
#include "handle.h"
#include "misc/resource.hpp"

namespace ydsh {

class Label {
private:
    unsigned int refCount;

    /**
     * indicate labeled address
     */
    unsigned int index;

public:
    Label() : refCount(0), index(0) { }

private:
    ~Label() = default;

public:
    bool operator==(const Label &l) const noexcept {
        return reinterpret_cast<unsigned long>(this) == reinterpret_cast<unsigned long>(&l);
    }

    void setIndex(unsigned int index) {
        this->index = index;
    }

    unsigned int getIndex() const {
        return this->index;
    }

    void destroy() const {
        delete this;
    }

    friend void intrusivePtr_addRef(Label *l) noexcept {
        if(l != nullptr) {
            l->refCount++;
        }
    }

    friend void intrusivePtr_release(Label *l) noexcept {
        if(l != nullptr && --l->refCount == 0) {
            l->destroy();
        }
    }
};

using CodeBuffer = FlexBuffer<unsigned char>;

template <bool T>
struct ByteCodeWriter {
    static_assert(true, "not allow instantiation");

    struct Compare {
        bool operator()(const IntrusivePtr<Label> &x, const IntrusivePtr<Label> &y) const noexcept {
            return *x == *y;
        }
    };

    struct GenHash {
        std::size_t operator()(const IntrusivePtr<Label> &l) const noexcept {
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

    std::unordered_map<IntrusivePtr<Label>, std::vector<LabelTarget>, GenHash, Compare> labelMap;

    CodeBuffer codeBuffer;

    void write8(unsigned int index, unsigned char b) noexcept {
        ydsh::write8(this->codeBuffer.begin() + index, b);
    }

    void write16(unsigned int index, unsigned short b) noexcept {
        ydsh::write16(this->codeBuffer.begin() + index, b);
    }

    void write32(unsigned int index, unsigned int b) noexcept {
        ydsh::write32(this->codeBuffer.begin() + index, b);
    }

    void write64(unsigned int index, unsigned long b) noexcept {
        ydsh::write64(this->codeBuffer.begin() + index, b);
    }

    void write(unsigned int index, unsigned long b) noexcept {
        if(b <= UINT8_MAX) {
            this->write8(index, static_cast<unsigned char>(b));
        } else if(b <= UINT16_MAX) {
            this->write16(index, static_cast<unsigned short>(b));
        } else if(b <= UINT32_MAX) {
            this->write32(index, static_cast<unsigned int>(b));
        } else {
            this->write64(index, b);
        }
    }

    void append8(unsigned char b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(1, 0);
        this->write8(index, b);
    }

    void append16(unsigned short b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(2, 0);
        this->write16(index, b);
    }

    void append32(unsigned int b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(4, 0);
        this->write32(index, b);
    }

    void append64(unsigned long b) {
        const unsigned int index = this->codeBuffer.size();
        this->codeBuffer.assign(8, 0);
        this->write64(index, b);
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

    void markLabel(unsigned int index,  IntrusivePtr<Label> &label) {
        label->setIndex(index);
        this->labelMap[label];
    }

    void writeLabel(unsigned int index, const IntrusivePtr<Label> &label,
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
                    this->write8(targetIndex, offset);
                    break;
                case LabelTarget::_16:
                    if(offset > UINT16_MAX) {
                        fatal("offset is greater than UINT16_MAX\n");
                    }
                    this->write16(targetIndex, offset);
                    break;
                case LabelTarget::_32:
                    this->write32(targetIndex, offset);
                    break;
                }
            }
        }
        this->labelMap.clear();
    }
};

class CatchBuilder {
private:
    IntrusivePtr<Label> begin;  // inclusive
    IntrusivePtr<Label> end;    // exclusive
    const DSType *type;
    unsigned int address;   // start index of catch block.

public:
    CatchBuilder() : begin(nullptr), end(nullptr), type(nullptr), address(0) { }
    CatchBuilder(const IntrusivePtr<Label> &begin, const IntrusivePtr<Label> &end, const DSType &type, unsigned int address) :
            begin(begin), end(end), type(&type), address(address) {}

    ~CatchBuilder() = default;

    ExceptionEntry toEntry() const;
};

class ByteCodeGenerator : protected NodeVisitor {
private:
    TypePool &pool;

    bool assertion;

    /**
     * if true, within user-defined command definition.
     */
    bool inUDC;

    MethodHandle *handle_STR;

    struct CodeBuilder : public ByteCodeWriter<true> {
        std::vector<DSValue> constBuffer;
        std::vector<SourcePosEntry> sourcePosEntries;
        std::vector<CatchBuilder> catchBuilders;

        /**
         * first is break label, second is continue label
         */
        std::vector<std::pair<IntrusivePtr<Label>, IntrusivePtr<Label>>> loopLabels;

        std::vector<IntrusivePtr<Label>> finallyLabels;
    };

    std::vector<CodeBuilder *> builders;

public:
    ByteCodeGenerator(TypePool &pool, bool assertion) :
            pool(pool), assertion(assertion), inUDC(false),
            handle_STR(nullptr), builders() { }

    ~ByteCodeGenerator();

private:
    CodeBuilder &curBuilder() noexcept {
        assert(!this->builders.empty());
        return *this->builders.back();
    }

    /**
     * not check byte size. not directly use it.
     */
    void writeIns(OpCode op);

    void write0byteIns(OpCode op);
    void write1byteIns(OpCode op, unsigned char v);
    void write2byteIns(OpCode op, unsigned short v);
    void write4byteIns(OpCode op, unsigned int v);
    void write8byteIns(OpCode op, unsigned long v);

    /**
     * write instruction having type. (ex. PRINT).
     */
    void writeTypeIns(OpCode op, const DSType &type);

    unsigned short writeConstant(DSValue &&value);
    void writeLdcIns(const DSValue &value);
    void writeLdcIns(DSValue &&value);
    void writeDescriptorIns(OpCode op, std::string &&desc);
    void writeMethodCallIns(OpCode op, unsigned short index, unsigned short paramSize);
    void writeToString();
    void writeNumCastIns(unsigned short op, const DSType &type);
    void writeBranchIns(OpCode op, const IntrusivePtr<Label> &label);
    void writeBranchIns(const IntrusivePtr<Label> &label);
    void writeJumpIns(const IntrusivePtr<Label> &label);
    void markLabel(IntrusivePtr<Label> &label);

    /**
     * for line number
     */
    void writeSourcePos(unsigned int pos);

    /**
     * begin and end have already been marked.
     */
    void catchException(const IntrusivePtr<Label> &begin, const IntrusivePtr<Label> &end, const DSType &type);
    void enterFinally();
    void writeCaptureIns(bool isStr, const IntrusivePtr<Label> &label);
    void generateCmdArg(CmdArgNode &node);
    void writePipelineIns(const std::vector<IntrusivePtr<Label>> &labels);
    void generateStringExpr(StringExprNode &node, bool fragment);

    void initCodeBuilder(CodeKind kind, unsigned short localVarNum);
    void initToplevelCodeBuilder(const RootNode &node);
    CompiledCode finalizeCodeBuilder(const CallableNode &node);

    // visitor api
    void visit(Node &node) override;
    void visitBaseTypeNode(BaseTypeNode &typeNode) override;
    void visitReifiedTypeNode(ReifiedTypeNode &typeNode) override;
    void visitFuncTypeNode(FuncTypeNode &typeNode) override;
    void visitDBusIfaceTypeNode(DBusIfaceTypeNode &typeNode) override;
    void visitReturnTypeNode(ReturnTypeNode &typeNode) override;
    void visitTypeOfNode(TypeOfNode &typeNode) override;
    void visitIntValueNode(IntValueNode &node) override;
    void visitLongValueNode(LongValueNode &node) override;
    void visitFloatValueNode(FloatValueNode &node) override;
    void visitStringValueNode(StringValueNode &node) override;
    void visitObjectPathNode(ObjectPathNode &node) override;
    void visitStringExprNode(StringExprNode &node) override;
    void visitArrayNode(ArrayNode &node) override;
    void visitMapNode(MapNode &node) override;
    void visitTupleNode(TupleNode &node) override;
    void visitVarNode(VarNode &node) override;
    void visitAccessNode(AccessNode &node) override;
    void visitCastNode(CastNode &node) override;
    void visitInstanceOfNode(InstanceOfNode &node) override;
    void visitPrintNode(PrintNode &node) override;
    void visitUnaryOpNode(UnaryOpNode &node) override;
    void visitBinaryOpNode(BinaryOpNode &node) override;
    void visitApplyNode(ApplyNode &node) override;
    void visitMethodCallNode(MethodCallNode &node) override;
    void visitNewNode(NewNode &node) override;
    void visitCondOpNode(CondOpNode &node) override;
    void visitCmdNode(CmdNode &node) override;
    void visitCmdArgNode(CmdArgNode &node) override;
    void visitRedirNode(RedirNode &node) override;
    void visitTildeNode(TildeNode &node) override;
    void visitPipedCmdNode(PipedCmdNode &node) override;
    void visitSubstitutionNode(SubstitutionNode &node) override;
    void visitAssertNode(AssertNode &node) override;
    void visitBlockNode(BlockNode &node) override;
    void visitBreakNode(BreakNode &node) override;
    void visitContinueNode(ContinueNode &node) override;
    void visitExportEnvNode(ExportEnvNode &node) override;
    void visitImportEnvNode(ImportEnvNode &node) override;
    void visitTypeAliasNode(TypeAliasNode &node) override;
    void visitForNode(ForNode &node) override;
    void visitWhileNode(WhileNode &node) override;
    void visitDoWhileNode(DoWhileNode &node) override;
    void visitIfNode(IfNode &node) override;
    void visitReturnNode(ReturnNode &node) override;
    void visitThrowNode(ThrowNode &node) override;
    void visitCatchNode(CatchNode &node) override;
    void visitTryNode(TryNode &node) override;
    void visitVarDeclNode(VarDeclNode &node) override;
    void visitAssignNode(AssignNode &node) override;
    void visitElementSelfAssignNode(ElementSelfAssignNode &node) override;
    void visitFunctionNode(FunctionNode &node) override;
    void visitInterfaceNode(InterfaceNode &node) override;
    void visitUserDefinedCmdNode(UserDefinedCmdNode &node) override;
    void visitEmptyNode(EmptyNode &node) override;
    void visitRootNode(RootNode &node) override;

public:
    /**
     * entry point of code generation.
     */
    CompiledCode generateToplevel(RootNode &node);
};

/**
 * for debugging
 */
void dumpCode(std::ostream &stream, DSState &ctx, const CompiledCode &c);

} // namespace ydsh

#endif //YDSH__CODEGEN_H
