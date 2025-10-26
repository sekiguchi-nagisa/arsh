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

#include "codegen.h"
#include "format_util.h"
#include "misc/format.hpp"
#include "redir.h"
#include "type_pool.h"

namespace arsh {

int getByteSize(OpCode code) {
  constexpr int table[] = {
#define GEN_BYTE_SIZE(CODE, N, S) N,
      OPCODE_LIST(GEN_BYTE_SIZE)
#undef GEN_BYTE_SIZE
  };
  return table[toUnderlying(code)];
}

bool isTypeOp(OpCode code) {
  switch (code) {
  case OpCode::PRINT:
  case OpCode::CHECK_CAST:
  case OpCode::CHECK_CAST_OPT:
  case OpCode::PUSH_TYPE:
  case OpCode::NEW:
    ASSERT_BYTE_SIZE(code, 3);
    return true;
  default:
    return false;
  }
}

void ConstBuffer::append(Value &&value) {
  if (this->size == this->cap) {
    unsigned int newSize = this->size;
    newSize += (newSize >> 1u);
    auto newValues = std::make_unique<Value[]>(newSize);
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
    return {};
  }

  const unsigned int codeSize = this->codeBuffer.size();
  ARCode code({
      .codeKind = this->kind,
      .localVarNum = this->localVarNum,
      .stackDepth = static_cast<unsigned short>(this->maxStackDepth),
      .size = codeSize,
      .code = this->codeBuffer.take(),
  });

  // create a constant pool
  this->constBuffer.append(nullptr); // sentinel
  auto *constPool = std::move(this->constBuffer).take();

  // extract source pos entry
  this->lineNumEntries.push_back({SYS_LIMIT_FUNC_LEN, 0});
  auto *entries = this->lineNumEntries.take();

  // create an exception entry
  const unsigned int exceptEntrySize = this->catchBuilders.size();
  auto *except = new ExceptionEntry[exceptEntrySize + 1];
  for (unsigned int i = 0; i < exceptEntrySize; i++) {
    except[i] = this->catchBuilders[i].toEntry();
  }
  except[exceptEntrySize] = {
      .typeId = toUnderlying(TYPE::Unresolved_),
      .begin = 0,
      .end = 0,
      .dest = 0,
      .localOffset = 0,
      .localSize = 0,
      .guardLevel = 0,
  }; // sentinel

  return {this->lexer->getSourceName(), this->modId, name, code, constPool, entries, except};
}

// ###############################
// ##     ByteCodeGenerator     ##
// ###############################

void ByteCodeGenerator::emitIns(OpCode op) {
  this->curBuilder().append8(toUnderlying(op));

  // max stack depth size
  constexpr int table[] = {
#define GEN_SIZE_TABLE(C, N, S) S,
      OPCODE_LIST(GEN_SIZE_TABLE)
#undef GEN_SIZE_TABLE
  };
  const int size = table[toUnderlying(op)];
  this->curBuilder().stackDepthCount += static_cast<short>(size);
  const auto count = this->curBuilder().stackDepthCount;
  if (count > 0 && static_cast<unsigned short>(count) > this->curBuilder().maxStackDepth) {
    this->curBuilder().maxStackDepth = count;
  }
}

void ByteCodeGenerator::emitMethodCallIns(const MethodHandle &handle) {
  /**
   * in constructor call, dose not pass receiver
   */
  const unsigned int actualParamSize = handle.getParamSize() + (handle.isConstructor() ? 0 : 1);
  if (handle.isNative()) {
    if (handle.getRecvTypeId() == toUnderlying(TYPE::Command) &&
        StringRef("call") == nativeFuncInfoTable()[handle.getIndex()].funcName) {
      this->emit0byteIns(OpCode::PUSH_NULL);
      this->emit0byteIns(OpCode::CALL_CMD_OBJ);
    } else {
      this->emitNativeCallIns(actualParamSize, handle.getIndex(),
                              !handle.getReturnType().isVoidType());
    }
  } else {
    this->emitValIns(OpCode::CALL_METHOD, actualParamSize, 0);
    this->curBuilder().append16(handle.getIndex());
    if (handle.getReturnType().isVoidType()) {
      this->emit0byteIns(OpCode::POP);
    }
  }
}

void ByteCodeGenerator::emitLdcIns(Value &&value) {
  const unsigned int index = this->emitConstant(std::move(value));
  if (index <= UINT8_MAX) {
    this->emit1byteIns(OpCode::LOAD_CONST, index);
  } else if (index <= UINT16_MAX) {
    this->emit2byteIns(OpCode::LOAD_CONST2, index);
  } else {
    this->emit4byteIns(OpCode::LOAD_CONST4, index);
  }
}

void ByteCodeGenerator::emitString(std::string &&value) {
  switch (value.size()) {
  case 0:
    this->emit0byteIns(OpCode::PUSH_STR0);
    break;
  case 1:
    this->emit1byteIns(OpCode::PUSH_STR1, value[0]);
    break;
  case 2:
    this->emit2byteIns(OpCode::PUSH_STR2, value[0], value[1]);
    break;
  case 3:
    this->emit3byteIns(OpCode::PUSH_STR3, value[0], value[1], value[2]);
    break;
  default:
    this->emitLdcIns(Value::createStr(std::move(value)));
    break;
  }
}

void ByteCodeGenerator::emitToString() {
  if (this->handle_STR == nullptr) {
    this->handle_STR = this->typePool.lookupMethod(this->typePool.get(TYPE::Any), OP_STR);
  }
  this->emitMethodCallIns(*this->handle_STR);
}

void ByteCodeGenerator::emitBranchIns(OpCode op, const Label &label) {
  const unsigned int index = this->currentCodeOffset(); // FIXME: check index range
  this->emit2byteIns(op, 0);
  this->curBuilder().writeLabel(index + 1, label, index, CodeEmitter<true>::LabelTarget::_16);
}

void ByteCodeGenerator::emitForkIns(ForkKind kind, const Label &label) {
  const unsigned int offset = this->currentCodeOffset();
  this->emitIns(OpCode::FORK);
  this->curBuilder().append8(toUnderlying(kind));
  this->curBuilder().append16(0);
  this->curBuilder().writeLabel(offset + 2, label, offset, CodeEmitter<true>::LabelTarget::_16);
}

void ByteCodeGenerator::emitJumpIns(const Label &label, OpCode op) {
  assert(op == OpCode::GOTO || op == OpCode::JUMP_LOOP || op == OpCode::JUMP_LOOP_V ||
         op == OpCode::JUMP_TRY || op == OpCode::JUMP_TRY_V || op == OpCode::ENTER_FINALLY);
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
  const unsigned int lineNum = this->curBuilder().lexer->getLineNumByPos(pos);
  if (this->curBuilder().lineNumEntries.empty() ||
      this->curBuilder().lineNumEntries.back().lineNum != lineNum) {
    this->curBuilder().lineNumEntries.push_back({index, lineNum});
  }
}

void ByteCodeGenerator::catchException(const Label &begin, const Label &end, const Type &type,
                                       unsigned short localOffset, unsigned short localSize,
                                       unsigned int level) {
  if (begin->getIndex() == end->getIndex()) {
    return;
  }
  const unsigned int index = this->currentCodeOffset();
  this->curBuilder().catchBuilders.emplace_back(begin, end, type, index, localOffset, localSize,
                                                level);
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
    if (auto &e = this->tryFinallyLabels()[size - 1 - index]; e.finallyLabel) {
      this->enterFinally(e.finallyLabel);
    }
  }
}

static bool isTilde(const CmdArgNode &node, unsigned int startIndex,
                    const ByteCodeGenerator::ConcatCmdArgParam param) {
  if (auto &e = *node.getSegmentNodes()[startIndex]; isExpandingTilde(e)) {
    if (node.isRightHandSide()) { // AA=$PATH:~/:~/
      return true;
    }
    if (param.tildeCount == 0 && param.assignCount <= 1) { // echo ~/
      /**
       * echo ~/
       * echo if=~
       */
      return true;
    }
  }
  return false;
}

unsigned int ByteCodeGenerator::concatCmdArgSegment(const CmdArgNode &node, unsigned int startIndex,
                                                    ConcatCmdArgParam &param) {
  const unsigned int size = node.getSegmentNodes().size();
  const unsigned int baseIndex = startIndex;
  const bool tilde = isTilde(node, baseIndex, param);
  for (; startIndex < size; startIndex++) {
    if (startIndex - baseIndex > 0 && isTilde(node, startIndex, param)) {
      break; // break next tilde
    }
    auto &segNode = *node.getSegmentNodes()[startIndex];
    if (isExpandingWildCard(segNode)) {
      switch (auto &wildNode = cast<WildCardNode>(segNode); wildNode.meta) {
      case ExpandMeta::TILDE:
        param.tildeCount++;
        wildNode.setExpand(false); // emit '~'
        break;
      case ExpandMeta::ASSIGN:
        param.assignCount++;
        wildNode.setExpand(false); // emit '='
        break;
      case ExpandMeta::COLON:
        wildNode.setExpand(false); // emit ':'
        break;
      default:
        break;
      }
    }
    this->generateConcat(segNode, startIndex - baseIndex > 0);
  }
  if (baseIndex > 0) { // concat
    this->emit0byteIns(tilde ? OpCode::APPEND_TILDE : OpCode::CONCAT);
  } else if (tilde) {
    this->emit0byteIns(OpCode::EXPAND_TILDE);
  }
  return startIndex;
}

static Value toPipelineDesc(const Lexer &lexer, const PipelineNode &node) {
  std::string value;
  for (auto &e : node.getNodes()) {
    if (!value.empty()) {
      value += '\0';
    }
    formatJobDesc(lexer.toStrRef(e->getToken()), value);
  }
  return Value::createStr(std::move(value));
}

void ByteCodeGenerator::generatePipeline(const PipelineNode &node, ForkKind forkKind) {
  const bool lastPipe = node.isLastPipe();
  const unsigned int size = node.getNodes().size() - (lastPipe ? 1 : 0);
  const unsigned int labelSize = node.getNodes().size() + (lastPipe ? 0 : 1);

  // init label
  std::vector<Label> labels(labelSize);
  for (unsigned int i = 0; i < labelSize; i++) {
    labels[i] = makeLabel();
  }

  // generate pipeline
  this->emitSourcePos(node.getActualPos());
  this->emitLdcIns(toPipelineDesc(*this->curBuilder().lexer, node));
  this->emitPipelineIns(labels, lastPipe, forkKind);

  auto begin = makeLabel();
  auto end = makeLabel();

  // generate pipeline (child)
  this->markLabel(begin);
  for (unsigned int i = 0; i < size; i++) {
    if (i > 0) {
      this->emit0byteIns(OpCode::TERM_HOOK);
      this->emit0byteIns(OpCode::SUBSHELL_EXIT);
    }
    this->markLabel(labels[i]);
    this->visit(*node.getNodes()[i], CmdCallCtx::STMT);
  }
  this->markLabel(end);
  this->guardChildProc(begin, end, this->typePool.get(TYPE::ProcGuard_));
  this->emit0byteIns(OpCode::TERM_HOOK);
  this->emit0byteIns(OpCode::SUBSHELL_EXIT);

  this->markLabel(labels.back());

  if (lastPipe) { // generate last pipe
    const bool needReclaim = !node.getNodes().back()->getType().isNothingType();
    this->generateBlock(node.getBaseIndex(), 1, needReclaim, [&] {
      this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());
      this->visit(*node.getNodes().back(), CmdCallCtx::AUTO);
      if (needReclaim) {
        this->emit1byteIns(OpCode::SYNC_PIPESTATUS, node.getBaseIndex());
      }
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
  this->emitIns(lastPipe                          ? OpCode::PIPELINE_LP
                : forkKind == ForkKind::NONE      ? OpCode::PIPELINE_SILENT
                : forkKind == ForkKind::PIPE_FAIL ? OpCode::PIPELINE
                                                  : OpCode::PIPELINE_ASYNC);
  if (forkKind != ForkKind::NONE && forkKind != ForkKind::PIPE_FAIL) {
    offset++;
    this->curBuilder().append(toUnderlying(forkKind));
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

void ByteCodeGenerator::generateConcat(Node &node, bool fragment) {
  switch (node.getNodeKind()) {
  case NodeKind::String:
    if (cast<StringNode>(node).getValue().empty() && fragment) {
      return;
    }
    break;
  case NodeKind::StringExpr: {
    const auto &strExprNode = cast<StringExprNode>(node);
    const unsigned int size = strExprNode.getExprNodes().size();
    if (!fragment) {
      if (size == 0 || !isa<StringNode>(*strExprNode.getExprNodes()[0])) {
        this->emit0byteIns(OpCode::PUSH_STR0);
        fragment = true;
      }
    }
    for (unsigned int i = 0; i < size; i++) {
      this->generateConcat(*strExprNode.getExprNodes()[i], fragment || i > 0);
    }
    return;
  }
  case NodeKind::BinaryOp:
    if (isBinaryStrConcat(node)) {
      const auto &binaryNode = cast<BinaryOpNode>(node);
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
    if (cast<EmbedNode>(node).getKind() == EmbedNode::STR_EXPR) {
      this->visit(exprNode);
      assert(fragment);
      this->emit0byteIns(OpCode::INTERPOLATE);
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

void ByteCodeGenerator::visit(Node &node, CmdCallCtx cmdCallCtx) {
  bool stmt = false;
  switch (cmdCallCtx) {
  case CmdCallCtx::STMT:
    stmt = true;
    break;
  case CmdCallCtx::EXPR:
    stmt = false;
    break;
  case CmdCallCtx::AUTO:
    stmt = this->inStmtCtx();
    break;
  }

  this->curBuilder().cmdCallCtxs.push_back(stmt);
  node.accept(*this);
  this->curBuilder().cmdCallCtxs.pop_back();
}

// visitor api
void ByteCodeGenerator::visit(Node &node) { this->visit(node, CmdCallCtx::EXPR); }

void ByteCodeGenerator::visitTypeNode(TypeNode &) { fatal("unsupported\n"); }

void ByteCodeGenerator::visitNumberNode(NumberNode &node) {
  switch (node.kind) {
  case NumberNode::Int:
    this->emitInt(node.getIntValue());
    break;
  case NumberNode::Float: {
    auto value = Value::createFloat(node.getFloatValue());
    this->emitLdcIns(std::move(value));
    break;
  }
  case NumberNode::Signal:
    assert(node.getIntValue() >= 0 && node.getIntValue() <= UINT8_MAX);
    this->emit1byteIns(OpCode::PUSH_SIG, node.getIntValue());
    break;
  case NumberNode::Bool: // normally unreachable
    this->emit0byteIns(node.getIntValue() ? OpCode::PUSH_TRUE : OpCode::PUSH_FALSE);
    break;
  case NumberNode::None: // normally unreachable
    this->emit0byteIns(OpCode::PUSH_INVALID);
    break;
  }
}

void ByteCodeGenerator::visitStringNode(StringNode &node) {
  if (node.isEscaped()) {
    auto value = Value::createStr(node.takeValue());
    const unsigned int index = this->emitConstant(std::move(value));
    this->emitLdcIns(Value::createNum(index));
  } else {
    this->emitString(node.takeValue());
  }
}

void ByteCodeGenerator::visitStringExprNode(StringExprNode &node) { this->generateConcat(node); }

void ByteCodeGenerator::visitRegexNode(RegexNode &node) {
  this->emitLdcIns(Value::create<RegexObject>(node.extractRE()));
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
  if (node.getExtraOp() == VarNode::CUR_ARG0) {
    this->emitLoadSpecialIns(SpecialVarKind::CUR_ARG0);
    return;
  }

  this->emitSourcePos(node.getActualPos());
  if (node.getHandle()->is(HandleKind::ENV)) {
    if (node.hasAttr(HandleAttr::GLOBAL)) {
      this->emit2byteIns(OpCode::LOAD_GLOBAL, node.getIndex());
    } else if (node.hasAttr(HandleAttr::UPVAR)) {
      this->emit1byteIns(OpCode::LOAD_UPVAR, node.getIndex());
    } else if (node.hasAttr(HandleAttr::BOXED)) {
      this->emit1byteIns(OpCode::LOAD_BOXED, node.getIndex());
    } else {
      this->emit1byteIns(OpCode::LOAD_LOCAL, node.getIndex());
    }

    this->emit0byteIns(OpCode::LOAD_ENV);
  } else if (node.getHandle()->is(HandleKind::MOD_CONST)) {
    this->emitLoadSpecialIns(SpecialVarKind::CUR_MOD);

    const unsigned int index = node.getIndex();
    const char *op = index == toIndex(BuiltinVarOffset::SCRIPT_NAME)  ? METHOD_SCRIPT_NAME
                     : index == toIndex(BuiltinVarOffset::SCRIPT_DIR) ? METHOD_SCRIPT_DIR
                                                                      : "";
    auto *handle = this->typePool.lookupMethod(this->typePool.get(TYPE::Module), op);
    this->emitMethodCallIns(*handle);
  } else if (node.getHandle()->is(HandleKind::SMALL_CONST)) {
    ConstEntry entry(node.getIndex());
    switch (entry.data.k) {
    case ConstEntry::BOOL:
      this->emit0byteIns(entry.data.v ? OpCode::PUSH_TRUE : OpCode::PUSH_FALSE);
      break;
    case ConstEntry::SIG:
      this->emit1byteIns(OpCode::PUSH_SIG, entry.data.v);
      break;
    case ConstEntry::NONE:
      this->emit0byteIns(OpCode::PUSH_INVALID);
      break;
    }
  } else {
    const auto index = node.getIndex();
    if (node.hasAttr(HandleAttr::GLOBAL)) {
      if (index == toIndex(BuiltinVarOffset::MODULE)) {
        this->emitLoadSpecialIns(SpecialVarKind::CUR_MOD);
      } else if (index == toIndex(BuiltinVarOffset::RANDOM)) {
        this->emitLoadSpecialIns(SpecialVarKind::RAND);
      } else if (index == toIndex(BuiltinVarOffset::SECONDS)) {
        this->emitLoadSpecialIns(SpecialVarKind::SECOND);
      } else if (index == toIndex(BuiltinVarOffset::THROWN)) {
        this->emitLoadSpecialIns(SpecialVarKind::CUR_THROWN);
      } else if (index == toIndex(BuiltinVarOffset::EXIT_STATUS)) {
        this->emitLoadSpecialIns(SpecialVarKind::EXIT_STATUS);
      } else {
        this->emit2byteIns(OpCode::LOAD_GLOBAL, index);
      }
    } else if (node.hasAttr(HandleAttr::UPVAR)) {
      this->emit1byteIns(OpCode::LOAD_UPVAR, index);
    } else if (node.hasAttr(HandleAttr::BOXED)) {
      this->emit1byteIns(OpCode::LOAD_BOXED, index);
    } else {
      this->emit1byteIns(OpCode::LOAD_LOCAL, index);
    }
  }

  switch (node.getExtraOp()) {
  case VarNode::ARGS_LEN: {
    auto *handle = this->typePool.lookupMethod(this->typePool.get(TYPE::StringArray), "size");
    assert(handle);
    this->emitMethodCallIns(*handle);
    break;
  }
  case VarNode::POSITIONAL_ARG:
    if (node.getExtraValue() > 0) {
      this->emitInt(static_cast<int64_t>(node.getExtraValue()));
      this->emit0byteIns(OpCode::GET_POS_ARG);
    }
    break;
  case VarNode::NONE:
  case VarNode::CUR_ARG0:
    break;
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
  if (node.getHandle()->is(HandleKind::ENV)) {
    this->emit0byteIns(OpCode::LOAD_ENV);
  }
}

static CmdCallCtx getCallCtx(TypeOpNode::OpKind kind) {
  if (kind == TypeOpNode::TO_VOID || kind == TypeOpNode::PRINT) {
    return CmdCallCtx::STMT;
  }
  return CmdCallCtx::EXPR;
}

void ByteCodeGenerator::visitTypeOpNode(TypeOpNode &node) {
  this->visit(node.getExprNode(), getCallCtx(node.getOpKind()));

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
    this->emitMethodCallIns(*handle);
    break;
  }
  case TypeOpNode::TO_STRING:
    this->emitSourcePos(node.getActualPos());
    this->emitToString();
    break;
  case TypeOpNode::TO_BOOL: {
    this->emitSourcePos(node.getActualPos());
    auto *handle = this->typePool.lookupMethod(node.getExprNode().getType(), OP_BOOL);
    assert(handle != nullptr);
    this->emitMethodCallIns(*handle);
    break;
  }
  case TypeOpNode::CHECK_CAST:
    this->emitSourcePos(node.getActualPos());
    this->emitTypeIns(OpCode::CHECK_CAST, node.getType());
    break;
  case TypeOpNode::CHECK_CAST_OPT:
    this->emitSourcePos(node.getActualPos());
    this->emitTypeIns(OpCode::CHECK_CAST_OPT, node.getType());
    break;
  case TypeOpNode::CHECK_UNWRAP:
    this->emitSourcePos(node.getActualPos());
    this->emit0byteIns(OpCode::CHECK_INVALID);
    break;
  case TypeOpNode::PRINT: {
    this->emitSourcePos(node.getActualPos());
    auto &exprType = node.getExprNode().getType();
    if (exprType.isOptionType()) {
      auto &elementType = cast<OptionType>(exprType).getElementType();

      auto thenLabel = makeLabel();
      auto mergeLabel = makeLabel();
      this->emitBranchIns(OpCode::IF_NOT_INVALID, thenLabel);
      this->emitLdcIns(Value::createStr("(invalid)"));
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
    if (node.getAssertOp() == AssertOp::IS) {
      this->emitTypeIns(OpCode::PUSH_TYPE, node.getTargetTypeNode()->getType());
    } else {
      this->emit0byteIns(OpCode::POP);
    }
    this->emit0byteIns(OpCode::PUSH_FALSE);
    break;
  case TypeOpNode::ALWAYS_TRUE:
    this->emit0byteIns(OpCode::POP);
    this->emit0byteIns(OpCode::PUSH_TRUE);
    break;
  case TypeOpNode::INSTANCEOF:
    this->emitTypeIns(OpCode::PUSH_TYPE, node.getTargetTypeNode()->getType());
    if (node.getAssertOp() == AssertOp::IS) {
      this->emit0byteIns(OpCode::DUP2);
    }
    this->emit0byteIns(OpCode::INSTANCE_OF);
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
  const auto kind = node.getOp();
  if (kind == TokenKind::COND_AND || kind == TokenKind::COND_OR) {
    auto elseLabel = makeLabel();
    auto mergeLabel = makeLabel();

    this->visit(*node.getLeftNode());
    this->emitBranchIns(elseLabel);

    if (kind == TokenKind::COND_AND) {
      this->visit(*node.getRightNode(), CmdCallCtx::AUTO);
      this->emitJumpIns(mergeLabel);

      this->markLabel(elseLabel);
      this->emit0byteIns(OpCode::PUSH_FALSE);
    } else {
      this->emit0byteIns(OpCode::PUSH_TRUE);
      this->emitJumpIns(mergeLabel);

      this->markLabel(elseLabel);
      this->visit(*node.getRightNode(), CmdCallCtx::AUTO);
    }

    this->markLabel(mergeLabel);
  } else if (kind == TokenKind::NULL_COALE) {
    auto mergeLabel = makeLabel();

    this->visit(*node.getLeftNode());
    this->emitBranchIns(OpCode::IF_NOT_INVALID, mergeLabel);

    this->visit(*node.getRightNode(), CmdCallCtx::AUTO);
    this->markLabel(mergeLabel);
  } else if (kind == TokenKind::ADD && node.getLeftNode() &&
             node.getLeftNode()->getType().is(TYPE::String)) {
    if (isa<EmptyNode>(node.getLeftNode())) {
      this->visit(*node.getRightNode());
      this->emit0byteIns(OpCode::APPEND);
    } else {
      this->generateConcat(node);
    }
  } else {
    assert(node.getOptNode());
    this->visit(*node.getOptNode());
  }
}

void ByteCodeGenerator::visitArgsNode(ArgsNode &node) {
  const unsigned int argSize = node.getNodes().size();
  const unsigned int namedArgStartOffset =
      node.hasNamedArgs() ? node.getNamedEntries()[0].getOffset() : argSize;
  unsigned int index = 0;
  for (; index < namedArgStartOffset; index++) {
    this->visit(*node.getNodes()[index]);
  }
  const unsigned int noneCount = node.getNoneCount();
  for (unsigned int i = 0; i < noneCount; i++) {
    this->emit0byteIns(OpCode::PUSH_INVALID);
  }
  /**
   * | expr | arg1 | arg2 | none | none | (arg4) | (arg3)
   * => arg4:offset = 0, arg3:offset = 1
   */
  for (; index < argSize; index++) {
    this->visit(*node.getNodes()[index]);
    const unsigned int paramIndex =
        node.getNamedEntries()[index - namedArgStartOffset].getParamIndex();
    this->emit1byteIns(OpCode::STORE_BY_OFFSET, noneCount + namedArgStartOffset - paramIndex - 1);
  }
}

void ByteCodeGenerator::visitApplyNode(ApplyNode &node) {
  if (node.getAttr() == ApplyNode::MAP_ITER_NEXT_KEY) {
    this->visit(node.getRecvNode());
    this->emitBranchIns(OpCode::MAP_ITER_NEXT, this->peekLoopLabels().breakLabel);
  } else if (node.getAttr() == ApplyNode::MAP_ITER_NEXT_VALUE) {
    // do nothing
  } else if (node.isMethodCall()) {
    this->visit(node.getRecvNode());
    if (node.getAttr() == ApplyNode::NEW_ITER && node.getRecvNode().getType().isOptionType()) {
      this->emitBranchIns(OpCode::IF_INVALID, this->curBuilder().forInBreakLabels.back());
    }
    this->visit(node.getArgsNode());
    if (node.getAssertOp() != AssertOp::DEFAULT) {
      this->emit0byteIns(OpCode::DUP2);
    }
    this->emitSourcePos(node.getActualPos());
    this->emitMethodCallIns(*node.getHandle());
    if (node.getAttr() == ApplyNode::ITER_NEXT) {
      this->emitBranchIns(OpCode::ITER_HAS_NEXT, this->peekLoopLabels().breakLabel);
    }
  } else {
    this->visit(node.getExprNode());
    this->visit(node.getArgsNode());
    this->emitSourcePos(node.getActualPos());
    this->emitFuncCallIns(cast<FunctionType>(node.getExprNode().getType()));
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
    auto &handle = *node.getHandle();
    assert(handle.isNative() || handle.isConstructor());
    if (handle.isNative()) {
      this->emitTypeIns(OpCode::NEW, node.getType());
    }

    // push arguments
    this->visit(node.getArgsNode());

    // call constructor
    this->emitSourcePos(node.getActualPos());
    this->emitMethodCallIns(handle);
    break;
  }
}

void ByteCodeGenerator::visitEmbedNode(EmbedNode &node) {
  this->visit(node.getExprNode());
  if (node.getHandle() != nullptr) {
    this->emitSourcePos(node.getPos());
    this->emitMethodCallIns(*node.getHandle());
  }
}

void ByteCodeGenerator::visitCmdNode(CmdNode &node) {
  if (node.getNameNode().getValue().empty()) { // io redirection within substitution
    this->emitString("__gets");
  } else {
    this->visit(node.getNameNode());
  }
  if (node.getNameNode().isTilde()) {
    this->emit0byteIns(OpCode::EXPAND_TILDE);
  }

  this->emit0byteIns(OpCode::NEW_CMD);
  this->emit0byteIns(node.hasRedir() ? OpCode::NEW_REDIR : OpCode::PUSH_NULL);

  for (auto &argNode : node.getArgNodes()) {
    this->visit(*argNode);
  }

  this->emitSourcePos(node.getActualPos());
  if (node.hasRedir()) {
    this->emit0byteIns(OpCode::DO_REDIR);
  }

  if (node.getHandle()) { // user-defined command
    OpCode ins = this->inStmtCtx() ? OpCode::CALL_UDC : OpCode::CALL_UDC_SILENT;
    this->emit2byteIns(ins, node.getHandle()->getIndex());
  } else {
    OpCode ins = OpCode::CALL_CMD_NOFORK;
    if (node.getNeedFork()) {
      ins = this->inStmtCtx() ? OpCode::CALL_CMD : OpCode::CALL_CMD_SILENT;
    }
    this->emit0byteIns(ins);
  }
}

void ByteCodeGenerator::visitCmdArgNode(CmdArgNode &node) {
  if (node.isBraceExpansion() || node.isGlobExpansion()) {
    this->emitSourcePos(node.getPos());
    const unsigned int size = node.getSegmentNodes().size();
    unsigned int firstIndex = 0;
    for (unsigned int i = 0; i < size; i++) {
      auto &e = node.getSegmentNodes()[i];
      if (isExpandingWildCard(*e) || isEscapedStr(*e)) {
        this->visit(*e);
        firstIndex = i + 1;
      } else {
        this->generateConcat(*e, i > firstIndex);
      }
    }
    this->emit0byteIns(OpCode::PUSH_NULL); // sentinel
    assert(node.getExpansionSize() <= SYS_LIMIT_EXPANSION_FRAG_NUM);
    const auto op = node.isBraceExpansion() ? ExpandOp::BRACE : ExpandOp::GLOB;
    this->emitExpandIns(node.getExpansionSize(), op);
  } else {
    this->generateCmdArg(node);
    this->emit0byteIns(OpCode::ADD_CMD_ARG);
  }
}

void ByteCodeGenerator::visitArgArrayNode(ArgArrayNode &node) {
  this->emitTypeIns(OpCode::NEW, node.getType());
  this->emit0byteIns(OpCode::PUSH_INVALID);
  for (auto &argNode : node.getCmdArgNodes()) {
    this->visit(*argNode);
  }
  this->emit0byteIns(OpCode::POP);
}

void ByteCodeGenerator::visitRedirNode(RedirNode &node) {
  if (const int newFd = node.getNewFd(); newFd >= 0 && newFd < RESERVED_FD_LIMIT) {
    if (const int targetFd = node.getTargetFd(); targetFd >= -1 && targetFd < RESERVED_FD_LIMIT) {
      if (targetFd < 0) {
        this->emit0byteIns(OpCode::PUSH_INVALID);
      } else {
        this->emitInt(targetFd);
      }
    } else {
      this->generateCmdArg(node.getTargetNode());
    }
    if (newFd < 3) {
      constexpr auto offset = toUnderlying(OpCode::ADD_REDIR_OP0);
      const auto op = static_cast<OpCode>(offset + newFd);
      this->emit1byteIns(op, toUnderlying(node.getRedirOp()));
    } else {
      this->emit2byteIns(OpCode::ADD_REDIR_OP, toUnderlying(node.getRedirOp()),
                         static_cast<unsigned char>(newFd));
    }
  } else {
    fatal("unsupported\n");
  }
}

void ByteCodeGenerator::visitWildCardNode(WildCardNode &node) {
  if (node.isExpand()) {
    this->emit2byteIns(OpCode::PUSH_META, toUnderlying(node.meta),
                       static_cast<unsigned char>(node.getBraceId()));
  } else {
    this->emitString(toString(node.meta));
  }
}

void ByteCodeGenerator::visitBraceSeqNode(BraceSeqNode &node) {
  /**
   * (begin, end, step, (digits, kind))
   */
  auto value = Value::create<BaseObject>(this->typePool.get(TYPE::Any), 4);
  auto &obj = typeAs<BaseObject>(value);
  auto &range = node.getRange();
  obj[0] = Value::createInt(range.begin);
  obj[1] = Value::createInt(range.end);
  obj[2] = Value::createInt(range.step);
  obj[3] = Value::createBraceRangeAttr(range.kind, range.digits);
  this->emitLdcIns(std::move(value));
}

void ByteCodeGenerator::visitPipelineNode(PipelineNode &node) {
  this->generatePipeline(node, this->inStmtCtx() ? ForkKind::PIPE_FAIL : ForkKind::NONE);
}

void ByteCodeGenerator::visitWithNode(WithNode &node) {
  this->generateBlock(node.getBaseIndex(), 1, true, [&] {
    this->emit0byteIns(OpCode::NEW_REDIR);
    for (auto &e : node.getRedirNodes()) {
      this->visit(*e);
    }
    this->emitSourcePos(node.getActualPos());
    this->emit0byteIns(OpCode::DO_REDIR);
    this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());

    this->visit(node.getExprNode(), CmdCallCtx::AUTO);
  });
}

void ByteCodeGenerator::visitTimeNode(TimeNode &node) {
  this->generateBlock(node.getBaseIndex(), 1, true, [&] {
    this->emit0byteIns(OpCode::NEW_TIMER);
    this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());
    this->visit(node.getExprNode(), CmdCallCtx::AUTO);
  });
}

static Value toForkDesc(const Lexer &lexer, const ForkNode &node) {
  std::string value;
  formatJobDesc(lexer.toStrRef(node.getExprNode().getToken()), value);
  return Value::createStr(std::move(value));
}

void ByteCodeGenerator::visitForkNode(ForkNode &node) {
  if (isa<PipelineNode>(node.getExprNode()) && !node.isCmdSubOrSubShell()) {
    this->generatePipeline(cast<PipelineNode>(node.getExprNode()), node.getOpKind());
  } else {
    auto beginLabel = makeLabel();
    auto endLabel = makeLabel();
    auto mergeLabel = makeLabel();

    this->emitSourcePos(node.getActualPos());
    this->emitLdcIns(toForkDesc(*this->curBuilder().lexer, node));
    auto forkKind = node.getOpKind();
    if (forkKind == ForkKind::NONE && this->inStmtCtx()) { // for subshell
      forkKind = ForkKind::PIPE_FAIL;
    }
    this->emitForkIns(forkKind, mergeLabel);
    this->markLabel(beginLabel);
    this->visit(node.getExprNode(), CmdCallCtx::STMT);
    this->markLabel(endLabel);

    this->guardChildProc(beginLabel, endLabel, this->typePool.get(TYPE::ProcGuard_));
    this->emit0byteIns(OpCode::TERM_HOOK);
    this->emit0byteIns(OpCode::SUBSHELL_EXIT);
    this->markLabel(mergeLabel);
  }
}

void ByteCodeGenerator::visitAssertNode(AssertNode &node) {
  auto mergeLabel = makeLabel();
  auto elseLabel = makeLabel();
  this->emitBranchIns(OpCode::ASSERT_ENABLED, mergeLabel);

  this->visit(node.getCondNode());
  this->emitBranchIns(OpCode::BRANCH_NOT, elseLabel);

  this->visit(node.getMessageNode());
  this->emitSourcePos(node.getCondNode().getPos());
  const auto assertOp = node.resolveAssertOp();
  if (assertOp == AssertOp::DEFAULT) {
    this->emit0byteIns(OpCode::ASSERT_FAIL);
  } else {
    this->emit1byteIns(OpCode::ASSERT_FAIL2, toUnderlying(assertOp));
  }

  this->markLabel(elseLabel);
  if (assertOp != AssertOp::DEFAULT) {
    this->emit0byteIns(OpCode::POP);
    this->emit0byteIns(OpCode::POP);
  }
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
    const unsigned int size = node.getNodes().size();
    for (unsigned int i = 0; i < size; i++) {
      auto &e = node.getNodes()[i];
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
        if (deferCount == 1) {
          this->emitTryGuard(this->tryFinallyLabels().size());
        }
      } else {
        auto callCtx = i == size - 1 ? CmdCallCtx::AUTO : CmdCallCtx::STMT;
        this->visit(*e, callCtx);
      }
    }

    auto mergeLabel = makeLabel();
    if (deferCount && !node.getNodes().back()->getType().isNothingType()) {
      this->enterMultiFinally(deferCount);
      this->emitJumpIns(mergeLabel,
                        node.getType().isVoidType() ? OpCode::JUMP_TRY : OpCode::JUMP_TRY_V);
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
  // prepare
  {
    auto &e = this->tryFinallyLabels().back();
    this->markLabel(e.finallyLabel);
    this->catchException(e.beginLabel, e.endLabel, this->typePool.get(TYPE::Throwable),
                         e.localOffset, e.localSize, this->tryFinallyLabels().size());
  }
  if (node.getDropLocalSize()) {
    this->emit2byteIns(OpCode::RECLAIM_LOCAL, node.getDropLocalOffset(), node.getDropLocalSize());
  }

  // generate defer/finally body
  auto beginLabel = makeLabel();
  auto endLabel = makeLabel();
  this->markLabel(beginLabel);
  this->visit(node.getBlockNode(), CmdCallCtx::STMT);
  this->markLabel(endLabel);
  /*
   * always catch all exceptions from defer/finally
   */
  this->guardChildProc(beginLabel, endLabel, this->typePool.get(TYPE::ProcGuard_));
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

    this->emitSourcePos(node.getPos());
    this->curBuilder().forInBreakLabels.push_back(breakLabel);
    this->visit(node.getInitNode());
    this->curBuilder().forInBreakLabels.pop_back();
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
      this->emitBranchIns(breakLabel);
    }

    this->markLabel(startLabel);
    this->emit0byteIns(OpCode::LOOP_GUARD);
    this->pushLoopLabels(breakLabel, continueLabel, breakWithValueLabel);
    this->visit(node.getBlockNode(), CmdCallCtx::STMT);
    this->popLoopLabels();

    this->markLabel(breakLabel);
    if (!node.getType().isVoidType()) {
      this->emit0byteIns(OpCode::PUSH_INVALID);
      this->markLabel(breakWithValueLabel);
    }
  });
}

void ByteCodeGenerator::visitIfNode(IfNode &node) {
  auto elseLabel = makeLabel();
  auto mergeLabel = makeLabel();

  switch (node.getIfLetKind()) {
  case IfNode::NOP:
    this->visit(node.getCondNode());
    this->emitBranchIns(elseLabel);
    break;
  case IfNode::ERROR:
    return; // unreachable
  case IfNode::UNWRAP:
    this->visit(node.getIfLetUnwrap());
    this->emitBranchIns(OpCode::IF_INVALID, elseLabel);
    break;
  }
  this->visit(node.getThenNode(), CmdCallCtx::AUTO);
  if (!isEmptyCode(node.getElseNode()) && !node.getThenNode().getType().isNothingType()) {
    this->emitJumpIns(mergeLabel);
  }

  this->markLabel(elseLabel);
  this->visit(node.getElseNode(), CmdCallCtx::AUTO);

  this->markLabel(mergeLabel);
}

static Value newObject(Node &constNode) {
  auto kind = constNode.getNodeKind();
  assert(kind == NodeKind::Number || kind == NodeKind::String);
  if (kind == NodeKind::Number) {
    if (constNode.getType().is(TYPE::Signal)) {
      return Value::createSig(static_cast<int>(cast<NumberNode>(constNode).getIntValue()));
    }
    return Value::createInt(cast<NumberNode>(constNode).getIntValue());
  }
  return Value::createStr(cast<StringNode>(constNode).getValue());
}

void ByteCodeGenerator::generateMapCase(CaseNode &node) {
  bool hasDefault = node.hasDefault();
  auto mergeLabel = makeLabel();
  auto elseLabel = makeLabel();
  auto value = Value::create<OrderedMapObject>(this->typePool.get(TYPE::Void),
                                               reinterpret_cast<uintptr_t>(&node));
  auto &map = typeAs<OrderedMapObject>(value);

  this->emitLdcIns(value);
  this->visit(node.getExprNode());
  this->emit0byteIns(OpCode::LOOKUP_HASH);

  if (hasDefault) {
    this->emitJumpIns(elseLabel);
  } else {
    this->emitJumpIns(mergeLabel);
  }

  // generate case arm
  const Type *prevType = nullptr;
  for (auto &armNode : node.getArmNodes()) {
    if (prevType != nullptr && !prevType->isNothingType()) {
      this->emitJumpIns(mergeLabel);
    }
    if (armNode->isDefault()) {
      this->markLabel(elseLabel);
    } else {
      this->generateCaseLabels(*armNode, map);
    }
    this->visit(*armNode, CmdCallCtx::AUTO);
    prevType = &armNode->getType();
  }

  this->markLabel(mergeLabel);
}

void ByteCodeGenerator::generateCaseLabels(const ArmNode &node, OrderedMapObject &obj) const {
  unsigned int offset = this->currentCodeOffset();
  for (auto &e : node.getConstPatternNodes()) {
    auto pair = obj.insert(newObject(*e), Value::createNum(offset));
    if (pair.second != OrderedMapObject::InsertStatus::OK) {
      fatal("map insertion failed\n");
    }
  }
}

void ByteCodeGenerator::generateIfElseCase(CaseNode &node) {
  const auto *exprType = &node.getExprNode().getType();
  if (exprType->isOptionType()) {
    exprType = &cast<OptionType>(exprType)->getElementType();
  }
  assert(exprType->is(TYPE::String));

  // generate expr
  this->visit(node.getExprNode());

  // generate if-else-chain
  auto &eqHandle = *this->typePool.lookupMethod(*exprType, OP_EQ);
  auto &matchHandle = *this->typePool.lookupMethod(*exprType, OP_MATCH);

  int defaultIndex = -1;
  auto defaultLabel = makeLabel();
  auto mergeLabel = makeLabel();
  if (node.getExprNode().getType().isOptionType()) {
    this->emit0byteIns(OpCode::DUP);
    this->emit0byteIns(OpCode::CHECK_INVALID);
    this->emitBranchIns(defaultLabel);
  }
  for (unsigned int index = 0; index < node.getArmNodes().size(); index++) {
    if (node.getArmNodes()[index]->isDefault()) {
      defaultIndex = static_cast<int>(index);
      continue;
    }
    this->generateIfElseArm(*node.getArmNodes()[index], eqHandle, matchHandle, mergeLabel);
  }
  this->markLabel(defaultLabel);
  if (defaultIndex > -1) {           // generate default
    this->emit0byteIns(OpCode::POP); // pop stack top 'expr'
    this->visit(*node.getArmNodes()[static_cast<unsigned int>(defaultIndex)], CmdCallCtx::AUTO);
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
    this->emitMethodCallIns(patternNode->getType().is(TYPE::String) ? eqHandle : matchHandle);
  }

  // generate arm action
  this->markLabel(armMerge);
  this->emitBranchIns(armElse);
  this->emit0byteIns(OpCode::POP); // pop stack top 'expr'
  this->visit(node, CmdCallCtx::AUTO);
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

void ByteCodeGenerator::visitArmNode(ArmNode &node) {
  this->visit(node.getActionNode(), CmdCallCtx::AUTO);
}

void ByteCodeGenerator::generateBreakContinue(JumpNode &node) {
  assert(!this->curBuilder().loopLabels.empty());

  // for break with value
  this->visit(node.getExprNode());

  // reclaim local and enter finally before jump
  const unsigned int blockIndex = this->curBuilder().loopLabels.back().blockIndex;
  const unsigned int startOffset = this->curBuilder().localVars[blockIndex].offset;
  const unsigned int stopOffset =
      this->curBuilder().localVars.back().offset + this->curBuilder().localVars.back().size;

  assert(startOffset <= stopOffset);
  this->enterMultiFinally(node.getTryDepth(), startOffset, stopOffset - startOffset);

  if (node.getOpKind() == JumpNode::BREAK) {
    if (node.getExprNode().getType().isVoidType()) {
      this->emitJumpIns(this->peekLoopLabels().breakLabel, OpCode::JUMP_LOOP);
    } else {
      this->emitJumpIns(this->peekLoopLabels().breakWithValueLabel, OpCode::JUMP_LOOP_V);
    }
  } else {
    assert(node.getExprNode().getType().isVoidType());
    this->emitJumpIns(this->peekLoopLabels().continueLabel, OpCode::JUMP_LOOP);
  }
}

void ByteCodeGenerator::visitJumpNode(JumpNode &node) {
  switch (node.getOpKind()) {
  case JumpNode::BREAK:
  case JumpNode::CONTINUE:
  case JumpNode::IMPLICIT_CONTINUE:
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
      assert(this->typePool.get(TYPE::Int).isSameOrBaseTypeOf(node.getExprNode().getType()));
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
    this->visit(node.getBlockNode(), CmdCallCtx::AUTO);
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
  this->emitTryGuard(this->tryFinallyLabels().size());
  this->visit(node.getExprNode(), CmdCallCtx::AUTO);
  this->markLabel(endLabel);
  if (!node.getExprNode().getType().isNothingType()) {
    if (hasFinally) {
      this->enterFinally(finallyLabel);
    }
    this->emitJumpIns(mergeLabel, node.getExprNode().getType().isVoidType() ? OpCode::JUMP_TRY
                                                                            : OpCode::JUMP_TRY_V);
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
                         blockNode.getMaxVarSize(), this->tryFinallyLabels().size());
    this->visit(*catchNode, CmdCallCtx::AUTO);
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
    this->emitLdcIns(Value::createStr(node.getVarName()));
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
    this->emitLdcIns(Value::createStr(node.getVarName()));
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
    if (node.getHandle()->has(HandleAttr::BOXED)) {
      this->emit1byteIns(OpCode::BOX_LOCAL, node.getVarIndex());
    }
  }
}

void ByteCodeGenerator::visitAttributeNode(AttributeNode &) {} // do nothing

void ByteCodeGenerator::visitAssignNode(AssignNode &node) {
  auto &assignableNode = cast<AssignableNode>(node.getLeftNode());
  this->emitSourcePos(node.getActualPos());
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

    if (accessNode.getHandle()->is(HandleKind::ENV)) {
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

    if (varNode.getHandle()->is(HandleKind::ENV)) {
      if (varNode.hasAttr(HandleAttr::GLOBAL)) {
        this->emit2byteIns(OpCode::LOAD_GLOBAL, index);
      } else if (varNode.hasAttr(HandleAttr::UPVAR)) {
        this->emit1byteIns(OpCode::LOAD_UPVAR, index);
      } else if (varNode.hasAttr(HandleAttr::BOXED)) {
        this->emit1byteIns(OpCode::LOAD_BOXED, index);
      } else {
        this->emit1byteIns(OpCode::LOAD_LOCAL, index);
      }
      this->emit0byteIns(OpCode::SWAP);
      this->emit0byteIns(OpCode::STORE_ENV);
    } else {
      if (varNode.hasAttr(HandleAttr::GLOBAL)) {
        if (index == toIndex(BuiltinVarOffset::SECONDS)) {
          this->emitStoreSpecialIns(SpecialVarKind::SECOND);
        } else if (index == toIndex(BuiltinVarOffset::EXIT_STATUS)) {
          this->emitStoreSpecialIns(SpecialVarKind::EXIT_STATUS);
        } else {
          this->emit2byteIns(OpCode::STORE_GLOBAL, index);
        }
      } else if (varNode.hasAttr(HandleAttr::UPVAR)) {
        this->emit1byteIns(OpCode::STORE_UPVAR, index);
      } else if (varNode.hasAttr(HandleAttr::BOXED)) {
        this->emit1byteIns(OpCode::STORE_BOXED, index);
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
        this->emitLdcIns(Value::createStr(leftNode.getVarName()));
        this->emit0byteIns(OpCode::DUP);
        this->emit1byteIns(OpCode::STORE_LOCAL, leftNode.getIndex());
        assert(isa<CmdArgNode>(e->getRightNode()));
        this->generateCmdArg(cast<CmdArgNode>(e->getRightNode()));
        this->emit0byteIns(OpCode::ADD2ENV_CTX);
      }
      this->emit1byteIns(OpCode::STORE_LOCAL, node.getBaseIndex());
      this->visit(*node.getExprNode(), CmdCallCtx::AUTO);
    });
  } else {
    for (auto &e : node.getAssignNodes()) {
      assert(e->getRightNode().getType().is(TYPE::String));
      this->visit(*e);
    }
  }
}

void ByteCodeGenerator::visitFunctionNode(FunctionNode &node) {
  this->initFuncCodeBuilder(CodeKind::FUNCTION, node.getMaxVarNum());
  for (auto &paramNode : node.getParamNodes()) {
    if (paramNode->getHandle()->has(HandleAttr::BOXED)) {
      this->emit1byteIns(OpCode::BOX_LOCAL, paramNode->getVarIndex());
    }
  }
  this->visit(node.getBlockNode(), CmdCallCtx::STMT);

  auto code = this->finalizeCodeBuilder(node.getFuncName());
  if (!code) {
    this->reportError<TooLargeFunc>(node, node.getFuncName().c_str());
  }
  auto func = Value::create<FuncObject>(*node.getResolvedType(), std::move(code));

  this->emitLdcIns(func);
  if (!node.getCaptures().empty()) {
    assert(node.isAnonymousFunc());
    for (auto &e : node.getCaptures()) {
      if (e->has(HandleAttr::UPVAR)) {
        this->emit1byteIns(OpCode::LOAD_RAW_UPVAR, e->getIndex());
      } else {
        this->emit1byteIns(OpCode::LOAD_LOCAL, e->getIndex());
      }
    }
    assert(node.getCaptures().size() <= UINT8_MAX);
    this->emitNewClosureIns(node.getCaptures().size());
  }
  if (!node.isAnonymousFunc()) {
    assert(node.getHandle()->has(HandleAttr::GLOBAL));
    this->emit2byteIns(OpCode::STORE_GLOBAL, node.getHandle()->getIndex());
  }
}

void ByteCodeGenerator::visitUserDefinedCmdNode(UserDefinedCmdNode &node) {
  this->initFuncCodeBuilder(CodeKind::USER_DEFINED_CMD, node.getMaxVarNum());
  if (node.getParamNode()) {
    this->visit(*node.getParamNode()); // var param : CLIType
    this->emit1byteIns(OpCode::LOAD_LOCAL, node.getParamNode()->getVarIndex());
    this->emit0byteIns(OpCode::PARSE_CLI);
  }
  this->visit(node.getBlockNode(), CmdCallCtx::STMT);

  auto code = this->finalizeCodeBuilder(node.getCmdName());
  if (!code) {
    this->reportError<TooLargeUdc>(node, node.getCmdName().c_str());
  }
  auto func = Value::create<FuncObject>(this->typePool.get(TYPE::Command), std::move(code));

  this->emitLdcIns(func);
  if (!node.getCaptures().empty()) {
    assert(node.isAnonymousCmd());
    for (auto &e : node.getCaptures()) {
      if (e->has(HandleAttr::UPVAR)) {
        this->emit1byteIns(OpCode::LOAD_RAW_UPVAR, e->getIndex());
      } else {
        this->emit1byteIns(OpCode::LOAD_LOCAL, e->getIndex());
      }
    }
    assert(node.getCaptures().size() <= UINT8_MAX);
    this->emitNewClosureIns(node.getCaptures().size());
  }
  if (!node.isAnonymousCmd()) {
    assert(node.getHandle()->has(HandleAttr::GLOBAL));
    this->emit2byteIns(OpCode::STORE_GLOBAL, node.getHandle()->getIndex());
  }
}

void ByteCodeGenerator::visitFuncListNode(FuncListNode &node) {
  for (auto &e : node.getNodes()) {
    this->visit(*e);
  }
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
    this->emitTryGuard(this->tryFinallyLabels().size());
  } else {
    this->visit(*node, CmdCallCtx::STMT);
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

  const auto lex = this->curBuilder().lexer;
  auto code = this->finalizeCodeBuilder(modType.toName());
  ObjPtr<FuncObject> func;
  if (code) {
    auto v = Value::create<FuncObject>(modType, std::move(code));
    func = ObjPtr<FuncObject>(&typeAs<FuncObject>(v));
  } else {
    this->reportError<TooLargeToplevel>(token, lex->getSourceName().c_str());
  }
  return func;
}

bool ByteCodeGenerator::exitModule(const SourceNode &node) {
  const auto func =
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

static std::string formatNum(unsigned int maxNum, unsigned int num) {
  unsigned int width = countDigits(maxNum > 0 ? maxNum - 1 : maxNum);
  return padLeft(num, width, ' ');
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

void ByteCodeDumper::dumpCode(const CompiledCode &c) {
  fputs("ARCode: ", this->fp);
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
    constexpr const char *opName[] = {
#define GEN_NAME(CODE, N, S) #CODE,
        OPCODE_LIST(GEN_NAME)
#undef GEN_NAME
    };

    for (unsigned int i = 0; i < c.getCodeSize(); i++) {
      const auto code = static_cast<OpCode>(c.getCode()[i]);
      fprintf(this->fp, "  %s: %s", formatNum(c.getCodeSize(), i).c_str(),
              opName[toUnderlying(code)]);
      if (isTypeOp(code)) {
        unsigned int v = read24(c.getCode(), i + 1);
        i += 3;
        fprintf(this->fp, "  %s", this->typePool.get(v).getName());
      } else {
        const int byteSize = getByteSize(code);
        if (code == OpCode::FORK || code == OpCode::CALL_METHOD) {
          fprintf(this->fp, "  %d  %d", read8(c.getCode(), i + 1), read16(c.getCode(), i + 2));
        } else if (code == OpCode::RECLAIM_LOCAL || code == OpCode::ADD_EXPANDING ||
                   code == OpCode::INIT_FIELDS || code == OpCode::PUSH_META) {
          fprintf(this->fp, "  %d  %d", read8(c.getCode(), i + 1), read8(c.getCode(), i + 2));
        } else if (code == OpCode::CALL_BUILTIN) {
          const unsigned int paramSize = read8(c.getCode(), i + 1);
          const char *name = nativeFuncInfoTable()[read8(c.getCode(), i + 2)].funcName;
          fprintf(this->fp, "  %d  %s", paramSize, name);
        } else if (code == OpCode::PUSH_STR1 || code == OpCode::PUSH_STR2 ||
                   code == OpCode::PUSH_STR3) {
          char data[4];
          const unsigned int size = code == OpCode::PUSH_STR1   ? 1
                                    : code == OpCode::PUSH_STR2 ? 2
                                                                : 3;
          for (unsigned int index = 0; index < size; index++) {
            data[index] = static_cast<char>(read8(c.getCode(), i + 1 + index));
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
            const auto s = static_cast<unsigned int>(read8(c.getCode(), i + 1));
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
      fprintf(this->fp, "  %s: ", formatNum(constSize, i).c_str());
      auto &v = c.getConstPool()[i];
      std::string value = v.toString(this->typePool);
      switch (v.kind()) {
      case ValueKind::NUMBER:
        fprintf(this->fp, "%s", value.c_str());
        break;
      case ValueKind::INVALID:
        break;
      default: {
        const auto &type = this->typePool.get(v.getTypeID());
        if (v.isObject()) {
          if (isa<FuncObject>(v.get())) {
            (type.isModType() ? this->mods : this->funcs)
                .push_back(std::ref(cast<FuncObject>(v.get())->getCode()));
          }
        }
        fprintf(this->fp, "%s %s", type.getName(), toPrintable(value).c_str());
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
      fprintf(this->fp, "  lineNum: %s, address: %s\n", formatNum(maxLineNum, e.lineNum).c_str(),
              formatNum(c.getCodeSize(), e.address).c_str());
    }
  }

  fputs("Exception Table:\n", this->fp);
  for (unsigned int i = 0; c.getExceptionEntries()[i]; i++) {
    const auto &e = c.getExceptionEntries()[i];
    auto &type = this->typePool.get(e.typeId);
    fprintf(this->fp, "  begin: %d, end: %d, type: %s, dest: %d, offset: %d, size: %d, level: %d\n",
            e.begin, e.end, type.getName(), e.dest, e.localOffset, e.localSize, e.guardLevel);
  }

  fputc('\n', this->fp);
  fflush(this->fp);
}

} // namespace arsh
