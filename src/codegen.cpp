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

#include <cstdarg>

#include "codegen.h"
#include "redir.h"
#include "type_pool.h"

namespace ydsh {

int getByteSize(OpCode code) {
  int table[] = {
#define GEN_BYTE_SIZE(CODE, N, S) N,
      OPCODE_LIST(GEN_BYTE_SIZE)
#undef GEN_BYTE_SIZE
  };
  return table[static_cast<unsigned char>(code)];
}

bool isTypeOp(OpCode code) {
  switch (code) {
  case OpCode::PRINT:
  case OpCode::INSTANCE_OF:
  case OpCode::CHECK_CAST:
  case OpCode::NEW:
    ASSERT_BYTE_SIZE(code, 3);
    return true;
  default:
    return false;
  }
}

void ConstBuffer::append(DSValue &&value) {
  if (this->size == this->cap) {
    unsigned int newSize = this->size;
    newSize += (newSize >> 1u);
    auto newValues = std::make_unique<DSValue[]>(newSize);
    for (unsigned int i = 0; i < this->size; i++) {
      newValues[i] = std::move(this->values[i]);
    }
    this->cap = newSize;
    this->values = std::move(newValues);
  }
  this->values[this->size++] = std::move(value);
}

CompiledCode CodeBuilder::build(const std::string &name) {
  if (!this->finalize()) {
    return CompiledCode();
  }

  const unsigned int codeSize = this->codeBuffer.size();
  DSCode code{
      .codeKind = this->kind,
      .localVarNum = this->localVarNum,
      .stackDepth = static_cast<unsigned short>(this->maxStackDepth),
      .size = codeSize,
      .code = this->codeBuffer.take(),
  };

  // create constant pool
  this->constBuffer.append(nullptr); // sentinel
  auto *constPool = std::move(this->constBuffer).take();

  // extract source pos entry
  this->lineNumEntries.push_back({SYS_LIMIT_FUNC_LEN, 0});
  auto *entries = this->lineNumEntries.take();

  // create exception entry
  const unsigned int exceptEntrySize = this->catchBuilders.size();
  auto *except = new ExceptionEntry[exceptEntrySize + 1];
  for (unsigned int i = 0; i < exceptEntrySize; i++) {
    except[i] = this->catchBuilders[i].toEntry();
  }
  except[exceptEntrySize] = {
      .type = nullptr,
      .begin = 0,
      .end = 0,
      .dest = 0,
      .localOffset = 0,
      .localSize = 0,
  }; // sentinel

  return CompiledCode(this->modId, name, code, constPool, entries, except);
}

// ###############################
// ##     ByteCodeGenerator     ##
// ###############################

void ByteCodeGenerator::emitIns(OpCode op) {
  this->curBuilder().append8(static_cast<unsigned char>(op));

  // max stack depth size
  int table[] = {
#define GEN_SIZE_TABLE(C, N, S) S,
      OPCODE_LIST(GEN_SIZE_TABLE)
#undef GEN_SIZE_TABLE
  };
  int size = table[static_cast<unsigned int>(op)];
  this->curBuilder().stackDepthCount += static_cast<short>(size);
  auto count = this->curBuilder().stackDepthCount;
  if (count > 0 && static_cast<unsigned short>(count) > this->curBuilder().maxStackDepth) {
    this->curBuilder().maxStackDepth = count;
  }
}

unsigned int ByteCodeGenerator::emitConstant(DSValue &&value) {
  this->curBuilder().constBuffer.append(std::move(value));
  unsigned int index = this->curBuilder().constBuffer.getSize() - 1;
  if (index > 0xFFFFFF) {
    fatal("const pool index is up to 24bit\n");
  }
  return index;
}

void ByteCodeGenerator::emitMethodCallIns(unsigned int paramSize, const MethodHandle &handle) {
  if (handle.isNative()) {
    this->emitNativeCallIns(paramSize + 1, handle.getIndex(), !handle.getReturnType().isVoidType());
  } else {
    fatal("unsupported\n");
  }
}

void ByteCodeGenerator::emitLdcIns(DSValue &&value) {
  unsigned int index = this->emitConstant(std::move(value));
  if (index <= UINT8_MAX) {
    this->emit1byteIns(OpCode::LOAD_CONST, index);
  } else if (index <= UINT16_MAX) {
    this->emit2byteIns(OpCode::LOAD_CONST_W, index);
  } else {
    this->emit3byteIns(OpCode::LOAD_CONST_T, index);
  }
}

void ByteCodeGenerator::emitToString() {
  if (this->handle_STR == nullptr) {
    this->handle_STR = this->typePool.lookupMethod(this->typePool.get(TYPE::Any), OP_STR);
  }
  this->emitMethodCallIns(0, *this->handle_STR);
}

void ByteCodeGenerator::emitBranchIns(OpCode op, const Label &label) {
  const unsigned int index = this->currentCodeOffset(); // FIXME: check index range
  this->emit2byteIns(op, 0);
  this->curBuilder().writeLabel(index + 1, label, index, CodeEmitter<true>::LabelTarget::_16);
}

void ByteCodeGenerator::emitForkIns(ForkKind kind, const Label &label) {
  const unsigned int offset = this->currentCodeOffset();
  this->emitIns(OpCode::FORK);
  this->curBuilder().append8(static_cast<unsigned char>(kind));
  this->curBuilder().append16(0);
  this->curBuilder().writeLabel(offset + 2, label, offset, CodeEmitter<true>::LabelTarget::_16);
}

void ByteCodeGenerator::emitJumpIns(const Label &label, OpCode op) {
  assert(op == OpCode::GOTO || op == OpCode::GOTO_UNWIND || op == OpCode::GOTO_UNWIND_V ||
         op == OpCode::ENTER_FINALLY);
  const unsigned int index = this->currentCodeOffset();
  this->emit4byteIns(op, 0);
  this->curBuilder().writeLabel(index + 1, label, 0, CodeEmitter<true>::LabelTarget::_32);
}

void ByteCodeGenerator::markLabel(Label &label) {
  const unsigned int index = this->currentCodeOffset();
  this->curBuilder().markLabel(index, label);
}

void ByteCodeGenerator::pushLoopLabels(Label breakLabel, Label continueLabel,
                                       Label breakWithValueLabel) {
  LoopState s = {
      .breakLabel = std::move(breakLabel),
      .continueLabel = std::move(continueLabel),
      .breakWithValueLabel = std::move(breakWithValueLabel),
      .blockIndex = static_cast<unsigned int>(this->curBuilder().localVars.size()),
  };
  this->curBuilder().loopLabels.push_back(std::move(s));
}

void ByteCodeGenerator::emitSourcePos(unsigned int pos) {
  const unsigned int index = this->currentCodeOffset();
  unsigned int lineNum = this->curBuilder().lexer.getLineNumByPos(pos);
  if (this->curBuilder().lineNumEntries.empty() ||
      this->curBuilder().lineNumEntries.back().lineNum != lineNum) {
    this->curBuilder().lineNumEntries.push_back({index, lineNum});
  }
}

void ByteCodeGenerator::catchException(const Label &begin, const Label &end, const DSType &type,
                                       unsigned short localOffset, unsigned short localSize) {
  const unsigned int index = this->currentCodeOffset();
  this->curBuilder().catchBuilders.emplace_back(begin, end, type, index, localOffset, localSize);
}

void ByteCodeGenerator::enterMultiFinally(unsigned int depth, unsigned int localOffset,
                                          unsigned int localSize) {
  const unsigned int size = this->tryFinallyLabels().size();
  depth = std::min(depth, size);

  /**
   * if enter defer block, not reclaim locals since defer block reclaim theme itself
   */
  unsigned int index = 0;
  for (; index < depth; index++) {
    auto &e = this->tryFinallyLabels()[size - 1 - index];
    if (e.defer) {
      assert(e.finallyLabel);
      this->enterFinally(e.finallyLabel);
    } else {
      break;
    }
  }

  if (localSize) {
    this->emit2byteIns(OpCode::RECLAIM_LOCAL, localOffset, localSize);
  }

  for (; index < depth; index++) {
    auto &e = this->tryFinallyLabels()[size - 1 - index];
    if (e.finallyLabel) {
      this->enterFinally(e.finallyLabel);
    }
  }
}

unsigned int ByteCodeGenerator::concatCmdArgSegment(CmdArgNode &node, unsigned int index) {
  const unsigned int size = node.getSegmentNodes().size();
  const unsigned int baseIndex = index;
  bool tilde = node.isTildeAt(index);
  for (; index < size; index++) {
    if ((index - baseIndex) > 0 && node.isTildeAt(index)) {
      break;
    }
    this->generateConcat(*node.getSegmentNodes()[index], (index - baseIndex) > 0);
  }
  if (tilde) {
    this->emit0byteIns(OpCode::EXPAND_TILDE);
  }
  if (baseIndex > 0) {
    this->emit0byteIns(OpCode::CONCAT);
  }
  return index;
}

void ByteCodeGenerator::generatePipeline(PipelineNode &node, ForkKind forkKind) {
  const bool lastPipe = node.isLastPipe();
  const unsigned int size = node.getNodes().size() - (lastPipe ? 1 : 0);
  const unsigned int labelSize = node.getNodes().size() + (lastPipe ? 0 : 1);

  // init label
  std::vector<Label> labels(labelSize);
  for (unsigned int i = 0; i < labelSize; i++) {
    labels[i] = makeLabel();
  }

  // generate pipeline
  this->emitSourcePos(node.getPos());
  this->emitPipelineIns(labels, lastPipe, forkKind);

  auto begin = makeLabel();
  auto end = makeLabel();

  // generate pipeline (child)
  this->markLabel(begin);
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      this->emit0byteIns(OpCode::HALT);
    }
    this->markLabel(labels[i]);
    this->visit(*node.getNodes()[i]);
  }
  this->markLabel(end);
  this->catchException(begin, end, this->typePool.get(TYPE::_ProcGuard));
  this->emit0byteIns(OpCode::HALT);

  this->markLabel(labels.back());

  if (lastPipe) { // generate last pipe
    this->generateBlock(node.getBaseIndex(), 1, true, [&] {
      this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());
      this->visit(*node.getNodes().back());
    });
  }
}

void ByteCodeGenerator::emitPipelineIns(const std::vector<Label> &labels, bool lastPipe,
                                        ForkKind forkKind) {
  const unsigned int size = labels.size();
  if (size > SYS_LIMIT_PIPE_LEN) {
    fatal("reach limit: %u > %zu\n", size, SYS_LIMIT_PIPE_LEN);
  }

  unsigned int offset = this->currentCodeOffset();
  this->emitIns(lastPipe                     ? OpCode::PIPELINE_LP
                : forkKind == ForkKind::NONE ? OpCode::PIPELINE
                                             : OpCode::PIPELINE_ASYNC);
  if (forkKind != ForkKind::NONE) {
    offset++;
    this->curBuilder().append(static_cast<unsigned char>(forkKind));
  }
  this->curBuilder().append8(size);
  for (unsigned int i = 0; i < size; i++) {
    this->curBuilder().append16(0);
    this->curBuilder().writeLabel(offset + 2 + i * 2, labels[i], offset,
                                  CodeEmitter<true>::LabelTarget::_16);
  }
}

static bool isBinaryStrConcat(const Node &node) {
  if (isa<BinaryOpNode>(node)) {
    auto &bin = cast<const BinaryOpNode>(node);
    if (bin.getOp() == TokenKind::ADD && bin.getLeftNode() &&
        bin.getLeftNode()->getType().is(TYPE::String)) {
      assert(!isa<EmptyNode>(bin.getLeftNode()));
      return true;
    }
  }
  return false;
}

void ByteCodeGenerator::generateConcat(Node &node, const bool fragment) {
  switch (node.getNodeKind()) {
  case NodeKind::String:
    if (cast<StringNode>(node).getValue().empty() && fragment) {
      return;
    }
    break;
  case NodeKind::StringExpr: {
    auto &strExprNode = cast<StringExprNode>(node);
    const unsigned int size = strExprNode.getExprNodes().size();
    if (size == 0) {
      if (!fragment) {
        this->emit0byteIns(OpCode::PUSH_STR0);
      }
      return;
    }
    for (unsigned int i = 0; i < size; i++) {
      this->generateConcat(*strExprNode.getExprNodes()[i], fragment || i > 0);
    }
    return;
  }
  case NodeKind::BinaryOp:
    if (isBinaryStrConcat(node)) {
      auto &binaryNode = cast<BinaryOpNode>(node);
      this->generateConcat(*binaryNode.getLeftNode(), fragment);
      this->generateConcat(*binaryNode.getRightNode(), true);
      return;
    }
    break;
  case NodeKind::Embed: {
    auto &exprNode = cast<EmbedNode>(node).getExprNode();
    if (isBinaryStrConcat(exprNode) || isa<StringExprNode>(exprNode) || isa<StringNode>(exprNode)) {
      this->generateConcat(exprNode, fragment);
      return;
    }
    break;
  }
  default:
    break;
  }

  // default
  this->visit(node);
  if (fragment) {
    this->emit0byteIns(OpCode::CONCAT);
  }
}

// visitor api
void ByteCodeGenerator::visit(Node &node) { node.accept(*this); }

void ByteCodeGenerator::visitTypeNode(TypeNode &) { fatal("unsupported\n"); }

void ByteCodeGenerator::visitNumberNode(NumberNode &node) {
  DSValue value;
  switch (node.kind) {
  case NumberNode::Int: {
    auto num = node.getIntValue();
    if (num >= 0 && num <= UINT8_MAX) {
      this->emit1byteIns(OpCode::PUSH_INT, static_cast<unsigned char>(num));
      return;
    }
    value = DSValue::createInt(num);
    break;
  }
  case NumberNode::Float:
    value = DSValue::createFloat(node.getFloatValue());
    break;
  case NumberNode::Signal:
    assert(node.getIntValue() >= 0 && node.getIntValue() <= UINT8_MAX);
    this->emit1byteIns(OpCode::PUSH_SIG, node.getIntValue());
    return;
  }
  this->emitLdcIns(std::move(value));
}

void ByteCodeGenerator::visitStringNode(StringNode &node) {
  switch (node.getValue().size()) {
  case 0:
    this->emit0byteIns(OpCode::PUSH_STR0);
    break;
  case 1:
    this->emit1byteIns(OpCode::PUSH_STR1, node.getValue()[0]);
    break;
  case 2:
    this->emit2byteIns(OpCode::PUSH_STR2, node.getValue()[0], node.getValue()[1]);
    break;
  case 3:
    this->emit3byteIns(OpCode::PUSH_STR3, node.getValue()[0], node.getValue()[1],
                       node.getValue()[2]);
    break;
  default:
    this->emitLdcIns(DSValue::createStr(node.takeValue()));
    break;
  }
}

void ByteCodeGenerator::visitStringExprNode(StringExprNode &node) { this->generateConcat(node); }

void ByteCodeGenerator::visitRegexNode(RegexNode &node) {
  this->emitLdcIns(DSValue::create<RegexObject>(node.getReStr(), node.extractRE()));
}

void ByteCodeGenerator::visitArrayNode(ArrayNode &node) {
  this->emitTypeIns(OpCode::NEW, node.getType());
  for (auto &e : node.getExprNodes()) {
    this->visit(*e);
    this->emit0byteIns(OpCode::APPEND_ARRAY);
  }
}

void ByteCodeGenerator::visitMapNode(MapNode &node) {
  this->emitTypeIns(OpCode::NEW, node.getType());
  const unsigned int size = node.getKeyNodes().size();
  for (unsigned int i = 0; i < size; i++) {
    this->visit(*node.getKeyNodes()[i]);
    this->visit(*node.getValueNodes()[i]);
    this->emit0byteIns(OpCode::APPEND_MAP);
  }
}

void ByteCodeGenerator::visitTupleNode(TupleNode &node) {
  this->emitTypeIns(OpCode::NEW, node.getType());
  const unsigned int size = node.getNodes().size();
  for (unsigned int i = 0; i < size; i++) {
    this->emit0byteIns(OpCode::DUP);
    this->visit(*node.getNodes()[i]);
    this->emit2byteIns(OpCode::STORE_FIELD, i);
  }
}

void ByteCodeGenerator::visitVarNode(VarNode &node) {
  this->emitSourcePos(node.getPos());
  if (hasFlag(node.attr(), HandleAttr::ENV)) {
    if (hasFlag(node.attr(), HandleAttr::GLOBAL)) {
      this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
    } else {
      this->emit1byteIns(OpCode::LOAD_LOCAL, node.getIndex());
    }

    this->emit0byteIns(OpCode::LOAD_ENV);
  } else if (hasFlag(node.attr(), HandleAttr::MOD_CONST)) {
    this->emit1byteIns(OpCode::LOAD_CONST, node.getIndex());
  } else {
    if (hasFlag(node.attr(), HandleAttr::GLOBAL)) {
      if (node.getIndex() == toIndex(BuiltinVarOffset::MODULE)) {
        this->emit0byteIns(OpCode::LOAD_CUR_MOD);
      } else if (node.getIndex() == toIndex(BuiltinVarOffset::RANDOM)) {
        this->emit0byteIns(OpCode::RAND);
      } else if (node.getIndex() == toIndex(BuiltinVarOffset::SECONDS)) {
        this->emit0byteIns(OpCode::GET_SECOND);
      } else {
        this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
      }
    } else {
      this->emit1byteIns(OpCode::LOAD_LOCAL, node.getIndex());
    }
  }
}

void ByteCodeGenerator::visitAccessNode(AccessNode &node) {
  this->visit(node.getRecvNode());

  if (node.getRecvNode().getType().isModType()) {
    this->emit0byteIns(OpCode::POP);
    this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
  } else {
    switch (node.getAdditionalOp()) {
    case AccessNode::NOP:
      this->emit2byteIns(OpCode::LOAD_FIELD, node.getIndex());
      break;
    case AccessNode::DUP_RECV:
      this->emit0byteIns(OpCode::DUP);
      this->emit2byteIns(OpCode::LOAD_FIELD, node.getIndex());
      break;
    }
  }
  if (hasFlag(node.attr(), HandleAttr::ENV)) {
    this->emit0byteIns(OpCode::LOAD_ENV);
  }
}

void ByteCodeGenerator::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode());

  switch (node.getOpKind()) {
  case TypeOpNode::NO_CAST:
    break;
  case TypeOpNode::TO_VOID:
    this->emit0byteIns(OpCode::POP);
    break;
  case TypeOpNode::NUM_CAST: {
    auto &type = node.getExprNode().getType();
    assert(type.is(TYPE::Int) || type.is(TYPE::Float));
    auto *handle = this->typePool.lookupMethod(type, type.is(TYPE::Int) ? OP_TO_FLOAT : OP_TO_INT);
    assert(handle != nullptr);
    this->emitMethodCallIns(0, *handle);
    break;
  }
  case TypeOpNode::TO_STRING:
    this->emitSourcePos(node.getPos());
    this->emitToString();
    break;
  case TypeOpNode::TO_BOOL: {
    this->emitSourcePos(node.getPos());
    auto *handle = this->typePool.lookupMethod(node.getExprNode().getType(), OP_BOOL);
    assert(handle != nullptr);
    this->emitMethodCallIns(0, *handle);
    break;
  }
  case TypeOpNode::CHECK_CAST:
    this->emitSourcePos(node.getPos());
    this->emitTypeIns(OpCode::CHECK_CAST, node.getType());
    break;
  case TypeOpNode::CHECK_UNWRAP:
    this->emitSourcePos(node.getPos());
    this->emit0byteIns(OpCode::CHECK_UNWRAP);
    break;
  case TypeOpNode::PRINT: {
    this->emitSourcePos(node.getPos());
    auto &exprType = node.getExprNode().getType();
    if (exprType.isOptionType()) {
      auto &elementType = cast<OptionType>(exprType).getElementType();

      auto thenLabel = makeLabel();
      auto mergeLabel = makeLabel();
      this->emitBranchIns(OpCode::TRY_UNWRAP, thenLabel);
      this->emitLdcIns(DSValue::createStr("(invalid)"));
      this->emitJumpIns(mergeLabel);

      this->markLabel(thenLabel);
      if (!elementType.is(TYPE::String)) {
        this->emitToString();
      }

      this->markLabel(mergeLabel);
    } else if (!exprType.is(TYPE::String)) {
      this->emitToString();
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
  if (node.isUnwrapOp()) {
    this->visit(*node.getExprNode());
    this->emit0byteIns(OpCode::UNWRAP);
  } else {
    this->visit(*node.getApplyNode());
  }
}

void ByteCodeGenerator::visitBinaryOpNode(BinaryOpNode &node) {
  auto kind = node.getOp();
  if (kind == TokenKind::COND_AND || kind == TokenKind::COND_OR) {
    auto elseLabel = makeLabel();
    auto mergeLabel = makeLabel();

    this->visit(*node.getLeftNode());
    this->emitBranchIns(elseLabel);

    if (kind == TokenKind::COND_AND) {
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
  } else if (kind == TokenKind::STR_CHECK) {
    auto mergeLabel = makeLabel();

    this->visit(*node.getLeftNode());
    this->emit0byteIns(OpCode::DUP);
    auto *handle = this->typePool.lookupMethod(this->typePool.get(TYPE::String), "empty");
    assert(handle != nullptr);
    this->emitMethodCallIns(0, *handle);

    // check left is empty
    this->emitBranchIns(mergeLabel);
    // if left is empty eval right
    this->emit0byteIns(OpCode::POP);
    this->visit(*node.getRightNode());

    this->markLabel(mergeLabel);
  } else if (kind == TokenKind::NULL_COALE) {
    auto mergeLabel = makeLabel();

    this->visit(*node.getLeftNode());
    this->emitBranchIns(OpCode::TRY_UNWRAP, mergeLabel);

    this->visit(*node.getRightNode());
    this->markLabel(mergeLabel);
  } else if (node.getLeftNode() && node.getLeftNode()->getType().isFuncType() &&
             node.getRightNode() && node.getRightNode()->getType().isFuncType()) {
    this->visit(*node.getLeftNode());
    this->visit(*node.getRightNode());
    if (kind == TokenKind::EQ) {
      this->emit0byteIns(OpCode::REF_EQ);
    } else {
      assert(kind == TokenKind::NE);
      this->emit0byteIns(OpCode::REF_NE);
    }
  } else if (kind == TokenKind::ADD && node.getLeftNode() &&
             node.getLeftNode()->getType().is(TYPE::String)) {
    if (isa<EmptyNode>(node.getLeftNode())) {
      this->visit(*node.getRightNode());
      this->emit0byteIns(OpCode::APPEND);
    } else {
      this->generateConcat(node);
    }
  } else {
    this->visit(*node.getOptNode());
  }
}

void ByteCodeGenerator::visitArgsNode(ArgsNode &node) {
  for (auto &e : node.getNodes()) {
    this->visit(*e);
  }
}

void ByteCodeGenerator::visitApplyNode(ApplyNode &node) {
  const unsigned int paramSize = node.getArgsNode().getNodes().size();
  if (node.isMethodCall()) {
    this->visit(node.getRecvNode());
    this->visit(node.getArgsNode());
    this->emitSourcePos(node.getPos());
    this->emitMethodCallIns(paramSize, *node.getHandle());
  } else {
    this->visit(node.getExprNode());
    this->visit(node.getArgsNode());
    this->emitSourcePos(node.getPos());
    this->emitFuncCallIns(paramSize, !node.getType().isVoidType());
  }
}

void ByteCodeGenerator::visitNewNode(NewNode &node) {
  switch (node.getType().typeKind()) {
  case TypeKind::Option:
    this->emit0byteIns(OpCode::PUSH_INVALID);
    break;
  case TypeKind::Array:
  case TypeKind::Map:
    this->emitTypeIns(OpCode::NEW, node.getType());
    break;
  default:
    assert(node.getHandle());
    if (node.getHandle()->isNative()) {
      // push arguments
      this->emitTypeIns(OpCode::NEW, node.getType());
      this->visit(node.getArgsNode());

      // call constructor
      this->emitSourcePos(node.getPos());
      unsigned int paramSize = node.getArgsNode().getNodes().size();
      this->emitMethodCallIns(paramSize, *node.getHandle());
    } else {
      // push constructor func
      this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getHandle()->getIndex());

      // push arguments
      this->visit(node.getArgsNode());

      // call constructor
      this->emitSourcePos(node.getPos());
      unsigned int paramSize = node.getArgsNode().getNodes().size();
      this->emitFuncCallIns(paramSize, !node.getType().isVoidType());
    }
    break;
  }
}

void ByteCodeGenerator::visitEmbedNode(EmbedNode &node) {
  this->visit(node.getExprNode());
  if (node.getHandle() != nullptr) {
    this->emitSourcePos(node.getPos());
    this->emitMethodCallIns(0, *node.getHandle());
  }
}

void ByteCodeGenerator::visitCmdNode(CmdNode &node) {
  this->visit(node.getNameNode());
  if (node.getNameNode().isTilde()) {
    this->emit0byteIns(OpCode::EXPAND_TILDE);
  }

  this->emit0byteIns(OpCode::NEW_CMD);
  this->emit0byteIns(node.hasRedir() ? OpCode::NEW_REDIR : OpCode::PUSH_NULL);

  for (auto &argNode : node.getArgNodes()) {
    this->visit(*argNode);
  }

  if (node.hasRedir()) {
    this->emit0byteIns(OpCode::DO_REDIR);
  }

  this->emitSourcePos(node.getPos());
  if (node.getUdcIndex() == 0) {
    OpCode ins = node.getNeedFork() ? OpCode::CALL_CMD : OpCode::CALL_CMD_NOFORK;
    this->emit0byteIns(ins);
  } else {
    OpCode ins = node.getNeedFork() ? OpCode::CALL_UDC : OpCode::CALL_UDC_NOFORK;
    this->emit2byteIns(ins, node.getUdcIndex());
  }
}

void ByteCodeGenerator::visitCmdArgNode(CmdArgNode &node) {
  if (node.getGlobPathSize() > 0) {
    const unsigned int size = node.getSegmentNodes().size();
    unsigned int firstIndex = 0;
    for (unsigned int i = 0; i < size; i++) {
      auto &e = node.getSegmentNodes()[i];
      if (isa<WildCardNode>(*e)) {
        this->visit(*e);
        firstIndex = i + 1;
      } else {
        this->generateConcat(*e, i > firstIndex);
      }
    }
    this->emit0byteIns(OpCode::PUSH_NULL); // sentinel
    assert(node.getGlobPathSize() <= SYS_LIMIT_GLOB_FRAG_NUM);
    this->emitGlobIns(node.getGlobPathSize(), node.isTilde());
  } else {
    this->generateCmdArg(node);
    this->emit1byteIns(OpCode::ADD_CMD_ARG, node.isIgnorableEmptyString() ? 1 : 0);
  }
}

void ByteCodeGenerator::visitArgArrayNode(ArgArrayNode &node) {
  this->emitTypeIns(OpCode::NEW, node.getType());
  this->emit0byteIns(OpCode::PUSH_NULL);
  for (auto &argNode : node.getCmdArgNodes()) {
    this->visit(*argNode);
  }
  this->emit0byteIns(OpCode::POP);
}

static RedirOP resolveRedirOp(TokenKind kind) {
  switch (kind) {
#define GEN_CASE(ENUM, BITS)                                                                       \
  case TokenKind::REDIR_##ENUM:                                                                    \
    return RedirOP::ENUM;
    EACH_RedirOP(GEN_CASE)
#undef GEN_CASE
        default : fatal("unsupported redir op: %s\n", toString(kind));
  }
}

void ByteCodeGenerator::visitRedirNode(RedirNode &node) {
  this->generateCmdArg(node.getTargetNode());
  this->emit1byteIns(OpCode::ADD_REDIR_OP,
                     static_cast<unsigned char>(resolveRedirOp(node.getRedirectOP())));
}

void ByteCodeGenerator::visitWildCardNode(WildCardNode &node) {
  this->emit1byteIns(OpCode::PUSH_META, static_cast<unsigned char>(node.meta));
}

void ByteCodeGenerator::visitPipelineNode(PipelineNode &node) {
  this->generatePipeline(node, ForkKind::NONE);
}

void ByteCodeGenerator::visitWithNode(WithNode &node) {
  this->generateBlock(node.getBaseIndex(), 1, true, [&] {
    this->emit0byteIns(OpCode::NEW_REDIR);
    for (auto &e : node.getRedirNodes()) {
      this->visit(*e);
    }
    this->emit0byteIns(OpCode::DO_REDIR);
    this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());

    this->visit(node.getExprNode());
  });
}

void ByteCodeGenerator::visitForkNode(ForkNode &node) {
  if (isa<PipelineNode>(node.getExprNode()) && node.getOpKind() != ForkKind::ARRAY &&
      node.getOpKind() != ForkKind::STR) {
    this->generatePipeline(cast<PipelineNode>(node.getExprNode()), node.getOpKind());
  } else {
    auto beginLabel = makeLabel();
    auto endLabel = makeLabel();
    auto mergeLabel = makeLabel();

    this->markLabel(beginLabel);
    this->emitForkIns(node.getOpKind(), mergeLabel);
    this->visit(node.getExprNode());
    this->markLabel(endLabel);

    this->catchException(beginLabel, endLabel, this->typePool.get(TYPE::_ProcGuard));
    this->emit0byteIns(OpCode::HALT);
    this->markLabel(mergeLabel);
  }
}

void ByteCodeGenerator::visitAssertNode(AssertNode &node) {
  auto mergeLabel = makeLabel();
  this->emitBranchIns(OpCode::ASSERT_ENABLED, mergeLabel);

  this->visit(node.getCondNode());
  this->emitBranchIns(OpCode::BRANCH_NOT, mergeLabel);

  this->visit(node.getMessageNode());
  this->emitSourcePos(node.getCondNode().getPos());
  this->emit0byteIns(OpCode::ASSERT_FAIL);

  this->markLabel(mergeLabel);
}

void ByteCodeGenerator::visitBlockNode(BlockNode &node) {
  if (node.getNodes().empty()) {
    return;
  }

  unsigned int reclaimSize = node.getVarSize();
  if (node.getFirstDeferOffset() > -1) {
    reclaimSize = static_cast<unsigned int>(node.getFirstDeferOffset()) - node.getBaseIndex();
  }
  this->generateBlock(node.getBaseIndex(), reclaimSize, needReclaim(node), [&] {
    unsigned int deferCount = 0;
    std::vector<DeferNode *> deferNodes;
    for (auto &e : node.getNodes()) {
      if (isa<DeferNode>(*e)) {
        deferCount++;
        this->tryFinallyLabels().push_back({
            .beginLabel = makeLabel(),
            .endLabel = makeLabel(),
            .finallyLabel = makeLabel(),
            .localOffset = 0,
            .localSize = 0,
            .defer = true,
        });
        deferNodes.push_back(cast<DeferNode>(e.get()));
        this->markLabel(this->tryFinallyLabels().back().beginLabel);
      } else {
        this->visit(*e);
      }
    }

    auto mergeLabel = makeLabel();
    if (deferCount && !node.getNodes().back()->getType().isNothingType()) {
      this->enterMultiFinally(deferCount);
      this->emitJumpIns(mergeLabel);
    }

    for (unsigned int i = 0; i < deferCount; i++) {
      this->markLabel(this->tryFinallyLabels().back().endLabel);
      this->visit(*deferNodes.back());
      this->tryFinallyLabels().pop_back();
      deferNodes.pop_back();
    }

    this->markLabel(mergeLabel);
  });
}

void ByteCodeGenerator::visitTypeDefNode(TypeDefNode &) {} // do nothing

void ByteCodeGenerator::visitDeferNode(DeferNode &node) {
  auto &e = this->tryFinallyLabels().back();
  this->markLabel(e.finallyLabel);
  this->catchException(e.beginLabel, e.endLabel, this->typePool.get(TYPE::_Root), e.localOffset,
                       e.localSize);
  if (node.getDropLocalSize()) {
    this->emit2byteIns(OpCode::RECLAIM_LOCAL, node.getDropLocalOffset(), node.getDropLocalSize());
  }
  this->visit(node.getBlockNode());
  this->emit0byteIns(OpCode::EXIT_FINALLY);
}

static bool isEmptyCode(Node &node) {
  return isa<EmptyNode>(node) || (isa<BlockNode>(node) && cast<BlockNode>(node).getNodes().empty());
}

void ByteCodeGenerator::visitLoopNode(LoopNode &node) {
  // generate code
  unsigned short localOffset = 0;
  unsigned short localSize = 0;
  if (isa<VarDeclNode>(node.getInitNode())) {
    localOffset = cast<VarDeclNode>(node.getInitNode()).getVarIndex();
    localSize = 1;
  }

  this->generateBlock(localOffset, localSize, localSize > 0, [&] {
    // push loop label
    auto initLabel = makeLabel();
    auto startLabel = makeLabel();
    auto breakLabel = makeLabel();
    auto continueLabel = makeLabel();
    auto breakWithValueLabel = makeLabel();
    this->pushLoopLabels(breakLabel, continueLabel, breakWithValueLabel);

    this->visit(node.getInitNode());
    if (!isEmptyCode(node.getIterNode())) {
      this->emitJumpIns(initLabel);
    }

    if (node.isDoWhile()) {
      this->emitJumpIns(startLabel);
    }
    this->markLabel(continueLabel);
    this->visit(node.getIterNode());

    this->markLabel(initLabel);
    if (node.getCondNode() != nullptr) {
      this->visit(*node.getCondNode());
    } else {
      this->emit0byteIns(OpCode::PUSH_TRUE);
    }
    this->emitBranchIns(breakLabel);

    this->markLabel(startLabel);
    this->emit0byteIns(OpCode::STACK_GUARD);
    this->visit(node.getBlockNode());

    this->markLabel(breakLabel);
    if (!node.getType().isVoidType()) {
      this->emit0byteIns(OpCode::PUSH_INVALID);
      this->markLabel(breakWithValueLabel);
    }

    // pop loop label
    this->popLoopLabels();
  });
}

void ByteCodeGenerator::visitIfNode(IfNode &node) {
  auto elseLabel = makeLabel();
  auto mergeLabel = makeLabel();

  this->visit(node.getCondNode());
  this->emitBranchIns(elseLabel);
  this->visit(node.getThenNode());
  if (!isEmptyCode(node.getElseNode()) && !node.getThenNode().getType().isNothingType()) {
    this->emitJumpIns(mergeLabel);
  }

  this->markLabel(elseLabel);
  this->visit(node.getElseNode());

  this->markLabel(mergeLabel);
}

static DSValue newObject(Node &constNode) {
  auto kind = constNode.getNodeKind();
  assert(kind == NodeKind::Number || kind == NodeKind::String);
  if (kind == NodeKind::Number) {
    if (constNode.getType().is(TYPE::Signal)) {
      return DSValue::createSig(cast<NumberNode>(constNode).getIntValue());
    }
    return DSValue::createInt(cast<NumberNode>(constNode).getIntValue());
  }
  return DSValue::createStr(cast<StringNode>(constNode).getValue());
}

void ByteCodeGenerator::generateMapCase(CaseNode &node) {
  bool hasDefault = node.hasDefault();
  auto mergeLabel = makeLabel();
  auto elseLabel = makeLabel();
  auto value = DSValue::create<MapObject>(this->typePool.get(TYPE::Void));
  auto &map = typeAs<MapObject>(value);

  this->emitLdcIns(value);
  this->visit(node.getExprNode());
  this->emit0byteIns(OpCode::LOOKUP_HASH);

  if (hasDefault) {
    this->emitJumpIns(elseLabel);
  } else {
    this->emitJumpIns(mergeLabel);
  }

  // generate case arm
  const DSType *prevType = nullptr;
  for (auto &armNode : node.getArmNodes()) {
    if (prevType != nullptr && !prevType->isNothingType()) {
      this->emitJumpIns(mergeLabel);
    }
    if (armNode->isDefault()) {
      this->markLabel(elseLabel);
    } else {
      this->generateCaseLabels(*armNode, map);
    }
    this->visit(*armNode);
    prevType = &armNode->getType();
  }

  this->markLabel(mergeLabel);
}

void ByteCodeGenerator::generateCaseLabels(const ArmNode &node, MapObject &obj) {
  unsigned int offset = this->currentCodeOffset();
  for (auto &e : node.getConstPatternNodes()) {
    obj.set(newObject(*e), DSValue::createNum(offset));
  }
}

void ByteCodeGenerator::generateIfElseCase(CaseNode &node) {
  auto &exprType = node.getExprNode().getType();
  assert(exprType.is(TYPE::String));

  // generate expr
  this->visit(node.getExprNode());

  // generate if-else chain
  auto &eqHandle = *this->typePool.lookupMethod(exprType, OP_EQ);
  auto &matchHandle = *this->typePool.lookupMethod(exprType, OP_MATCH);

  int defaultIndex = -1;
  auto mergeLabel = makeLabel();
  for (unsigned int index = 0; index < node.getArmNodes().size(); index++) {
    if (node.getArmNodes()[index]->isDefault()) {
      defaultIndex = static_cast<int>(index);
      continue;
    }
    this->generateIfElseArm(*node.getArmNodes()[index], eqHandle, matchHandle, mergeLabel);
  }
  if (defaultIndex > -1) {           // generate default
    this->emit0byteIns(OpCode::POP); // pop stack top 'expr'
    this->visit(*node.getArmNodes()[static_cast<unsigned int>(defaultIndex)]);
  }
  this->markLabel(mergeLabel);
}

void ByteCodeGenerator::generateIfElseArm(ArmNode &node, const MethodHandle &eqHandle,
                                          const MethodHandle &matchHandle,
                                          const Label &mergeLabel) {
  auto armElse = makeLabel();
  auto armMerge = makeLabel();
  unsigned int size = node.getConstPatternNodes().size();

  // generate arm pattern
  for (unsigned int index = 0; index < size; index++) {
    if (index > 0) {
      auto elseLabel = makeLabel();
      this->emitBranchIns(elseLabel);
      this->emit0byteIns(OpCode::PUSH_TRUE);
      this->emitJumpIns(armMerge);
      this->markLabel(elseLabel);
    }
    this->emit0byteIns(OpCode::DUP);
    auto &patternNode = node.getConstPatternNodes()[index];
    this->visit(*patternNode);
    assert(patternNode->getType().is(TYPE::String) || patternNode->getType().is(TYPE::Regex));
    this->emitMethodCallIns(1, patternNode->getType().is(TYPE::String) ? eqHandle : matchHandle);
  }

  // generate arm action
  this->markLabel(armMerge);
  this->emitBranchIns(armElse);
  this->emit0byteIns(OpCode::POP); // pop stack top 'expr'
  this->visit(node);
  this->emitJumpIns(mergeLabel);
  this->markLabel(armElse);
}

void ByteCodeGenerator::visitCaseNode(CaseNode &node) {
  switch (node.getCaseKind()) {
  case CaseNode::MAP:
    this->generateMapCase(node);
    break;
  case CaseNode::IF_ELSE:
    this->generateIfElseCase(node);
    break;
  }
}

void ByteCodeGenerator::visitArmNode(ArmNode &node) { this->visit(node.getActionNode()); }

void ByteCodeGenerator::generateBreakContinue(JumpNode &node) {
  assert(!this->curBuilder().loopLabels.empty());

  // for break with value
  this->visit(node.getExprNode());

  // reclaim local and enter finally before jump
  unsigned int blockIndex = this->curBuilder().loopLabels.back().blockIndex;
  const unsigned int startOffset = this->curBuilder().localVars[blockIndex].first;
  const unsigned int stopOffset =
      this->curBuilder().localVars.back().first + this->curBuilder().localVars.back().second;

  assert(startOffset <= stopOffset);
  this->enterMultiFinally(node.getTryDepth(), startOffset, stopOffset - startOffset);

  if (node.getOpKind() == JumpNode::BREAK) {
    if (node.getExprNode().getType().isVoidType()) {
      this->emitJumpIns(this->peekLoopLabels().breakLabel, OpCode::GOTO_UNWIND);
    } else {
      this->emitJumpIns(this->peekLoopLabels().breakWithValueLabel, OpCode::GOTO_UNWIND_V);
    }
  } else {
    assert(node.getExprNode().getType().isVoidType());
    this->emitJumpIns(this->peekLoopLabels().continueLabel, OpCode::GOTO_UNWIND);
  }
}

void ByteCodeGenerator::visitJumpNode(JumpNode &node) {
  switch (node.getOpKind()) {
  case JumpNode::BREAK:
  case JumpNode::CONTINUE:
    this->generateBreakContinue(node);
    break;
  case JumpNode::THROW: {
    this->visit(node.getExprNode());
    this->emit0byteIns(OpCode::THROW);
    break;
  }
  case JumpNode::RETURN:
  case JumpNode::RETURN_INIT: {
    if (node.getOpKind() == JumpNode::RETURN_INIT) {
      this->emitTypeIns(OpCode::NEW, node.getExprNode().getType());
      this->emit2byteIns(OpCode::INIT_FIELDS, node.getFieldOffset(), node.getFieldSize());
    } else {
      this->visit(node.getExprNode());
    }

    // add finally before return
    this->enterMultiFinally(this->tryFinallyLabels().size());

    if (this->inUDC()) {
      assert(node.getExprNode().getType().is(TYPE::Int));
      this->emit0byteIns(OpCode::RETURN_UDC);
    } else {
      if (node.getExprNode().getType().isVoidType()) {
        this->emit0byteIns(OpCode::PUSH_INVALID);
      }
      this->emit0byteIns(OpCode::RETURN);
    }
    break;
  }
  }
}

void ByteCodeGenerator::visitCatchNode(CatchNode &node) {
  if (node.getBlockNode().getNodes().empty()) {
    this->emit0byteIns(OpCode::POP);
  } else {
    this->emit1byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
    this->visit(node.getBlockNode());
  }
}

void ByteCodeGenerator::visitTryNode(TryNode &node) {
  const bool hasFinally = node.getFinallyNode() != nullptr;
  auto beginLabel = makeLabel();
  auto endLabel = makeLabel();
  auto finallyLabel = makeLabel();
  auto mergeLabel = makeLabel();

  this->tryFinallyLabels().push_back({
      .beginLabel = beginLabel,
      .endLabel = finallyLabel,
      .finallyLabel = hasFinally ? finallyLabel : nullptr,
      .localOffset = 0,
      .localSize = 0,
      .defer = false,
  });

  // generate try block
  this->markLabel(beginLabel);
  this->visit(node.getExprNode());
  this->markLabel(endLabel);
  if (!node.getExprNode().getType().isNothingType()) {
    if (hasFinally) {
      this->enterFinally(finallyLabel);
    }
    this->emitJumpIns(mergeLabel);
  }

  // generate catch
  auto &blockNode = *findInnerNode<BlockNode>(&node.getExprNode());
  auto maxLocalSize = blockNode.getMaxVarSize();
  for (auto &catchNode : node.getCatchNodes()) {
    auto *innerNode = findInnerNode<CatchNode>(catchNode.get());
    unsigned int varSize = innerNode->getBlockNode().getMaxVarSize();
    if (maxLocalSize < varSize) {
      maxLocalSize = varSize;
    }

    auto &catchType = innerNode->getTypeNode().getType();
    this->catchException(beginLabel, endLabel, catchType, blockNode.getBaseIndex(),
                         blockNode.getVarSize());
    this->visit(*catchNode);
    if (!catchNode->getType().isNothingType()) {
      if (hasFinally) {
        this->enterFinally(finallyLabel);
      }
      this->emitJumpIns(mergeLabel);
    }
  }

  // generate finally
  if (hasFinally) {
    auto &e = this->tryFinallyLabels().back();
    e.localOffset = blockNode.getBaseIndex();
    e.localSize = maxLocalSize;
    this->visit(*node.getFinallyNode());
  }
  this->markLabel(mergeLabel);
  this->tryFinallyLabels().pop_back();
}

void ByteCodeGenerator::visitVarDeclNode(VarDeclNode &node) {
  switch (node.getKind()) {
  case VarDeclNode::VAR:
  case VarDeclNode::LET:
    this->visit(*node.getExprNode());
    break;
  case VarDeclNode::IMPORT_ENV: {
    this->emitLdcIns(DSValue::createStr(node.getVarName()));
    this->emit0byteIns(OpCode::DUP);
    const bool hashDefault = node.getExprNode() != nullptr;
    if (hashDefault) {
      this->visit(*node.getExprNode());
    }

    this->emitSourcePos(node.getPos());
    this->emit1byteIns(OpCode::IMPORT_ENV, hashDefault ? 1 : 0);
    break;
  }
  case VarDeclNode::EXPORT_ENV: {
    this->emitLdcIns(DSValue::createStr(node.getVarName()));
    this->emit0byteIns(OpCode::DUP);
    this->visit(*node.getExprNode());
    this->emit0byteIns(OpCode::STORE_ENV);
    break;
  }
  }

  if (node.isGlobal()) {
    this->emit2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
  } else {
    this->emit1byteIns(OpCode::STORE_LOCAL, node.getVarIndex());
  }
}

void ByteCodeGenerator::visitAssignNode(AssignNode &node) {
  auto &assignableNode = cast<AssignableNode>(node.getLeftNode());
  unsigned int index = assignableNode.getIndex();
  if (node.isFieldAssign()) {
    auto &accessNode = cast<AccessNode>(node.getLeftNode());
    if (node.isSelfAssignment()) {
      this->visit(node.getLeftNode());
    } else {
      this->visit(accessNode.getRecvNode());
      if (accessNode.getRecvNode().getType().isModType()) {
        this->emit0byteIns(OpCode::POP);
      }
    }
    this->visit(node.getRightNode());

    if (hasFlag(accessNode.attr(), HandleAttr::ENV)) {
      if (accessNode.getRecvNode().getType().isModType()) {
        this->emit2byteIns(OpCode::LOAD_GLOBAL, index);
      } else {
        this->emit0byteIns(OpCode::SWAP);
        this->emit2byteIns(OpCode::LOAD_FIELD, index);
      }
      this->emit0byteIns(OpCode::SWAP);
      this->emit0byteIns(OpCode::STORE_ENV);
    } else {
      if (accessNode.getRecvNode().getType().isModType()) {
        this->emit2byteIns(OpCode::STORE_GLOBAL, index);
      } else {
        this->emit2byteIns(OpCode::STORE_FIELD, index);
      }
    }
  } else {
    if (node.isSelfAssignment()) {
      this->visit(node.getLeftNode());
    }
    if (isa<CmdArgNode>(node.getRightNode())) { // for prefix assign
      this->generateCmdArg(cast<CmdArgNode>(node.getRightNode()));
    } else {
      this->visit(node.getRightNode());
    }
    auto &varNode = cast<VarNode>(node.getLeftNode());

    if (hasFlag(varNode.attr(), HandleAttr::ENV)) {
      if (hasFlag(varNode.attr(), HandleAttr::GLOBAL)) {
        this->emit2byteIns(OpCode::LOAD_GLOBAL, index);
      } else {
        this->emit1byteIns(OpCode::LOAD_LOCAL, index);
      }
      this->emit0byteIns(OpCode::SWAP);
      this->emit0byteIns(OpCode::STORE_ENV);
    } else {
      if (hasFlag(varNode.attr(), HandleAttr::GLOBAL)) {
        if (index == toIndex(BuiltinVarOffset::SECONDS)) {
          this->emit0byteIns(OpCode::SET_SECOND);
        } else {
          this->emit2byteIns(OpCode::STORE_GLOBAL, index);
        }
      } else {
        this->emit1byteIns(OpCode::STORE_LOCAL, index);
      }
    }
  }
}

void ByteCodeGenerator::visitElementSelfAssignNode(ElementSelfAssignNode &node) {
  this->visit(node.getRecvNode());
  this->visit(node.getIndexNode());
  this->emit0byteIns(OpCode::DUP2);

  this->visit(node.getGetterNode());
  this->visit(node.getRightNode());

  this->visit(node.getSetterNode());
}

void ByteCodeGenerator::visitPrefixAssignNode(PrefixAssignNode &node) {
  if (node.getExprNode()) {
    this->generateBlock(node.getBaseIndex(), node.getVarSize(), true, [&] {
      this->emit0byteIns(OpCode::NEW_ENV_CTX);
      for (auto &e : node.getAssignNodes()) {
        auto &leftNode = cast<VarNode>(e->getLeftNode());
        this->emitLdcIns(DSValue::createStr(leftNode.getVarName()));
        this->emit0byteIns(OpCode::DUP);
        this->emit1byteIns(OpCode::STORE_LOCAL, leftNode.getIndex());
        assert(isa<CmdArgNode>(e->getRightNode()));
        this->generateCmdArg(cast<CmdArgNode>(e->getRightNode()));
        this->emit0byteIns(OpCode::ADD2ENV_CTX);
      }
      this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());
      this->visit(*node.getExprNode());
    });
  } else {
    for (auto &e : node.getAssignNodes()) {
      assert(e->getRightNode().getType().is(TYPE::String));
      this->visit(*e);
    }
  }
}

void ByteCodeGenerator::visitFunctionNode(FunctionNode &node) {
  this->initCodeBuilder(CodeKind::FUNCTION, node.getMaxVarNum());
  this->visit(node.getBlockNode());

  auto code = this->finalizeCodeBuilder(node.getFuncName());
  if (!code) {
    this->reportError<TooLargeFunc>(node, node.getFuncName().c_str());
  }
  auto func = DSValue::create<FuncObject>(*node.getResolvedType(), std::move(code));

  this->emitLdcIns(func);
  if (!node.isAnonymousFunc()) {
    this->emit2byteIns(OpCode::STORE_GLOBAL, node.getVarIndex());
  }
}

void ByteCodeGenerator::visitInterfaceNode(InterfaceNode &) {} // do nothing

void ByteCodeGenerator::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  this->initCodeBuilder(CodeKind::USER_DEFINED_CMD, node.getMaxVarNum());
  this->visit(node.getBlockNode());

  auto code = this->finalizeCodeBuilder(node.getCmdName());
  if (!code) {
    this->reportError<TooLargeUdc>(node, node.getCmdName().c_str());
  }
  auto func = DSValue::create<FuncObject>(this->typePool.get(TYPE::Void), std::move(code));

  this->emitLdcIns(func);
  this->emit2byteIns(OpCode::STORE_GLOBAL, node.getUdcIndex());
}

void ByteCodeGenerator::visitSourceNode(SourceNode &) {} // do nothing

void ByteCodeGenerator::visitSourceListNode(SourceListNode &) {} // do nothing

void ByteCodeGenerator::visitCodeCompNode(CodeCompNode &) {} // do nothing

void ByteCodeGenerator::visitErrorNode(ErrorNode &) {} // do nothing

void ByteCodeGenerator::visitEmptyNode(EmptyNode &) {} // do nothing

void ByteCodeGenerator::reportErrorImpl(Token token, const char *kind, const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    abort();
  }
  va_end(arg);

  this->error = CodeGenError(token, kind, CStrPtr(str));
}

bool ByteCodeGenerator::generate(std::unique_ptr<Node> &&node) {
  if (isa<DeferNode>(*node)) {
    this->tryFinallyLabels().push_back({
        .beginLabel = makeLabel(),
        .endLabel = makeLabel(),
        .finallyLabel = makeLabel(),
        .localOffset = 0,
        .localSize = 0,
        .defer = true,
    });
    this->toplevelDeferNodes().emplace_back(cast<DeferNode>(node.release()));
    this->markLabel(this->tryFinallyLabels().back().beginLabel);
  } else {
    this->visit(*node);
  }
  return !this->hasError();
}

ObjPtr<FuncObject> ByteCodeGenerator::finalizeToplevelCodeBuilder(Token token,
                                                                  unsigned int maxVarIndex,
                                                                  const ModType &modType) {
  if (!this->toplevelDeferNodes().empty()) {
    this->enterMultiFinally(this->toplevelDeferNodes().size());
  }

  this->curBuilder().localVarNum = maxVarIndex;
  this->emit0byteIns(OpCode::PUSH_INVALID);
  this->emit0byteIns(OpCode::RETURN);

  while (!this->toplevelDeferNodes().empty()) {
    this->markLabel(this->tryFinallyLabels().back().endLabel);
    this->visit(*this->toplevelDeferNodes().back());
    this->tryFinallyLabels().pop_back();
    this->toplevelDeferNodes().pop_back();
  }

  auto code = this->finalizeCodeBuilder(modType.toName());
  ObjPtr<FuncObject> func;
  if (code) {
    auto v = DSValue::create<FuncObject>(modType, std::move(code));
    func = ObjPtr<FuncObject>(&typeAs<FuncObject>(v));
  } else {
    this->reportError<TooLargeToplevel>(token, this->commons.back().getScriptName().asCStr());
  }
  this->commons.pop_back();
  return func;
}

bool ByteCodeGenerator::exitModule(const SourceNode &node) {
  auto func =
      this->finalizeToplevelCodeBuilder(node.getToken(), node.getMaxVarNum(), node.getModType());
  if (!func) {
    return false;
  }

  this->emitLdcIns(func);
  this->emitSourcePos(node.getPathToken().pos);
  this->emit0byteIns(OpCode::DUP);
  this->emit2byteIns(OpCode::STORE_GLOBAL, node.getModType().getIndex());
  this->emitFuncCallIns(0, false);
  return !this->hasError();
}

// ############################
// ##     ByteCodeDumper     ##
// ############################

static unsigned int digit(unsigned int n) {
  unsigned int c;
  if (n == 0) {
    return 1;
  }
  for (c = 0; n > 0; c++) {
    n /= 10;
  }
  return c;
}

static std::string formatNum(unsigned int width, unsigned int num) {
  std::string str;
  unsigned int numWidth = digit(num);
  for (unsigned int i = 0; i < width - numWidth; i++) {
    str += ' ';
  }
  str += std::to_string(num);
  return str;
}

static unsigned int getMaxLineNum(const LineNumEntry *table) {
  unsigned int max = 1;
  for (unsigned int i = 0; table[i]; i++) {
    unsigned int value = table[i].lineNum;
    if (value > max) {
      max = value;
    }
  }
  return max;
}

void ByteCodeDumper::operator()(const CompiledCode &code, unsigned int gvarIndex) {
  this->maxGVarIndex = gvarIndex;
  fprintf(this->fp, "### dump compiled code ###\n");
  this->dumpModule(code);
  while (!this->mods.empty()) {
    auto ref = this->mods.front();
    this->mods.erase(this->mods.begin());
    this->dumpModule(ref.get());
  }
}

void ByteCodeDumper::dumpModule(const CompiledCode &code) {
  fprintf(this->fp, "Source File: %s\n", code.getSourceName().data());

  this->dumpCode(code);
  while (!this->funcs.empty()) {
    auto func = this->funcs.front();
    this->funcs.erase(this->funcs.begin());
    this->dumpCode(func.get());
  }
}

void ByteCodeDumper::dumpCode(const ydsh::CompiledCode &c) {
  fputs("DSCode: ", this->fp);
  switch (c.getKind()) {
  case CodeKind::TOPLEVEL:
    fputs("top level", this->fp);
    break;
  case CodeKind::FUNCTION:
    fprintf(this->fp, "function %s", c.getName());
    break;
  case CodeKind::USER_DEFINED_CMD:
    fprintf(this->fp, "command %s", c.getName());
    break;
  default:
    break;
  }
  fputc('\n', this->fp);
  fprintf(this->fp, "  code size: %d\n", c.getCodeSize());
  fprintf(this->fp, "  max stack depth: %d\n", c.getStackDepth());
  fprintf(this->fp, "  number of local variable: %d\n", c.getLocalVarNum());
  if (c.getKind() == CodeKind::TOPLEVEL) {
    fprintf(this->fp, "  number of global variable: %d\n", this->maxGVarIndex);
  }

  fputs("Code:\n", this->fp);
  {
    const char *opName[] = {
#define GEN_NAME(CODE, N, S) #CODE,
        OPCODE_LIST(GEN_NAME)
#undef GEN_NAME
    };

    for (unsigned int i = 0; i < c.getCodeSize(); i++) {
      auto code = static_cast<OpCode>(c.getCode()[i]);
      fprintf(this->fp, "  %s: %s", formatNum(digit(c.getCodeSize()), i).c_str(),
              opName[static_cast<unsigned char>(code)]);
      if (isTypeOp(code)) {
        unsigned int v = read24(c.getCode(), i + 1);
        i += 3;
        fprintf(this->fp, "  %s", this->typePool.get(v).getName());
      } else {
        const int byteSize = getByteSize(code);
        if (code == OpCode::FORK) {
          fprintf(this->fp, "  %d  %d", read8(c.getCode(), i + 1), read16(c.getCode(), i + 2));
        } else if (code == OpCode::RECLAIM_LOCAL || code == OpCode::ADD_GLOBBING ||
                   code == OpCode::INIT_FIELDS) {
          fprintf(this->fp, "  %d  %d", read8(c.getCode(), i + 1), read8(c.getCode(), i + 2));
        } else if (code == OpCode::CALL_BUILTIN2) {
          unsigned int paramSize = read8(c.getCode(), i + 1);
          const char *name = nativeFuncInfoTable()[read8(c.getCode(), i + 2)].funcName;
          fprintf(this->fp, "  %d  %s", paramSize, name);
        } else if (code == OpCode::PUSH_STR1 || code == OpCode::PUSH_STR2 ||
                   code == OpCode::PUSH_STR3) {
          char data[4];
          unsigned int size = code == OpCode::PUSH_STR1 ? 1 : code == OpCode::PUSH_STR2 ? 2 : 3;
          for (unsigned int index = 0; index < size; index++) {
            data[index] = read8(c.getCode(), i + 1 + index);
          }
          data[size] = '\0';
          fprintf(this->fp, "  `%s'", data);
        } else {
          switch (byteSize) {
          case 1:
            fprintf(this->fp, "  %d", static_cast<unsigned int>(read8(c.getCode(), i + 1)));
            break;
          case 2:
            fprintf(this->fp, "  %d", read16(c.getCode(), i + 1));
            break;
          case 3:
            fprintf(this->fp, "  %d", read24(c.getCode(), i + 1));
            break;
          case 4:
            fprintf(this->fp, "  %d", read32(c.getCode(), i + 1));
            break;
          case -1: {
            if (code == OpCode::PIPELINE_ASYNC) {
              fprintf(this->fp, " %d", read8(c.getCode(), i + 1));
              i++;
            }
            auto s = static_cast<unsigned int>(read8(c.getCode(), i + 1));
            fprintf(this->fp, " %d", s);
            for (unsigned int index = 0; index < s; index++) {
              fprintf(this->fp, "  %d", read16(c.getCode(), i + 2 + index * 2));
            }
            break;
          }
          default:
            break; // do nothing
          }
        }
        if (byteSize >= 0) {
          i += byteSize;
        } else {
          i += -1 * byteSize + 2 * read8(c.getCode(), i + 1);
        }
      }
      fputc('\n', this->fp);
    }
  }

  fputs("Constant Pool:\n", this->fp);
  {
    unsigned int constSize;
    for (constSize = 0; c.getConstPool()[constSize]; constSize++)
      ;
    for (unsigned int i = 0; c.getConstPool()[i]; i++) {
      fprintf(this->fp, "  %s: ", formatNum(digit(constSize), i).c_str());
      auto &v = c.getConstPool()[i];
      std::string value = v.toString();
      switch (v.kind()) {
      case DSValueKind::NUMBER:
        fprintf(this->fp, "%s", value.c_str());
        break;
      case DSValueKind::INVALID:
        break;
      default: {
        const auto &type = this->typePool.get(v.getTypeID());
        if (v.isObject()) {
          if (isa<FuncObject>(v.get())) {
            (type.isModType() ? this->mods : this->funcs)
                .push_back(std::ref(cast<FuncObject>(v.get())->getCode()));
          }
        }
        fprintf(this->fp, "%s %s", type.getName(), value.c_str());
        break;
      }
      }
      fputc('\n', this->fp);
    }
  }

  fputs("Line Number Table:\n", this->fp);
  {
    const unsigned int maxLineNum = getMaxLineNum(c.getLineNumEntries());
    for (unsigned int i = 0; c.getLineNumEntries()[i]; i++) {
      const auto &e = c.getLineNumEntries()[i];
      fprintf(this->fp, "  lineNum: %s, address: %s\n",
              formatNum(digit(maxLineNum), e.lineNum).c_str(),
              formatNum(digit(c.getCodeSize()), e.address).c_str());
    }
  }

  fputs("Exception Table:\n", this->fp);
  for (unsigned int i = 0; c.getExceptionEntries()[i].type != nullptr; i++) {
    const auto &e = c.getExceptionEntries()[i];
    fprintf(this->fp, "  begin: %d, end: %d, type: %s, dest: %d, offset: %d, size: %d\n", e.begin,
            e.end, e.type->getName(), e.dest, e.localOffset, e.localSize);
  }

  fputc('\n', this->fp);
  fflush(this->fp);
}

} // namespace ydsh
