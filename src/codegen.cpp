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

#include <iomanip>

#include "codegen.h"
#include "symbol.h"
#include "core.h"

#define ASSERT_BYTE_SIZE(op, size) assert(getByteSize(op) == (size))

namespace ydsh {

int getByteSize(OpCode code) {
    char table[] = {
#define GEN_BYTE_SIZE(CODE, N) N,
            OPCODE_LIST(GEN_BYTE_SIZE)
#undef GEN_BYTE_SIZE
    };
    return table[static_cast<unsigned char>(code)];
}

bool isTypeOp(OpCode code) {
    switch(code) {
    case OpCode::PRINT:
    case OpCode::INSTANCE_OF:
    case OpCode::CHECK_CAST:
    case OpCode::NEW_ARRAY:
    case OpCode::NEW_MAP:
    case OpCode::NEW_TUPLE:
    case OpCode::NEW:
        ASSERT_BYTE_SIZE(code, 8);
        return true;
    default:
        return false;
    }
}

// ##########################
// ##     CatchBuilder     ##
// ##########################

ExceptionEntry CatchBuilder::toEntry() const {
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


// ###############################
// ##     ByteCodeGenerator     ##
// ###############################

ByteCodeGenerator::~ByteCodeGenerator() {
    for(auto &e : this->builders) {
        delete e;
    }
}

void ByteCodeGenerator::emitIns(OpCode op) {
    this->curBuilder().append8(static_cast<unsigned char>(op));
}

void ByteCodeGenerator::emit0byteIns(OpCode op) {
    ASSERT_BYTE_SIZE(op, 0);
    this->emitIns(op);
}

void ByteCodeGenerator::emit1byteIns(OpCode op, unsigned char v) {
    ASSERT_BYTE_SIZE(op, 1);
    this->emitIns(op);
    this->curBuilder().append8(v);
}

void ByteCodeGenerator::emit2byteIns(OpCode op, unsigned short v) {
    ASSERT_BYTE_SIZE(op, 2);
    this->emitIns(op);
    this->curBuilder().append16(v);
}

void ByteCodeGenerator::emit4byteIns(OpCode op, unsigned int v) {
    ASSERT_BYTE_SIZE(op, 4);
    this->emitIns(op);
    this->curBuilder().append32(v);
}

void ByteCodeGenerator::emit4byteIns(OpCode op, unsigned short v1, unsigned short v2) {
    assert(op == OpCode::CALL_METHOD || op == OpCode::RECLAIM_LOCAL);
    ASSERT_BYTE_SIZE(op, 4);
    this->emitIns(op);
    this->curBuilder().append16(v1);
    this->curBuilder().append16(v2);
}

void ByteCodeGenerator::emit8byteIns(OpCode op, unsigned long v) {
    ASSERT_BYTE_SIZE(op, 8);
    this->emitIns(op);
    this->curBuilder().append64(v);
}

void ByteCodeGenerator::emitTypeIns(OpCode op, const DSType &type) {
    assert(isTypeOp(op));
    this->emit8byteIns(op, reinterpret_cast<unsigned long>(&type));
}

unsigned short ByteCodeGenerator::emitConstant(DSValue &&value) {
    this->curBuilder().constBuffer.push_back(std::move(value));
    unsigned int index = this->curBuilder().constBuffer.size() - 1;
    if(index > UINT16_MAX) {
        fatal("const pool index must be 16bit");
    }
    return index;
}

void ByteCodeGenerator::emitLdcIns(const DSValue &value) {
    this->emitLdcIns(DSValue(value));
}

void ByteCodeGenerator::emitLdcIns(DSValue &&value) {
    unsigned short index = this->emitConstant(std::move(value));
    if(index <= UINT8_MAX) {
        this->emit1byteIns(OpCode::LOAD_CONST, index);
    } else {
        this->emit2byteIns(OpCode::LOAD_CONST_W, index);
    }
}

void ByteCodeGenerator::emitDescriptorIns(OpCode op, std::string &&desc) {
    unsigned short index = this->emitConstant(
            DSValue::create<String_Object>(this->pool.getStringType(), std::move(desc)));
    this->emit2byteIns(op, index);
}

void ByteCodeGenerator::generateToString() {
    if(this->handle_STR == nullptr) {
        this->handle_STR = this->pool.getAnyType().lookupMethodHandle(this->pool, std::string(OP_STR));
    }

    this->emit4byteIns(OpCode::CALL_METHOD, this->handle_STR->getMethodIndex(), 0);
}

static constexpr unsigned short toShort(OpCode op) {
    return static_cast<unsigned char>(op);
}

static constexpr unsigned short toShort(OpCode op1, OpCode op2) {
    return toShort(op1) | toShort(op2) << 8;
}

void ByteCodeGenerator::emitNumCastIns(const DSType &beforeType, const DSType &afterType) {
    const int beforeIndex = this->pool.getNumTypeIndex(beforeType);
    const int afterIndex = this->pool.getNumTypeIndex(afterType);

    assert(beforeIndex > -1 && beforeIndex < 8);
    assert(afterIndex > -1 && afterIndex < 8);

#define _1(L) toShort(OpCode::L)
#define _2(L, R) toShort(OpCode::L, OpCode::R)

    const unsigned short table[8][8] = {
            {_1(NOP),              _1(COPY_INT),        _1(COPY_INT),        _1(COPY_INT), _1(COPY_INT), _1(NEW_LONG),   _1(NEW_LONG),   _1(U32_TO_D)},
            {_1(TO_BYTE),          _1(NOP),             _1(TO_U16),          _1(COPY_INT), _1(COPY_INT), _1(I_NEW_LONG), _1(I_NEW_LONG), _1(I32_TO_D)},
            {_1(TO_BYTE),          _1(TO_I16),          _1(NOP),             _1(COPY_INT), _1(COPY_INT), _1(NEW_LONG),   _1(NEW_LONG),   _1(U32_TO_D)},
            {_1(TO_BYTE),          _1(TO_I16),          _1(TO_U16),          _1(NOP),      _1(COPY_INT), _1(I_NEW_LONG), _1(I_NEW_LONG), _1(I32_TO_D)},
            {_1(TO_BYTE),          _1(TO_I16),          _1(TO_U16),          _1(COPY_INT), _1(NOP),      _1(NEW_LONG),   _1(NEW_LONG),   _1(U32_TO_D)},
            {_2(NEW_INT,TO_BYTE),  _2(NEW_INT,TO_I16),  _2(NEW_INT,TO_U16),  _1(NEW_INT),  _1(NEW_INT),  _1(NOP),        _1(COPY_LONG),  _1(I64_TO_D)},
            {_2(NEW_INT,TO_BYTE),  _2(NEW_INT,TO_I16),  _2(NEW_INT,TO_U16),  _1(NEW_INT),  _1(NEW_INT),  _1(COPY_LONG),  _1(NOP),        _1(U64_TO_D)},
            {_2(D_TO_U32,TO_BYTE), _2(D_TO_I32,TO_I16), _2(D_TO_U32,TO_U16), _1(D_TO_I32), _1(D_TO_U32), _1(D_TO_I64),   _1(D_TO_U64),   _1(NOP)},
    };

#undef _1
#undef _2

    const unsigned short v = table[beforeIndex][afterIndex];
    for(unsigned int i = 0; i < 2; i++) {
        const unsigned short mask = 0xFF << (i * 8);
        OpCode op = static_cast<OpCode>((mask & v) >> (i * 8));
        if(op != OpCode::NOP) {
            unsigned int size = getByteSize(op);
            assert(size == 0 || size == 1);
            if(size) {
                this->emit1byteIns(op, afterIndex);
            } else {
                this->emit0byteIns(op);
            }
        }
    }
}

void ByteCodeGenerator::emitBranchIns(OpCode op, const IntrusivePtr<Label> &label) {
    const unsigned int index = this->curBuilder().codeBuffer.size();    //FIXME: check index range
    this->emit2byteIns(op, 0);
    this->curBuilder().writeLabel(index + 1, label, index, CodeEmitter<true>::LabelTarget::_16);
}

void ByteCodeGenerator::emitBranchIns(const IntrusivePtr<Label> &label) {
    this->emitBranchIns(OpCode::BRANCH, label);
}

void ByteCodeGenerator::emitJumpIns(const IntrusivePtr<Label> &label) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    this->emit4byteIns(OpCode::GOTO, 0);
    this->curBuilder().writeLabel(index + 1, label, 0, CodeEmitter<true>::LabelTarget::_32);
}

void ByteCodeGenerator::markLabel(IntrusivePtr<Label> &label) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    this->curBuilder().markLabel(index, label);
}

void ByteCodeGenerator::pushLoopLabels(const IntrusivePtr<Label> &breakLabel, const IntrusivePtr<Label> &continueLabel) {
    this->curBuilder().loopLabels.push_back({{breakLabel, continueLabel}, this->curBuilder().localVars.size()});
}

void ByteCodeGenerator::popLoopLabels() {
    this->curBuilder().loopLabels.pop_back();
}

const std::pair<IntrusivePtr<Label>, IntrusivePtr<Label>> &ByteCodeGenerator::peekLoopLabels() {
    return this->curBuilder().loopLabels.back().first;
}

void ByteCodeGenerator::writeSourcePos(unsigned int pos) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    if(this->curBuilder().sourcePosEntries.empty() || this->curBuilder().sourcePosEntries.back().pos != pos) {
        this->curBuilder().sourcePosEntries.push_back({index, pos});
    }
}

void ByteCodeGenerator::catchException(const IntrusivePtr<Label> &begin, const IntrusivePtr<Label> &end,
                                       const DSType &type, unsigned short localOffset, unsigned short localSize) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    this->curBuilder().catchBuilders.push_back(CatchBuilder(begin, end, type, index, localOffset, localSize));
}

void ByteCodeGenerator::enterFinally() {
    for(auto iter = this->curBuilder().finallyLabels.rbegin();
        iter != this->curBuilder().finallyLabels.rend(); ++iter) {
        const unsigned int index = this->curBuilder().codeBuffer.size();
        this->emit4byteIns(OpCode::ENTER_FINALLY, 0);
        this->curBuilder().writeLabel(index + 1, *iter, 0, CodeEmitter<true>::LabelTarget::_32);
    }
}

void ByteCodeGenerator::writeCaptureIns(bool isStr, const IntrusivePtr<Label> &label) {
    this->emitBranchIns(isStr ? OpCode::CAPTURE_STR : OpCode::CAPTURE_ARRAY, label);
}

void ByteCodeGenerator::generateCmdArg(CmdArgNode &node) {
    const unsigned int size = node.getSegmentNodes().size();

    if(size == 1) {
        this->visit(*node.getSegmentNodes()[0]);
    } else {
        this->emit0byteIns(OpCode::NEW_STRING);

        unsigned int index = 0;
        const bool tildeExpansion = dynamic_cast<TildeNode *>(node.getSegmentNodes()[0]) != nullptr;
        if(tildeExpansion) {
            this->emitLdcIns(DSValue::create<String_Object>(
                    this->pool.getStringType(), static_cast<TildeNode *>(node.getSegmentNodes()[0])->getValue()));
            this->emit0byteIns(OpCode::APPEND_STRING);
            index++;
        }

        for(; index < size; index++) {
            auto *e = node.getSegmentNodes()[index];
            if(dynamic_cast<StringExprNode *>(e) != nullptr &&
                    static_cast<StringExprNode *>(e)->getExprNodes().size() > 1) {
                this->generateStringExpr(*static_cast<StringExprNode *>(e), true);
            } else {
                this->visit(*e);
                this->emit0byteIns(OpCode::APPEND_STRING);
            }
        }

        if(tildeExpansion) {
            this->emit0byteIns(OpCode::EXPAND_TILDE);
        }
    }
}

void ByteCodeGenerator::writePipelineIns(const std::vector<IntrusivePtr<Label>> &labels) {
    const unsigned int size = labels.size();
    if(size > UINT8_MAX) {
        fatal("reach limit\n");
    }

    const unsigned int offset = this->curBuilder().codeBuffer.size();
    this->emitIns(OpCode::CALL_PIPELINE);
    this->curBuilder().append8(size);
    for(unsigned int i = 0; i < size; i++) {
        this->curBuilder().append16(0);
        this->curBuilder().writeLabel(offset + 2 + i * 2, labels[i], offset, CodeEmitter<true>::LabelTarget::_16);
    }
}

void ByteCodeGenerator::generateStringExpr(StringExprNode &node, bool fragment) {
    const unsigned int size = node.getExprNodes().size();
    if(size == 0) {
        if(!fragment) {
            this->emit0byteIns(OpCode::PUSH_ESTRING);
        }
    } else if(size == 1) {
        this->visit(*node.getExprNodes()[0]);
    } else {
        if(!fragment) {
            this->emit0byteIns(OpCode::NEW_STRING);
        }
        unsigned int count = 0;
        for(Node *e : node.getExprNodes()) {
            if(dynamic_cast<BinaryOpNode *>(e) != nullptr) {
                auto *binary = static_cast<BinaryOpNode *>(e);
                if(dynamic_cast<StringExprNode *>(binary->getOptNode()) != nullptr) {
                    for(Node *e2 : static_cast<StringExprNode *>(binary->getOptNode())->getExprNodes()) {
                        this->visit(*e2);
                        this->emit0byteIns(OpCode::APPEND_STRING);
                    }
                    continue;
                }
            }
            this->visit(*e);
            if(count++ == 0 && dynamic_cast<EmptyNode *>(e) != nullptr) {
                /**
                 * When calling `APPEND_STRING' ins, the operand stack layout is the following
                 *
                 * +-----------------------------+-------+
                 * | buf (created by NEW_STRING) | value |
                 * +-----------------------------+-------+
                 *
                 * However, when string self assignment, first expr is empty expression
                 * (due to self assignment implementation, see. AssignNode).
                 * As a result, the stack layout will be the following
                 *
                 * +-------+-----+
                 * | value | buf |
                 * +-------+-----+
                 *
                 * In this situation `APPEND_STRING' ins brokes stack top string object (due to appending buf to value).
                 * To prevent it, swap stack top two values.
                 */
                this->emit0byteIns(OpCode::SWAP);
            }

            this->emit0byteIns(OpCode::APPEND_STRING);
        }
    }
}


// visitor api
void ByteCodeGenerator::visit(Node &node) {
    node.accept(*this);
}

void ByteCodeGenerator::visitBaseTypeNode(BaseTypeNode &) {
    fatal("unsupported\n");
}

void ByteCodeGenerator::visitReifiedTypeNode(ReifiedTypeNode &) {
    fatal("unsupported\n");
}

void ByteCodeGenerator::visitFuncTypeNode(FuncTypeNode &) {
    fatal("unsupported\n");
}

void ByteCodeGenerator::visitDBusIfaceTypeNode(DBusIfaceTypeNode &) {
    fatal("unsupported\n");
}

void ByteCodeGenerator::visitReturnTypeNode(ReturnTypeNode &) {
    fatal("unsupported\n");
}

void ByteCodeGenerator::visitTypeOfNode(TypeOfNode &) {
    fatal("unsupported\n");
}

void ByteCodeGenerator::visitIntValueNode(IntValueNode &node) {
    this->emitLdcIns(DSValue::create<Int_Object>(node.getType(), node.getValue()));
}

void ByteCodeGenerator::visitLongValueNode(LongValueNode &node) {
    this->emitLdcIns(DSValue::create<Long_Object>(node.getType(), node.getValue()));
}

void ByteCodeGenerator::visitFloatValueNode(FloatValueNode &node) {
    this->emitLdcIns(DSValue::create<Float_Object>(node.getType(), node.getValue()));
}

void ByteCodeGenerator::visitStringValueNode(StringValueNode &node) {
    if(node.getValue().empty()) {
        this->emit0byteIns(OpCode::PUSH_ESTRING);
    } else {
        this->emitLdcIns(DSValue::create<String_Object>(node.getType(), StringValueNode::extract(std::move(node))));
    }
}

void ByteCodeGenerator::visitStringExprNode(StringExprNode &node) {
    this->generateStringExpr(node, false);
}

void ByteCodeGenerator::visitRegexNode(RegexNode &node) {
    this->emitLdcIns(DSValue::create<Regex_Object>(node.getType(), node.extractRE()));
}

void ByteCodeGenerator::visitArrayNode(ArrayNode &node) {
    this->emitTypeIns(OpCode::NEW_ARRAY, node.getType());
    for(Node *e : node.getExprNodes()) {
        this->visit(*e);
        this->emit0byteIns(OpCode::APPEND_ARRAY);
    }
}

void ByteCodeGenerator::visitMapNode(MapNode &node) {
    this->emitTypeIns(OpCode::NEW_MAP, node.getType());
    const unsigned int size = node.getKeyNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        this->visit(*node.getKeyNodes()[i]);
        this->visit(*node.getValueNodes()[i]);
        this->emit0byteIns(OpCode::APPEND_MAP);
    }
}

void ByteCodeGenerator::visitTupleNode(TupleNode &node) {
    this->emitTypeIns(OpCode::NEW_TUPLE, node.getType());
    const unsigned int size = node.getNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        this->emit0byteIns(OpCode::DUP);
        this->visit(*node.getNodes()[i]);
        this->emit2byteIns(OpCode::STORE_FIELD, i);
    }
}

void ByteCodeGenerator::visitVarNode(VarNode &node) {
    if(node.attr().has(FieldAttribute::ENV)) {
        if(node.attr().has(FieldAttribute::GLOBAL)) {
            this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
        } else {
            this->emit2byteIns(OpCode::LOAD_LOCAL, node.getIndex());
        }

        this->emit0byteIns(OpCode::LOAD_ENV);
    } else if(node.attr().has(FieldAttribute::RANDOM)) {
        this->emit0byteIns(OpCode::RAND);
    } else if(node.attr().has(FieldAttribute::SECONDS)) {
        this->emit0byteIns(OpCode::GET_SECOND);
    } else {
        if(node.attr().has(FieldAttribute::GLOBAL)) {
            if(!node.isUntyped() && node.getType().isFuncType()) {
                this->emit2byteIns(OpCode::LOAD_FUNC, node.getIndex());
            } else {
                this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
            }
        } else {
            this->emit2byteIns(OpCode::LOAD_LOCAL, node.getIndex());
        }
    }
}

void ByteCodeGenerator::visitAccessNode(AccessNode &node) {
    this->visit(*node.getRecvNode());

    switch(node.getAdditionalOp()) {
    case AccessNode::NOP: {
        if(node.attr().has(FieldAttribute::INTERFACE)) {
            std::string desc = encodeFieldDescriptor(
                    node.getRecvNode()->getType(), node.getFieldName().c_str(), node.getType());
            this->emitDescriptorIns(OpCode::INVOKE_GETTER, std::move(desc));
        } else {
            this->emit2byteIns(OpCode::LOAD_FIELD, node.getIndex());
        }
        break;
    }
    case AccessNode::DUP_RECV: {
        this->emit0byteIns(OpCode::DUP);

        if(node.attr().has(FieldAttribute::INTERFACE)) {
            std::string desc = encodeFieldDescriptor(
                    node.getRecvNode()->getType(), node.getFieldName().c_str(), node.getType());
            this->emitDescriptorIns(OpCode::INVOKE_GETTER, std::move(desc));
        } else {
            this->emit2byteIns(OpCode::LOAD_FIELD, node.getIndex());
        }
        break;
    }
    }
}

void ByteCodeGenerator::visitTypeOpNode(TypeOpNode &node) {
    this->visit(*node.getExprNode());

    switch(node.getOpKind()) {
    case TypeOpNode::NO_CAST:
        break;
    case TypeOpNode::TO_VOID:
        this->emit0byteIns(OpCode::POP);
        break;
    case TypeOpNode::NUM_CAST:
        this->emitNumCastIns(node.getExprNode()->getType(), node.getType());
        break;
    case TypeOpNode::TO_STRING:
        this->writeSourcePos(node.getPos());
        this->generateToString();
        break;
    case TypeOpNode::CHECK_CAST:
        this->writeSourcePos(node.getPos());
        this->emitTypeIns(OpCode::CHECK_CAST, node.getType());
        break;
    case TypeOpNode::CHECK_UNWRAP:
        this->writeSourcePos(node.getPos());
        this->emit0byteIns(OpCode::CHECK_UNWRAP);
        break;
    case TypeOpNode::PRINT: {
        this->writeSourcePos(node.getPos());
        auto &exprType = node.getExprNode()->getType();
        if(exprType.isOptionType()) {
            this->emit0byteIns(OpCode::UNWRAP);
        }
        if(exprType != this->pool.getStringType()) {
            this->generateToString();
        }
        this->emitTypeIns(OpCode::PRINT, exprType);
        break;
    }
    case TypeOpNode::ALWAYS_FALSE:
        this->emit0byteIns(OpCode::POP);
        this->emit0byteIns(OpCode::PUSH_FALSE);
        break;
    case TypeOpNode::ALWAYS_TRUE:
        this->emit0byteIns(OpCode::POP);
        this->emit0byteIns(OpCode::PUSH_TRUE);
        break;
    case TypeOpNode::INSTANCEOF:
        this->emitTypeIns(OpCode::INSTANCE_OF, node.getTargetTypeNode()->getType());
        break;
    }
}

void ByteCodeGenerator::visitUnaryOpNode(UnaryOpNode &node) {
    if(node.isUnwrapOp()) {
        this->visit(*node.getExprNode());
        this->emit0byteIns(OpCode::UNWRAP);
    } else {
        this->visit(*node.getApplyNode());
    }
}

void ByteCodeGenerator::visitBinaryOpNode(BinaryOpNode &node) {
    auto kind = node.getOp();
    if(kind == COND_AND || kind == COND_OR) {
        auto elseLabel = makeIntrusive<Label>();
        auto mergeLabel = makeIntrusive<Label>();

        this->visit(*node.getLeftNode());
        this->emitBranchIns(elseLabel);

        if(kind == COND_AND) {
            this->visit(*node.getRightNode());
            this->emitJumpIns(mergeLabel);

            this->markLabel(elseLabel);
            this->emit0byteIns(OpCode::PUSH_FALSE);
        } else {
            this->emit0byteIns(OpCode::PUSH_TRUE);
            this->emitJumpIns(mergeLabel);

            this->markLabel(elseLabel);
            this->visit(*node.getRightNode());
        }

        this->markLabel(mergeLabel);
    } else {
        this->visit(*node.getOptNode());
    }
}

void ByteCodeGenerator::visitApplyNode(ApplyNode &node) {
    const unsigned int paramSize = node.getArgNodes().size();
    this->visit(*node.getExprNode());

    for(Node *e : node.getArgNodes()) {
        this->visit(*e);
    }

    this->writeSourcePos(node.getPos());
    this->emit2byteIns(OpCode::CALL_FUNC, paramSize);
}

void ByteCodeGenerator::visitMethodCallNode(MethodCallNode &node) {
    this->visit(*node.getRecvNode());

    for(Node *e : node.getArgNodes()) {
        this->visit(*e);
    }

    this->writeSourcePos(node.getPos());
    if(node.getHandle()->isInterfaceMethod()) {
        this->emitDescriptorIns(
                OpCode::INVOKE_METHOD, encodeMethodDescriptor(node.getMethodName().c_str(), node.getHandle()));
    } else {
        this->emit4byteIns(OpCode::CALL_METHOD, node.getHandle()->getMethodIndex(), node.getArgNodes().size());
    }
}

void ByteCodeGenerator::visitNewNode(NewNode &node) {
    if(node.getType().isOptionType()) {
        this->emit0byteIns(OpCode::NEW_INVALID);
        return;
    }

    unsigned int paramSize = node.getArgNodes().size();

    this->emitTypeIns(OpCode::NEW, node.getType());

    // push arguments
    for(Node *argNode : node.getArgNodes()) {
        this->visit(*argNode);
    }

    // call constructor
    this->writeSourcePos(node.getPos());
    this->emit2byteIns(OpCode::CALL_INIT, paramSize);
}

void ByteCodeGenerator::visitTernaryNode(TernaryNode &node) {
    auto elseLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->visit(*node.getCondNode());
    this->emitBranchIns(elseLabel);
    this->visit(*node.getLeftNode());
    this->emitJumpIns(mergeLabel);

    this->markLabel(elseLabel);
    this->visit(*node.getRightNode());

    this->markLabel(mergeLabel);
}


void ByteCodeGenerator::visitCmdNode(CmdNode &node) {
    this->visit(*node.getNameNode());

    this->emit0byteIns(OpCode::OPEN_PROC);
    for(auto &e : node.getArgNodes()) {
        this->visit(*e);
    }
    this->emit0byteIns(OpCode::CLOSE_PROC);
}

void ByteCodeGenerator::visitCmdArgNode(CmdArgNode &node) {
    this->generateCmdArg(node);
    this->emit1byteIns(OpCode::ADD_CMD_ARG, node.isIgnorableEmptyString() ? 1 : 0);
}

void ByteCodeGenerator::visitRedirNode(RedirNode &node) {
    this->generateCmdArg(*node.getTargetNode());
    this->emit1byteIns(OpCode::ADD_REDIR_OP, node.getRedirectOP());
}

void ByteCodeGenerator::visitTildeNode(TildeNode &node) {
    this->emitLdcIns(DSValue::create<String_Object>(this->pool.getStringType(), node.getValue()));
    this->emit0byteIns(OpCode::EXPAND_TILDE);
}

void ByteCodeGenerator::visitPipedCmdNode(PipedCmdNode &node) {
    const unsigned int size = node.getCmdNodes().size();
    std::vector<IntrusivePtr<Label>> labels(size + 1);

    this->emit0byteIns(OpCode::NEW_PIPELINE);

    for(unsigned int i = 0; i < size; i++) {
        this->visit(*node.getCmdNodes()[i]);
        labels[i] = makeIntrusive<Label>();
    }
    labels[size] = makeIntrusive<Label>();

    this->writeSourcePos(node.getPos());
    this->writePipelineIns(labels);

    if(size == 1) {
        this->markLabel(labels[0]);
        this->emit1byteIns(OpCode::CALL_CMD, 0);
        this->emit0byteIns(OpCode::SUCCESS_CHILD);
    } else {
        auto begin = makeIntrusive<Label>();
        auto end = makeIntrusive<Label>();

        this->markLabel(begin);
        for(unsigned int i = 0; i < size; i++) {
            this->markLabel(labels[i]);
            this->emit1byteIns(OpCode::CALL_CMD, i);
            this->emit0byteIns(OpCode::SUCCESS_CHILD);
        }
        this->markLabel(end);
        this->catchException(begin, end, this->pool.getAnyType());
        this->emit0byteIns(OpCode::FAILURE_CHILD);
    }

    this->markLabel(labels[size]);
    this->emit0byteIns(OpCode::POP_PIPELINE);
}

void ByteCodeGenerator::visitSubstitutionNode(SubstitutionNode &node) {
    auto beginLabel = makeIntrusive<Label>();
    auto endLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->markLabel(beginLabel);
    this->writeCaptureIns(node.isStrExpr(), mergeLabel);
    this->visit(*node.getExprNode());
    this->markLabel(endLabel);

    this->emit0byteIns(OpCode::SUCCESS_CHILD);
    this->catchException(beginLabel, endLabel, this->pool.getAnyType());
    this->emit0byteIns(OpCode::FAILURE_CHILD);
    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitAssertNode(AssertNode &node) {
    if(this->assertion) {
        this->visit(*node.getCondNode());
        this->writeSourcePos(node.getCondNode()->getPos());
        this->visit(*node.getMessageNode());
        this->emit0byteIns(OpCode::ASSERT);
    }
}

void ByteCodeGenerator::visitBlockNode(BlockNode &node) {
    if(node.getNodeList().empty()) {
        return;
    }

    this->generateBlock(node.getBaseIndex(), node.getVarSize(), needReclaim(node), [&]{
        for(auto &e : node.getNodeList()) {
            this->visit(*e);
        }
    });
}

void ByteCodeGenerator::visitJumpNode(JumpNode &node) {
    assert(!this->curBuilder().loopLabels.empty());

    // reclaim local before jump
    unsigned int blockIndex = this->curBuilder().loopLabels.back().second;
    const unsigned int startOffset = this->curBuilder().localVars[blockIndex].first;
    unsigned int stopOffset = startOffset + this->curBuilder().localVars[blockIndex].second;

    const unsigned int size = this->curBuilder().localVars.size();
    for(; blockIndex < size; blockIndex++) {
        auto &pair = this->curBuilder().localVars[blockIndex];
        stopOffset = pair.first + pair.second;
    }

    if(stopOffset - startOffset > 0) {
        this->emit4byteIns(OpCode::RECLAIM_LOCAL, startOffset, stopOffset - startOffset);
    }

    // add finally before jump
    if(node.isLeavingBlock()) {
        this->enterFinally();
    }

    this->emitJumpIns(node.isBreak() ? this->peekLoopLabels().first : this->peekLoopLabels().second);
}

void ByteCodeGenerator::visitTypeAliasNode(TypeAliasNode &) { } // do nothing

void ByteCodeGenerator::visitForNode(ForNode &node) {
    // generate code
    unsigned short localOffset = 0;
    unsigned short localSize = 0;
    if(dynamic_cast<VarDeclNode *>(node.getInitNode())) {
        localOffset = static_cast<VarDeclNode *>(node.getInitNode())->getVarIndex();
        localSize = 1;
    }

    this->generateBlock(localOffset, localSize, localSize > 0, [&]{
        // push loop label
        auto initLabel = makeIntrusive<Label>();
        auto breakLabel = makeIntrusive<Label>();
        auto continueLabel = makeIntrusive<Label>();
        this->pushLoopLabels(breakLabel, continueLabel);

        this->visit(*node.getInitNode());
        if(dynamic_cast<EmptyNode *>(node.getIterNode()) == nullptr) {
            this->emitJumpIns(initLabel);
        }

        this->markLabel(continueLabel);
        this->visit(*node.getIterNode());

        this->markLabel(initLabel);
        this->visit(*node.getCondNode());
        this->emitBranchIns(breakLabel);

        this->visit(*node.getBlockNode());

        if(!node.getBlockNode()->getType().isBottomType()) {
            this->emitJumpIns(continueLabel);
        }

        this->markLabel(breakLabel);

        // pop loop label
        this->popLoopLabels();
    });
}

void ByteCodeGenerator::visitWhileNode(WhileNode &node) {
    // push loop label
    auto breakLabel = makeIntrusive<Label>();
    auto continueLabel = makeIntrusive<Label>();
    this->pushLoopLabels(breakLabel, continueLabel);

    // generate code
    this->markLabel(continueLabel);
    this->visit(*node.getCondNode());
    this->emitBranchIns(breakLabel);

    this->visit(*node.getBlockNode());
    if(!node.getBlockNode()->getType().isBottomType()) {
        this->emitJumpIns(continueLabel);
    }

    this->markLabel(breakLabel);

    // pop loop label
    this->popLoopLabels();
}

void ByteCodeGenerator::visitDoWhileNode(DoWhileNode &node) {
    // push loop label
    auto initLabel = makeIntrusive<Label>();
    auto breakLabel = makeIntrusive<Label>();
    auto continueLabel = makeIntrusive<Label>();
    this->pushLoopLabels(breakLabel, continueLabel);

    // generate code
    auto &blockNode = *node.getBlockNode();

    this->markLabel(initLabel);
    this->generateBlock(blockNode.getBaseIndex(), blockNode.getVarSize(), this->needReclaim(blockNode), [&]{
        for(auto &e : blockNode.getNodeList()) {
            this->visit(*e);
        }

        this->markLabel(continueLabel);
        this->visit(*node.getCondNode());
    });
    this->emitBranchIns(breakLabel);
    this->emitJumpIns(initLabel);

    this->markLabel(breakLabel);


    // pop loop label
    this->popLoopLabels();
}

void ByteCodeGenerator::visitIfNode(IfNode &node) {
    auto elseLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->visit(*node.getCondNode());
    this->emitBranchIns(elseLabel);
    this->visit(*node.getThenNode());
    this->emitJumpIns(mergeLabel);

    this->markLabel(elseLabel);
    this->visit(*node.getElseNode());

    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitReturnNode(ReturnNode &node) {
    this->visit(*node.getExprNode());

    // add finally before return
    this->enterFinally();

    if(this->inUDC()) {
        assert(node.getExprNode()->getType() == this->pool.getInt32Type());
        this->emit0byteIns(OpCode::RETURN_UDC);
    } else if(node.getExprNode()->getType().isVoidType()) {
        this->emit0byteIns(OpCode::RETURN);
    } else {
        this->emit0byteIns(OpCode::RETURN_V);
    }
}

void ByteCodeGenerator::visitThrowNode(ThrowNode &node) {
    this->visit(*node.getExprNode());
    this->emit0byteIns(OpCode::THROW);
}

void ByteCodeGenerator::visitCatchNode(CatchNode &node) {
    if(node.getBlockNode()->getNodeList().empty()) {
        this->emit0byteIns(OpCode::POP);
    } else {
        this->emit2byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
        this->visit(*node.getBlockNode());
    }
}

void ByteCodeGenerator::visitTryNode(TryNode &node) {
    auto finallyLabel = makeIntrusive<Label>();

    const bool hasFinally = node.getFinallyNode() != nullptr;
    if(hasFinally) {
        this->curBuilder().finallyLabels.push_back(finallyLabel);
    }

    auto beginLabel = makeIntrusive<Label>();
    auto endLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    auto &blockNode = *node.getBlockNode();

    // generate try block
    this->markLabel(beginLabel);
    this->visit(blockNode);
    this->markLabel(endLabel);
    if(!blockNode.getType().isBottomType()) {
        if(hasFinally) {
            this->enterFinally();
        }
        this->emitJumpIns(mergeLabel);
    }

    // generate catch
    for(auto &c : node.getCatchNodes()) {
        this->catchException(beginLabel, endLabel, c->getTypeNode()->getType(),
                             blockNode.getBaseIndex(), blockNode.getVarSize());
        this->visit(*c);
        if(!c->getType().isBottomType()) {
            if(hasFinally) {
                this->enterFinally();
            }
            this->emitJumpIns(mergeLabel);
        }
    }

    // generate finally
    if(hasFinally) {
        this->curBuilder().finallyLabels.pop_back();

        this->markLabel(finallyLabel);
        this->catchException(beginLabel, finallyLabel, this->pool.getAnyType());
        this->visit(*node.getFinallyNode());
        this->emit0byteIns(OpCode::EXIT_FINALLY);
    }

    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitVarDeclNode(VarDeclNode &node) {
    switch(node.getKind()) {
    case VarDeclNode::VAR:
    case VarDeclNode::CONST:
        this->visit(*node.getExprNode());
        break;
    case VarDeclNode::IMPORT_ENV: {
        this->emitLdcIns(DSValue::create<String_Object>(this->pool.getStringType(), node.getVarName()));
        this->emit0byteIns(OpCode::DUP);
        const bool hashDefault = node.getExprNode() != nullptr;
        if(hashDefault) {
            this->visit(*node.getExprNode());
        }

        this->writeSourcePos(node.getPos());
        this->emit1byteIns(OpCode::IMPORT_ENV, hashDefault ? 1 : 0);
        break;
    }
    case VarDeclNode::EXPORT_ENV: {
        this->emitLdcIns(DSValue::create<String_Object>(this->pool.getStringType(), node.getVarName()));
        this->emit0byteIns(OpCode::DUP);
        this->visit(*node.getExprNode());
        this->emit0byteIns(OpCode::STORE_ENV);
        break;
    }
    }

    if(node.isGlobal()) {
        this->emit2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
    } else {
        this->emit2byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
    }
}

void ByteCodeGenerator::visitAssignNode(AssignNode &node) {
    AssignableNode *assignableNode = static_cast<AssignableNode *>(node.getLeftNode());
    unsigned int index = assignableNode->getIndex();
    if(node.isFieldAssign()) {
        AccessNode *accessNode = static_cast<AccessNode *>(node.getLeftNode());
        if(node.isSelfAssignment()) {
            this->visit(*node.getLeftNode());
        } else {
            this->visit(*accessNode->getRecvNode());
        }
        this->visit(*node.getRightNode());

        if(assignableNode->attr().has(FieldAttribute::INTERFACE)) {
            std::string desc = encodeFieldDescriptor(
                    accessNode->getRecvNode()->getType(), accessNode->getFieldName().c_str(), accessNode->getType());
            this->emitDescriptorIns(OpCode::INVOKE_SETTER, std::move(desc));
        } else {
            this->emit2byteIns(OpCode::STORE_FIELD, index);
        }
    } else {
        if(node.isSelfAssignment()) {
            this->visit(*node.getLeftNode());
        }
        this->visit(*node.getRightNode());
        VarNode *varNode = static_cast<VarNode *>(node.getLeftNode());

        if(varNode->attr().has(FieldAttribute::ENV)) {
            if(varNode->attr().has(FieldAttribute::GLOBAL)) {
                this->emit2byteIns(OpCode::LOAD_GLOBAL, index);
            } else {
                this->emit2byteIns(OpCode::LOAD_LOCAL, index);
            }

            this->emit0byteIns(OpCode::SWAP);
            this->emit0byteIns(OpCode::STORE_ENV);
        } else if(varNode->attr().has(FieldAttribute::SECONDS)) {
            this->emit0byteIns(OpCode::SET_SECOND);
        } else {
            if(varNode->attr().has(FieldAttribute::GLOBAL)) {
                this->emit2byteIns(OpCode::STORE_GLOBAL, index);
            } else {
                this->emit2byteIns(OpCode::STORE_LOCAL, index);
            }
        }
    }
}

void ByteCodeGenerator::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
    this->visit(*node.getRecvNode());
    this->visit(*node.getIndexNode());
    this->emit0byteIns(OpCode::DUP2);

    this->visit(*node.getGetterNode());
    this->visit(*node.getRightNode());

    this->visit(*node.getSetterNode());
}

void ByteCodeGenerator::visitFunctionNode(FunctionNode &node) {
    this->initCodeBuilder(CodeKind::FUNCTION, node.getMaxVarNum());
    this->visit(*node.getBlockNode());
    auto func = DSValue::create<FuncObject>(this->finalizeCodeBuilder(node));

    this->emitLdcIns(func);
    this->emit2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
}

void ByteCodeGenerator::visitInterfaceNode(InterfaceNode &) { } // do nothing

void ByteCodeGenerator::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
    this->initCodeBuilder(CodeKind::USER_DEFINED_CMD, node.getMaxVarNum());
    this->visit(*node.getBlockNode());
    auto func = DSValue::create<FuncObject>(this->finalizeCodeBuilder(node));

    this->emitLdcIns(func);
    this->emit2byteIns(OpCode::STORE_GLOBAL, node.getUdcIndex());
}

void ByteCodeGenerator::visitEmptyNode(EmptyNode &) { } // do nothing

void ByteCodeGenerator::visitRootNode(RootNode &rootNode) {
    for(auto &node : rootNode.refNodeList()) {
        this->visit(*node);
    }

    this->emit0byteIns(OpCode::STOP_EVAL);
}

void ByteCodeGenerator::initCodeBuilder(CodeKind kind, unsigned short localVarNum) {
    // push new builder
    auto *builder = new CodeBuilder();
    this->builders.push_back(builder);

    // generate header
    this->curBuilder().append8(static_cast<unsigned char>(kind));
    this->curBuilder().append32(0);
    this->curBuilder().append16(localVarNum);
}

void ByteCodeGenerator::initToplevelCodeBuilder(const RootNode &node) {
    this->initCodeBuilder(CodeKind::TOPLEVEL, node.getMaxVarNum());
    this->curBuilder().append16(node.getMaxGVarNum());
}

CompiledCode ByteCodeGenerator::finalizeCodeBuilder(const CallableNode &node) {
    this->curBuilder().finalize();

    // extract code
    const unsigned int codeSize = this->curBuilder().codeBuffer.size();
    this->curBuilder().emit32(1, codeSize);
    unsigned char *code = extract(std::move(this->curBuilder().codeBuffer));

    // create constant pool
    const unsigned int constSize = this->curBuilder().constBuffer.size();
    DSValue *constPool = new DSValue[constSize + 1];
    for(unsigned int i = 0; i < constSize; i++) {
        constPool[i] = std::move(this->curBuilder().constBuffer[i]);
    }
    constPool[constSize] = nullptr; // sentinel

    // create source pos entry
    const unsigned int lineNumEntrySize = this->curBuilder().sourcePosEntries.size();
    SourcePosEntry *entries = new SourcePosEntry[lineNumEntrySize + 1];
    for(unsigned int i = 0; i < lineNumEntrySize; i++) {
        entries[i] = this->curBuilder().sourcePosEntries[i];
    }
    entries[lineNumEntrySize] = {0, 0};  // sentinel

    // create exception entry
    const unsigned int exeptEntrySize = this->curBuilder().catchBuilders.size();
    ExceptionEntry *except = new ExceptionEntry[exeptEntrySize + 1];
    for(unsigned int i = 0; i < exeptEntrySize; i++) {
        except[i] = this->curBuilder().catchBuilders[i].toEntry();
    }
    except[exeptEntrySize] = {
            .type = nullptr,
            .begin = 0,
            .end = 0,
            .dest = 0,
            .localOffset = 0,
            .localSize = 0,
    };  // sentinel

    // remove current builder
    delete this->builders.back();
    this->builders.pop_back();

    return CompiledCode(node.getSourceInfoPtr(), node.getName().empty() ? nullptr : node.getName().c_str(),
                    code, constPool, entries, except);
}

CompiledCode ByteCodeGenerator::generateToplevel(RootNode &node) {
    this->initToplevelCodeBuilder(node);
    this->visit(node);
    return this->finalizeCodeBuilder(node);
}

static unsigned int digit(unsigned int n) {
    unsigned int c;
    for(c = 0; n > 0; c++) {
        n /= 10;
    }
    return c;
}


static void dumpCodeImpl(std::ostream &stream, DSState &ctx, const CompiledCode &c,
                         std::vector<const CompiledCode *> *list) {
    const unsigned int codeSize = c.getCodeSize();

    stream << "DSCode: ";
    switch(c.getKind()) {
    case CodeKind::TOPLEVEL:
        stream << "top level";
        break;
    case CodeKind::FUNCTION:
        stream << "function " << c.getName();
        break;
    case CodeKind::USER_DEFINED_CMD:
        stream << "command " << c.getName();
        break;
    default:
        break;
    }
    stream << std::endl;
    stream << "  code size: " << c.getCodeSize() << std::endl;
    stream << "  number of local variable: " << c.getLocalVarNum() << std::endl;
    if(c.getKind() == CodeKind::TOPLEVEL) {
        stream << "  number of global variable: " << c.getGlobalVarNum() << std::endl;
    }

#if 0
    stream << "Line Number Table:" << std::endl;
    {
        const unsigned int size = c.getSrcInfo()->getLineNumTable().size();
        for(unsigned int i = 0; i < size; i++) {
            stream << "  line " << std::setw(digit(size)) << (i + 1) <<
            ", pos " << c.getSrcInfo()->getLineNumTable()[i] << std::endl;
        }
    }
#endif
    stream << "Code:" << std::endl;
    {
        const char *opName[] = {
#define GEN_NAME(CODE, N) #CODE,
                OPCODE_LIST(GEN_NAME)
#undef GEN_NAME
        };

        for(unsigned int i = c.getCodeOffset(); i < codeSize; i++) {
            OpCode code = static_cast<OpCode>(c.getCode()[i]);
            stream << "  " << std::setw(digit(codeSize)) << i << ": "
            << opName[static_cast<unsigned char>(code)];
            if(isTypeOp(code)) {
                unsigned long v = read64(c.getCode(), i + 1);
                i += 8;
                stream << "  " << getPool(ctx).getTypeName(*reinterpret_cast<DSType *>(v));
            } else {
                const int byteSize = getByteSize(code);
                if(code == OpCode::CALL_METHOD) {
                    stream << "  " << read16(c.getCode(), i + 1) << "  " << read16(c.getCode(), i + 3);
                } else {
                    switch(byteSize) {
                    case 1:
                        stream << "  " << static_cast<unsigned int>(read8(c.getCode(), i + 1));
                        break;
                    case 2:
                        stream << "  " << read16(c.getCode(), i + 1);
                        break;
                    case 4:
                        stream << "  " << read32(c.getCode(), i + 1);
                        break;
                    case -1: {
                        unsigned int s = static_cast<unsigned int>(read8(c.getCode(), i + 1));
                        stream << " " << s;
                        for(unsigned int index = 0; index < s; index++) {
                            stream << "  " << read16(c.getCode(), i + 2 + index * 2);
                        }
                        break;
                    }
                    default:
                        break;  // do nothing
                    }
                }
                if(byteSize >= 0) {
                    i += byteSize;
                } else {
                    i += -1 * byteSize + 2 * read8(c.getCode(), i + 1);
                }
            }
            stream << std::endl;
        }
    }


    stream << "Constant Pool:" << std::endl;
    {
        unsigned int constSize;
        for(constSize = 0; c.getConstPool()[constSize]; constSize++);
        for(unsigned int i = 0; c.getConstPool()[i]; i++) {
            stream << "  " << std::setw(digit(constSize)) << i << ": ";
            auto &v = c.getConstPool()[i];
            switch(v.kind()) {
            case DSValueKind::NUMBER:
                stream << static_cast<unsigned long>(v.value());
                break;
            case DSValueKind::OBJECT:
                if(list != nullptr && dynamic_cast<FuncObject *>(v.get()) != nullptr) {
                    list->push_back(&static_cast<FuncObject *>(v.get())->getCode());
                }
                stream << (v.get()->getType() != nullptr ? getPool(ctx).getTypeName(*v.get()->getType()) : "(null)")
                << " " << v.get()->toString(ctx, nullptr);
                break;
            case DSValueKind::INVALID:
                break;
            }
            stream << std::endl;
        }
    }


    stream << "Source Pos Entry:" << std::endl;
    {
        auto &srcInfo = c.getSrcInfo();
        const unsigned int maxLineNum = srcInfo->getLineNumTable().size() + srcInfo->getLineNumOffset();
        for(unsigned int i = 0; c.getSourcePosEntries()[i].address != 0; i++) {
            const auto &e = c.getSourcePosEntries()[i];
            stream << "  lineNum: " << std::setw(digit(maxLineNum)) << srcInfo->getLineNum(e.pos)
                   << ", address: " << std::setw(digit(codeSize)) << e.address
                   << ", pos: " << e.pos << std::endl;
        }
    }

    stream << "Exception Table:" << std::endl;
    for(unsigned int i = 0; c.getExceptionEntries()[i].type != nullptr; i++) {
        const auto &e = c.getExceptionEntries()[i];
        stream << "  begin: " << e.begin << ", end: " << e.end << ", type: "
        << getPool(ctx).getTypeName(*e.type) << ", dest: " << e.dest << std::endl;
    }
}

void dumpCode(std::ostream &stream, DSState &ctx, const CompiledCode &c) {
    stream << "Source File: " << c.getSrcInfo()->getSourceName() << std::endl;

    std::vector<const CompiledCode *> list;

    dumpCodeImpl(stream, ctx, c, &list);
    for(auto &e : list) {
        stream << std::endl;
        dumpCodeImpl(stream, ctx, *e, nullptr);
    }
}

} // namespace ydsh
