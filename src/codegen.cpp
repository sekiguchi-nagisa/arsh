/*
 * Copyright (C) 2016 Nagisa Sekiguchi
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
#include "state.h"

namespace ydsh {

int getByteSize(OpCode code) {
    static char table[] = {
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
        assert(getByteSize(code) == 8);
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
    };
}


// ###############################
// ##     ByteCodeGenerator     ##
// ###############################

#ifndef NDEBUG
static bool checkByteSize(OpCode op, unsigned char size) {
    return getByteSize(op) == size;
}
#endif

ByteCodeGenerator::~ByteCodeGenerator() {
    for(auto &e : this->builders) {
        delete e;
    }
}

void ByteCodeGenerator::writeIns(OpCode op) {
    this->curBuilder().append8(static_cast<unsigned char>(op));
}

void ByteCodeGenerator::write0byteIns(OpCode op) {
    assert(checkByteSize(op, 0));
    this->writeIns(op);
}

void ByteCodeGenerator::write1byteIns(OpCode op, unsigned char v) {
    assert(checkByteSize(op, 1));
    this->writeIns(op);
    this->curBuilder().append8(v);
}

void ByteCodeGenerator::write2byteIns(OpCode op, unsigned short v) {
    assert(checkByteSize(op, 2));
    this->writeIns(op);
    this->curBuilder().append16(v);
}

void ByteCodeGenerator::write4byteIns(OpCode op, unsigned int v) {
    assert(checkByteSize(op, 4));
    this->writeIns(op);
    this->curBuilder().append32(v);
}

void ByteCodeGenerator::write8byteIns(OpCode op, unsigned long v) {
    assert(checkByteSize(op, 8));
    this->writeIns(op);
    this->curBuilder().append64(v);
}

void ByteCodeGenerator::writeTypeIns(OpCode op, const DSType &type) {
    assert(isTypeOp(op));
    this->write8byteIns(op, reinterpret_cast<unsigned long>(&type));
}

unsigned short ByteCodeGenerator::writeConstant(DSValue &&value) {
    this->curBuilder().constBuffer.push_back(std::move(value));
    unsigned int index = this->curBuilder().constBuffer.size() - 1;
    if(index > UINT16_MAX) {
        fatal("const pool index must be 16bit");
    }
    return index;
}

void ByteCodeGenerator::writeLdcIns(const DSValue &value) {
    this->writeLdcIns(DSValue(value));
}

void ByteCodeGenerator::writeLdcIns(DSValue &&value) {
    unsigned short index = this->writeConstant(std::move(value));
    if(index <= UINT8_MAX) {
        this->write1byteIns(OpCode::LOAD_CONST, index);
    } else {
        this->write2byteIns(OpCode::LOAD_CONST_W, index);
    }
}

void ByteCodeGenerator::writeDescriptorIns(OpCode op, std::string &&desc) {
    unsigned short index = this->writeConstant(DSValue::create<String_Object>(this->pool.getStringType(), std::move(desc)));
    this->write2byteIns(op, index);
}

void ByteCodeGenerator::writeMethodCallIns(OpCode op, unsigned short index, unsigned short paramSize) {
    assert(op == OpCode::CALL_METHOD);
    assert(checkByteSize(op, 4));
    this->writeIns(op);
    this->curBuilder().append16(index);
    this->curBuilder().append16(paramSize);
}

void ByteCodeGenerator::writeToString() {
    if(this->handle_STR == nullptr) {
        this->handle_STR = this->pool.getAnyType().lookupMethodHandle(this->pool, std::string(OP_STR));
    }

    this->writeMethodCallIns(OpCode::CALL_METHOD, this->handle_STR->getMethodIndex(), 0);
}

void ByteCodeGenerator::writeNumCastIns(unsigned short v, const DSType &type) {
    const int index = this->pool.getNumTypeIndex(type);
    assert(index > -1);

    for(int i = 15; i > -1; i--) {
        unsigned short flag = (1 << i);
        if(!hasFlag(v, flag)) {
            continue;
        }
        auto op = static_cast<CastNode::NumberCastOp >(flag);
        switch(op) {
        case CastNode::NOP:
            break;
        case CastNode::COPY_INT:
            this->write1byteIns(OpCode::COPY_INT, index);
            break;
        case CastNode::TO_B:
            this->write0byteIns(OpCode::TO_BYTE);
            break;
        case CastNode::TO_U16:
            this->write0byteIns(OpCode::TO_U16);
            break;
        case CastNode::TO_I16:
            this->write0byteIns(OpCode::TO_I16);
            break;
        case CastNode::NEW_LONG:
            this->write1byteIns(OpCode::NEW_LONG, index);
            break;
        case CastNode::COPY_LONG:
            this->write1byteIns(OpCode::COPY_LONG, index);
            break;
        case CastNode::I_NEW_LONG:
            this->write1byteIns(OpCode::I_NEW_LONG, index);
            break;
        case CastNode::NEW_INT:
            this->write1byteIns(OpCode::NEW_INT, index);
            break;
        case CastNode::U32_TO_D:
            this->write0byteIns(OpCode::U32_TO_D);
            break;
        case CastNode::I32_TO_D:
            this->write0byteIns(OpCode::I32_TO_D);
            break;
        case CastNode::U64_TO_D:
            this->write0byteIns(OpCode::U64_TO_D);
            break;
        case CastNode::I64_TO_D:
            this->write0byteIns(OpCode::I64_TO_D);
            break;
        case CastNode::D_TO_U32:
            this->write0byteIns(OpCode::D_TO_U32);
            break;
        case CastNode::D_TO_I32:
            this->write0byteIns(OpCode::D_TO_I32);
            break;
        case CastNode::D_TO_U64:
            this->write0byteIns(OpCode::D_TO_U64);
            break;
        case CastNode::D_TO_I64:
            this->write0byteIns(OpCode::D_TO_I64);
            break;
        }
    }
}

void ByteCodeGenerator::writeBranchIns(OpCode op, const IntrusivePtr<Label> &label) {
    const unsigned int index = this->curBuilder().codeBuffer.size();    //FIXME: check index range
    this->write2byteIns(op, 0);
    this->curBuilder().writeLabel(index + 1, label, index, ByteCodeWriter<true>::LabelTarget::_16);
}

void ByteCodeGenerator::writeBranchIns(const IntrusivePtr<Label> &label) {
    this->writeBranchIns(OpCode::BRANCH, label);
}

void ByteCodeGenerator::writeJumpIns(const IntrusivePtr<Label> &label) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    this->write4byteIns(OpCode::GOTO, 0);
    this->curBuilder().writeLabel(index + 1, label, 0, ByteCodeWriter<true>::LabelTarget::_32);
}

void ByteCodeGenerator::markLabel(IntrusivePtr<Label> &label) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    this->curBuilder().markLabel(index, label);
}

void ByteCodeGenerator::writeSourcePos(unsigned int pos) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    if(this->curBuilder().sourcePosEntries.empty() || this->curBuilder().sourcePosEntries.back().pos != pos) {
        this->curBuilder().sourcePosEntries.push_back({index, pos});
    }
}

void ByteCodeGenerator::catchException(const IntrusivePtr<Label> &begin, const IntrusivePtr<Label> &end, const DSType &type) {
    const unsigned int index = this->curBuilder().codeBuffer.size();
    this->curBuilder().catchBuilders.push_back(CatchBuilder(begin, end, type, index));
}

void ByteCodeGenerator::enterFinally() {
    for(auto iter = this->curBuilder().finallyLabels.rbegin();
        iter != this->curBuilder().finallyLabels.rend(); ++iter) {
        const unsigned int index = this->curBuilder().codeBuffer.size();
        this->write4byteIns(OpCode::ENTER_FINALLY, 0);
        this->curBuilder().writeLabel(index + 1, *iter, 0, ByteCodeWriter<true>::LabelTarget::_32);
    }
}

void ByteCodeGenerator::writeCaptureIns(bool isStr, const IntrusivePtr<Label> &label) {
    this->writeBranchIns(isStr ? OpCode::CAPTURE_STR : OpCode::CAPTURE_ARRAY, label);
}

void ByteCodeGenerator::generateCmdArg(CmdArgNode &node) {
    const unsigned int size = node.getSegmentNodes().size();

    if(size == 1) {
        this->visit(*node.getSegmentNodes()[0]);
    } else {
        this->write0byteIns(OpCode::NEW_STRING);

        unsigned int index = 0;
        if(dynamic_cast<TildeNode *>(node.getSegmentNodes()[0]) != nullptr) {
            this->generateTilde(*static_cast<TildeNode *>(node.getSegmentNodes()[0]), false);
            this->write0byteIns(OpCode::APPEND_STRING);
            index++;
        }

        for(; index < size; index++) {
            auto *e = node.getSegmentNodes()[index];
            if(dynamic_cast<StringExprNode *>(e) != nullptr &&
                    static_cast<StringExprNode *>(e)->getExprNodes().size() > 1) {
                this->generateStringExpr(*static_cast<StringExprNode *>(e), true);
            } else {
                this->visit(*e);
                this->write0byteIns(OpCode::APPEND_STRING);
            }
        }
    }
}

void ByteCodeGenerator::generateTilde(TildeNode &node, bool isLastSegment) {
    this->writeLdcIns(DSValue::create<String_Object>(this->pool.getStringType(), node.getValue()));
    if(!isLastSegment && strchr(node.getValue().c_str(), '/') == nullptr) {
        return;
    } else {
        this->write0byteIns(OpCode::EXPAND_TILDE);
    }
}

void ByteCodeGenerator::writePipelineIns(const std::vector<IntrusivePtr<Label>> &labels) {
    const unsigned int size = labels.size();
    if(size > UINT8_MAX) {
        fatal("reach limit\n");
    }

    const unsigned int offset = this->curBuilder().codeBuffer.size();
    this->writeIns(OpCode::CALL_PIPELINE);
    this->curBuilder().append8(size);
    for(unsigned int i = 0; i < size; i++) {
        this->curBuilder().append16(0);
        this->curBuilder().writeLabel(offset + 2 + i * 2, labels[i], offset, ByteCodeWriter<true>::LabelTarget::_16);
    }
}

void ByteCodeGenerator::generateStringExpr(StringExprNode &node, bool fragment) {
    const unsigned int size = node.getExprNodes().size();
    if(size == 0) {
        if(!fragment) {
            this->write0byteIns(OpCode::PUSH_ESTRING);
        }
    } else if(size == 1) {
        this->visit(*node.getExprNodes()[0]);
    } else {
        if(!fragment) {
            this->write0byteIns(OpCode::NEW_STRING);
        }
        for(Node *e : node.getExprNodes()) {
            if(dynamic_cast<BinaryOpNode *>(e) != nullptr) {
                auto *binary = static_cast<BinaryOpNode *>(e);
                if(dynamic_cast<StringExprNode *>(binary->getOptNode()) != nullptr) {
                    for(Node *e2 : static_cast<StringExprNode *>(binary->getOptNode())->getExprNodes()) {
                        this->visit(*e2);
                        this->write0byteIns(OpCode::APPEND_STRING);
                    }
                    continue;
                }
            }
            this->visit(*e);
            this->write0byteIns(OpCode::APPEND_STRING);
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
    this->writeLdcIns(DSValue::create<Int_Object>(node.getType(), node.getValue()));
}

void ByteCodeGenerator::visitLongValueNode(LongValueNode &node) {
    this->writeLdcIns(DSValue::create<Long_Object>(node.getType(), node.getValue()));
}

void ByteCodeGenerator::visitFloatValueNode(FloatValueNode &node) {
    this->writeLdcIns(DSValue::create<Float_Object>(node.getType(), node.getValue()));
}

void ByteCodeGenerator::visitStringValueNode(StringValueNode &node) {
    this->writeLdcIns(DSValue::create<String_Object>(node.getType(), StringValueNode::extract(std::move(node))));
}

void ByteCodeGenerator::visitObjectPathNode(ObjectPathNode &node) {
    this->writeLdcIns(DSValue::create<String_Object>(node.getType(), StringValueNode::extract(std::move(node))));
}

void ByteCodeGenerator::visitStringExprNode(StringExprNode &node) {
    this->generateStringExpr(node, false);
}

void ByteCodeGenerator::visitArrayNode(ArrayNode &node) {
    this->writeTypeIns(OpCode::NEW_ARRAY, node.getType());
    for(Node *e : node.getExprNodes()) {
        this->visit(*e);
        this->write0byteIns(OpCode::APPEND_ARRAY);
    }
}

void ByteCodeGenerator::visitMapNode(MapNode &node) {
    this->writeTypeIns(OpCode::NEW_MAP, node.getType());
    const unsigned int size = node.getKeyNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        this->visit(*node.getKeyNodes()[i]);
        this->visit(*node.getValueNodes()[i]);
        this->write0byteIns(OpCode::APPEND_MAP);
    }
}

void ByteCodeGenerator::visitTupleNode(TupleNode &node) {
    this->writeTypeIns(OpCode::NEW_TUPLE, node.getType());
    const unsigned int size = node.getNodes().size();
    for(unsigned int i = 0; i < size; i++) {
        this->write0byteIns(OpCode::DUP);
        this->visit(*node.getNodes()[i]);
        this->write2byteIns(OpCode::STORE_FIELD, i);
    }
}

void ByteCodeGenerator::visitVarNode(VarNode &node) {
    if(node.isEnv()) {
        if(node.isGlobal()) {
            this->write2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
        } else {
            this->write2byteIns(OpCode::LOAD_LOCAL, node.getIndex());
        }

        this->write0byteIns(OpCode::LOAD_ENV);
    } else {
        if(node.isGlobal()) {
            if(!node.isUntyped() && node.getType().isFuncType()) {
                this->write2byteIns(OpCode::LOAD_FUNC, node.getIndex());
            } else {
                this->write2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
            }
        } else {
            this->write2byteIns(OpCode::LOAD_LOCAL, node.getIndex());
        }
    }
}

void ByteCodeGenerator::visitAccessNode(AccessNode &node) {
    this->visit(*node.getRecvNode());

    switch(node.getAdditionalOp()) {
    case AccessNode::NOP: {
        if(node.withinInterface()) {
            std::string desc = encodeFieldDescriptor(
                    node.getRecvNode()->getType(), node.getFieldName().c_str(), node.getType());
            this->writeDescriptorIns(OpCode::INVOKE_GETTER, std::move(desc));
        } else {
            this->write2byteIns(OpCode::LOAD_FIELD, node.getIndex());
        }
        break;
    }
    case AccessNode::DUP_RECV: {
        this->write0byteIns(OpCode::DUP);

        if(node.withinInterface()) {
            std::string desc = encodeFieldDescriptor(
                    node.getRecvNode()->getType(), node.getFieldName().c_str(), node.getType());
            this->writeDescriptorIns(OpCode::INVOKE_GETTER, std::move(desc));
        } else {
            this->write2byteIns(OpCode::LOAD_FIELD, node.getIndex());
        }
        break;
    }
    }
}

void ByteCodeGenerator::visitCastNode(CastNode &node) {
    this->visit(*node.getExprNode());

    switch(node.getOpKind()) {
    case CastNode::NO_CAST:
        break;
    case CastNode::TO_VOID:
        this->write0byteIns(OpCode::POP);
        break;
    case CastNode::NUM_CAST:
        this->writeNumCastIns(node.getNumberCastOp(), node.getType());
        break;
    case CastNode::TO_STRING:
        this->writeSourcePos(node.getStartPos());
        this->writeToString();
        break;
    case CastNode::CHECK_CAST:
        this->writeSourcePos(node.getStartPos());
        this->writeTypeIns(OpCode::CHECK_CAST, node.getType());
        break;
    }
}

void ByteCodeGenerator::visitInstanceOfNode(InstanceOfNode &node) {
    this->visit(*node.getTargetNode());

    switch(node.getOpKind()) {
    case InstanceOfNode::INSTANCEOF:
        this->writeTypeIns(OpCode::INSTANCE_OF, node.getTargetTypeNode()->getType());
        break;
    case InstanceOfNode::ALWAYS_TRUE:
        this->write0byteIns(OpCode::POP);
        this->write0byteIns(OpCode::PUSH_TRUE);
        break;
    case InstanceOfNode::ALWAYS_FALSE:
        this->write0byteIns(OpCode::POP);
        this->write0byteIns(OpCode::PUSH_FALSE);
        break;
    }
}

void ByteCodeGenerator::visitPrintNode(PrintNode &node) {
    this->visit(*node.getExprNode());
    if(node.getExprNode()->getType() != this->pool.getStringType()) {
        this->writeToString();
    }
    this->writeTypeIns(OpCode::PRINT, node.getExprNode()->getType());
}

void ByteCodeGenerator::visitUnaryOpNode(UnaryOpNode &node) {
    this->visit(*node.getApplyNode());
}

void ByteCodeGenerator::visitBinaryOpNode(BinaryOpNode &node) {
    this->visit(*node.getOptNode());
}

void ByteCodeGenerator::visitApplyNode(ApplyNode &node) {
    const unsigned int paramSize = node.getArgNodes().size();
    this->visit(*node.getExprNode());

    for(Node *e : node.getArgNodes()) {
        this->visit(*e);
    }

    this->writeSourcePos(node.getStartPos());
    this->write2byteIns(OpCode::CALL_FUNC, paramSize);
}

void ByteCodeGenerator::visitMethodCallNode(MethodCallNode &node) {
    this->visit(*node.getRecvNode());

    for(Node *e : node.getArgNodes()) {
        this->visit(*e);
    }

    this->writeSourcePos(node.getStartPos());
    if(node.getHandle()->isInterfaceMethod()) {
        this->writeDescriptorIns(
                OpCode::INVOKE_METHOD, encodeMethodDescriptor(node.getMethodName().c_str(), node.getHandle()));
    } else {
        this->writeMethodCallIns(OpCode::CALL_METHOD, node.getHandle()->getMethodIndex(), node.getArgNodes().size());
    }
}

void ByteCodeGenerator::visitNewNode(NewNode &node) {
    unsigned int paramSize = node.getArgNodes().size();

    this->writeTypeIns(OpCode::NEW, node.getType());

    // push arguments
    for(Node *argNode : node.getArgNodes()) {
        this->visit(*argNode);
    }

    // call constructor
    this->writeSourcePos(node.getStartPos());
    this->write2byteIns(OpCode::CALL_INIT, paramSize);
}

void ByteCodeGenerator::visitCondOpNode(CondOpNode &node) {
    auto elseLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->visit(*node.getLeftNode());
    this->writeBranchIns(elseLabel);

    if(node.isAndOp()) {
        this->visit(*node.getRightNode());
        this->writeJumpIns(mergeLabel);

        this->markLabel(elseLabel);
        this->write0byteIns(OpCode::PUSH_FALSE);
    } else {
        this->write0byteIns(OpCode::PUSH_TRUE);
        this->writeJumpIns(mergeLabel);

        this->markLabel(elseLabel);
        this->visit(*node.getRightNode());
    }

    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitTernaryNode(TernaryNode &node) {
    auto elseLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->visit(*node.getCondNode());
    this->writeBranchIns(elseLabel);
    this->visit(*node.getLeftNode());
    this->writeJumpIns(mergeLabel);

    this->markLabel(elseLabel);
    this->visit(*node.getRightNode());

    this->markLabel(mergeLabel);
}


void ByteCodeGenerator::visitCmdNode(CmdNode &node) {
    this->visit(*node.getNameNode());

    this->write0byteIns(OpCode::OPEN_PROC);
    for(auto &e : node.getArgNodes()) {
        this->visit(*e);
    }
    this->write0byteIns(OpCode::CLOSE_PROC);
}

void ByteCodeGenerator::visitCmdArgNode(CmdArgNode &node) {
    this->generateCmdArg(node);
    this->write1byteIns(OpCode::ADD_CMD_ARG, node.isIgnorableEmptyString() ? 1 : 0);
}

void ByteCodeGenerator::visitRedirNode(RedirNode &node) {
    this->generateCmdArg(*node.getTargetNode());
    this->write1byteIns(OpCode::ADD_REDIR_OP, node.getRedirectOP());
}

void ByteCodeGenerator::visitTildeNode(TildeNode &node) {
    this->generateTilde(node, true);
}

void ByteCodeGenerator::visitPipedCmdNode(PipedCmdNode &node) {
    const unsigned int size = node.getCmdNodes().size();
    std::vector<IntrusivePtr<Label>> labels(size + 1);

    this->write0byteIns(OpCode::NEW_PIPELINE);

    for(unsigned int i = 0; i < size; i++) {
        this->visit(*node.getCmdNodes()[i]);
        labels[i] = makeIntrusive<Label>();
    }
    labels[size] = makeIntrusive<Label>();

    this->writeSourcePos(node.getStartPos());
    this->writePipelineIns(labels);

    if(size == 1) {
        this->markLabel(labels[0]);
        this->write1byteIns(OpCode::CALL_CMD, 0);
        this->write0byteIns(OpCode::SUCCESS_CHILD);
    } else {
        auto begin = makeIntrusive<Label>();
        auto end = makeIntrusive<Label>();

        this->markLabel(begin);
        for(unsigned int i = 0; i < size; i++) {
            this->markLabel(labels[i]);
            this->write1byteIns(OpCode::CALL_CMD, i);
            this->write0byteIns(OpCode::SUCCESS_CHILD);
        }
        this->markLabel(end);
        this->catchException(begin, end, this->pool.getAnyType());
        this->write0byteIns(OpCode::FAILURE_CHILD);
    }

    this->markLabel(labels[size]);
    this->write0byteIns(OpCode::POP_PIPELINE);
}

void ByteCodeGenerator::visitSubstitutionNode(SubstitutionNode &node) {
    auto beginLabel = makeIntrusive<Label>();
    auto endLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->markLabel(beginLabel);
    this->writeCaptureIns(node.isStrExpr(), mergeLabel);
    this->visit(*node.getExprNode());
    this->markLabel(endLabel);

    this->write0byteIns(OpCode::SUCCESS_CHILD);
    this->catchException(beginLabel, endLabel, this->pool.getAnyType());
    this->write0byteIns(OpCode::FAILURE_CHILD);
    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitAssertNode(AssertNode &node) {
    if(this->assertion) {
        this->visit(*node.getCondNode());
        this->writeSourcePos(node.getCondNode()->getStartPos());
        this->visit(*node.getMessageNode());
        this->write0byteIns(OpCode::ASSERT);
    }
}

void ByteCodeGenerator::visitBlockNode(BlockNode &node) {
    if(node.getNodeList().empty()) {
        this->write0byteIns(OpCode::NOP);
    }
    for(auto &e : node.getNodeList()) {
        this->visit(*e);
    }
}

void ByteCodeGenerator::visitBreakNode(BreakNode &node) {
    assert(!this->curBuilder().loopLabels.empty());

    // add finally before jump
    if(node.isLeavingBlock()) {
        this->enterFinally();
    }

    this->writeJumpIns(this->curBuilder().loopLabels.back().first);
}

void ByteCodeGenerator::visitContinueNode(ContinueNode &node) {
    assert(!this->curBuilder().loopLabels.empty());

    // add finally before jump
    if(node.isLeavingBlock()) {
        this->enterFinally();
    }

    this->writeJumpIns(this->curBuilder().loopLabels.back().second);
}

void ByteCodeGenerator::visitExportEnvNode(ExportEnvNode &node) {
    this->writeLdcIns(DSValue::create<String_Object>(this->pool.getStringType(), node.getEnvName()));
    this->write0byteIns(OpCode::DUP);
    this->visit(*node.getExprNode());
    this->write0byteIns(OpCode::STORE_ENV);

    if(node.isGlobal()) {
        this->write2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
    } else {
        this->write2byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
    }
}

void ByteCodeGenerator::visitImportEnvNode(ImportEnvNode &node) {
    this->writeLdcIns(DSValue::create<String_Object>(this->pool.getStringType(), node.getEnvName()));
    this->write0byteIns(OpCode::DUP);
    const bool hashDefault = node.getDefaultValueNode() != nullptr;
    if(hashDefault) {
        this->visit(*node.getDefaultValueNode());
    }

    this->writeSourcePos(node.getStartPos());
    this->write1byteIns(OpCode::IMPORT_ENV, hashDefault ? 1 : 0);

    if(node.isGlobal()) {
        this->write2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
    } else {
        this->write2byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
    }
}

void ByteCodeGenerator::visitTypeAliasNode(TypeAliasNode &) { } // do nothing

void ByteCodeGenerator::visitForNode(ForNode &node) {
    // push loop label
    auto initLabel = makeIntrusive<Label>();
    auto breakLabel = makeIntrusive<Label>();
    auto continueLabel = makeIntrusive<Label>();
    this->curBuilder().loopLabels.push_back(std::make_pair(breakLabel, continueLabel));

    // generate code
    this->visit(*node.getInitNode());
    this->writeJumpIns(initLabel);

    this->markLabel(continueLabel);
    this->visit(*node.getIterNode());

    this->markLabel(initLabel);
    this->visit(*node.getCondNode());
    this->writeBranchIns(breakLabel);

    this->visit(*node.getBlockNode());
    this->writeJumpIns(continueLabel);

    this->markLabel(breakLabel);

    // pop loop label
    this->curBuilder().loopLabels.pop_back();
}

void ByteCodeGenerator::visitWhileNode(WhileNode &node) {
    // push loop label
    auto breakLabel = makeIntrusive<Label>();
    auto continueLabel = makeIntrusive<Label>();
    this->curBuilder().loopLabels.push_back(std::make_pair(breakLabel, continueLabel));

    // generate code
    this->markLabel(continueLabel);
    this->visit(*node.getCondNode());
    this->writeBranchIns(breakLabel);

    this->visit(*node.getBlockNode());
    this->writeJumpIns(continueLabel);

    this->markLabel(breakLabel);


    // pop loop label
    this->curBuilder().loopLabels.pop_back();
}

void ByteCodeGenerator::visitDoWhileNode(DoWhileNode &node) {
    // push loop label
    auto initLabel = makeIntrusive<Label>();
    auto breakLabel = makeIntrusive<Label>();
    auto continueLabel = makeIntrusive<Label>();
    this->curBuilder().loopLabels.push_back(std::make_pair(breakLabel, continueLabel));

    // generate code
    this->writeJumpIns(initLabel);

    this->markLabel(continueLabel);
    this->visit(*node.getCondNode());
    this->writeBranchIns(breakLabel);

    this->markLabel(initLabel);
    this->visit(*node.getBlockNode());
    this->writeJumpIns(continueLabel);

    this->markLabel(breakLabel);

    // pop loop label
    this->curBuilder().loopLabels.pop_back();
}

void ByteCodeGenerator::visitIfNode(IfNode &node) {
    auto elseLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    this->visit(*node.getCondNode());
    this->writeBranchIns(elseLabel);
    this->visit(*node.getThenNode());
    this->writeJumpIns(mergeLabel);

    this->markLabel(elseLabel);
    this->visit(*node.getElseNode());

    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitReturnNode(ReturnNode &node) {
    this->visit(*node.getExprNode());

    // add finally before return
    this->enterFinally();

    if(this->inUDC) {
        assert(node.getExprNode()->getType() == this->pool.getInt32Type());
        this->write0byteIns(OpCode::RETURN_UDC);
    } else if(node.getExprNode()->getType().isVoidType()) {
        this->write0byteIns(OpCode::RETURN);
    } else {
        this->write0byteIns(OpCode::RETURN_V);
    }
}

void ByteCodeGenerator::visitThrowNode(ThrowNode &node) {
    this->visit(*node.getExprNode());
    this->write0byteIns(OpCode::THROW);
}

void ByteCodeGenerator::visitCatchNode(CatchNode &node) {
    this->write2byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
    this->visit(*node.getBlockNode());
}

void ByteCodeGenerator::visitTryNode(TryNode &node) {
    auto finallyLabel = makeIntrusive<Label>();

    if(node.getFinallyNode() != nullptr) {
        this->curBuilder().finallyLabels.push_back(finallyLabel);
    }

    auto beginLabel = makeIntrusive<Label>();
    auto endLabel = makeIntrusive<Label>();
    auto mergeLabel = makeIntrusive<Label>();

    // generate try block
    this->markLabel(beginLabel);
    this->visit(*node.getBlockNode());
    this->markLabel(endLabel);
    if(!node.getBlockNode()->getType().isBottomType()) {
        this->enterFinally();
        this->writeJumpIns(mergeLabel);
    }

    // generate catch
    for(auto &c : node.getCatchNodes()) {
        this->catchException(beginLabel, endLabel, c->getTypeNode()->getType());
        this->visit(*c);
        if(!c->getType().isBottomType()) {
            this->enterFinally();
            this->writeJumpIns(mergeLabel);
        }
    }

    // generate finally
    if(node.getFinallyNode() != nullptr) {
        this->curBuilder().finallyLabels.pop_back();

        this->markLabel(finallyLabel);
        this->catchException(beginLabel, finallyLabel, this->pool.getAnyType());
        this->visit(*node.getFinallyNode());
        this->write0byteIns(OpCode::EXIT_FINALLY);
    }

    this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitVarDeclNode(VarDeclNode &node) {
    this->visit(*node.getInitValueNode());
    if(node.isGlobal()) {
        this->write2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
    } else {
        this->write2byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
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

        if(assignableNode->withinInterface()) {
            std::string desc = encodeFieldDescriptor(
                    accessNode->getRecvNode()->getType(), accessNode->getFieldName().c_str(), accessNode->getType());
            this->writeDescriptorIns(OpCode::INVOKE_SETTER, std::move(desc));
        } else {
            this->write2byteIns(OpCode::STORE_FIELD, index);
        }
    } else {
        if(node.isSelfAssignment()) {
            this->visit(*node.getLeftNode());
        }
        this->visit(*node.getRightNode());
        VarNode *varNode = static_cast<VarNode *>(node.getLeftNode());

        if(varNode->isEnv()) {
            if(varNode->isGlobal()) {
                this->write2byteIns(OpCode::LOAD_GLOBAL, index);
            } else {
                this->write2byteIns(OpCode::LOAD_LOCAL, index);
            }

            this->write0byteIns(OpCode::SWAP);
            this->write0byteIns(OpCode::STORE_ENV);
        } else {
            if(varNode->isGlobal()) {
                this->write2byteIns(OpCode::STORE_GLOBAL, index);
            } else {
                this->write2byteIns(OpCode::STORE_LOCAL, index);
            }
        }
    }
}

void ByteCodeGenerator::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
    this->visit(*node.getRecvNode());
    this->visit(*node.getIndexNode());
    this->write0byteIns(OpCode::DUP2);

    this->visit(*node.getGetterNode());
    this->visit(*node.getRightNode());

    this->visit(*node.getSetterNode());
}

void ByteCodeGenerator::visitFunctionNode(FunctionNode &node) {
    this->initCodeBuilder(CodeKind::FUNCTION, node.getMaxVarNum());
    this->visit(*node.getBlockNode());
    auto func = DSValue::create<FuncObject>(this->finalizeCodeBuilder(node));

    this->writeLdcIns(func);
    this->write2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
}

void ByteCodeGenerator::visitInterfaceNode(InterfaceNode &) { } // do nothing

void ByteCodeGenerator::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
    this->inUDC = true;

    this->initCodeBuilder(CodeKind::USER_DEFINED_CMD, node.getMaxVarNum());
    this->visit(*node.getBlockNode());
    auto func = DSValue::create<FuncObject>(this->finalizeCodeBuilder(node));

    this->writeLdcIns(func);
    this->write2byteIns(OpCode::STORE_GLOBAL, node.getUdcIndex());

    this->inUDC = false;
}

void ByteCodeGenerator::visitEmptyNode(EmptyNode &) { } // do nothing

void ByteCodeGenerator::visitRootNode(RootNode &rootNode) {
    for(auto &node : rootNode.refNodeList()) {
        this->visit(*node);
    }

    this->write0byteIns(OpCode::STOP_EVAL);
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
    this->curBuilder().write32(1, codeSize);
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

    stream << "Line Number Table:" << std::endl;
    {
        const unsigned int size = c.getSrcInfo()->getLineNumTable().size();
        for(unsigned int i = 0; i < size; i++) {
            stream << "  line " << std::setw(digit(size)) << (i + 1) <<
            ", pos " << c.getSrcInfo()->getLineNumTable()[i] << std::endl;
        }
    }

    stream << "Code:" << std::endl;
    {
        static const char *opName[] = {
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
                stream << "  " << ctx.getPool().getTypeName(*reinterpret_cast<DSType *>(v));
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
                stream << (v.get()->getType() != nullptr ? ctx.getPool().getTypeName(*v.get()->getType()) : "(null)")
                << " " << v.get()->toString(ctx, nullptr);
                break;
            }
            stream << std::endl;
        }
    }


    stream << "Source Pos Entry:" << std::endl;
    for(unsigned int i = 0; c.getSourcePosEntries()[i].address != 0; i++) {
        const auto &e = c.getSourcePosEntries()[i];
        stream << "  address: " << std::setw(digit(codeSize)) <<
        e.address << ", pos: " << e.pos << std::endl;
    }

    stream << "Exception Table:" << std::endl;
    for(unsigned int i = 0; c.getExceptionEntries()[i].type != nullptr; i++) {
        const auto &e = c.getExceptionEntries()[i];
        stream << "  begin: " << e.begin << ", end: " << e.end << ", type: "
        << ctx.getPool().getTypeName(*e.type) << ", dest: " << e.dest << std::endl;
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
