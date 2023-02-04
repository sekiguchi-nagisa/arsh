/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "misc/glob.hpp"
#include "misc/num_util.hpp"
#include "paths.h"
#include "type_checker.h"

namespace ydsh {

void TypeChecker::visitCmdNode(CmdNode &node) {
  this->checkType(this->typePool.get(TYPE::String), node.getNameNode());
  for (auto &argNode : node.getArgNodes()) {
    this->checkTypeAsExpr(*argNode);
  }
  if (node.getNameNode().getValue() == "exit" || node.getNameNode().getValue() == "_exit") {
    node.setType(this->typePool.get(TYPE::Nothing));
  } else {
    node.setType(this->typePool.get(TYPE::Bool));
    std::string cmdName = toCmdFullName(node.getNameNode().getValue());
    if (auto ret = this->curScope->lookup(cmdName)) {
      auto handle = std::move(ret).take();
      node.setHandle(handle);
      auto &type = this->typePool.get(handle->getTypeId());
      if (type.isFuncType()) { // resolved command may be module object
        auto &returnType = cast<FunctionType>(type).getReturnType();
        assert(returnType.is(TYPE::Int) || returnType.isNothingType());
        if (returnType.isNothingType()) {
          node.setType(returnType);
        }
      }
    }
  }
  if (node.getType().isNothingType() &&
      this->funcCtx->finallyLevel() > this->funcCtx->childLevel()) {
    this->reportError<InsideFinally>(node.getActualToken());
  }
}

void TypeChecker::checkBraceExpansion(CmdArgNode &node) {
  // check balance of brace expansion
  std::vector<std::pair<unsigned int, unsigned int>> stack;
  auto &segmentNodes = node.refSegmentNodes();
  const unsigned int size = segmentNodes.size();
  for (unsigned int i = 0; i < size; i++) {
    auto &e = *segmentNodes[i];
    if (isa<WildCardNode>(e)) {
      auto &wild = cast<WildCardNode>(e);
      switch (wild.meta) {
      case ExpandMeta::BRACE_OPEN:
        stack.emplace_back(i, 0);
        break;
      case ExpandMeta::BRACE_CLOSE:
        if (stack.empty()) {
          this->reportError<BraceUnopened>(wild);
        } else {
          if (stack.back().second) {
            cast<WildCardNode>(*segmentNodes[stack.back().first]).setExpand(true);
            wild.setExpand(true);
            node.setBraceExpansion(true);
          }
          stack.pop_back();
        }
        break;
      case ExpandMeta::BRACE_SEP:
        if (!stack.empty()) {
          wild.setExpand(true);
          stack.back().second++;
        }
        break;
      case ExpandMeta::BRACE_SEQ_OPEN:
      case ExpandMeta::BRACE_SEQ_CLOSE:
        node.setBraceExpansion(true);
        break;
      default:
        break;
      }
    }
  }
  unsigned int errorCount = 0;
  for (; !stack.empty(); stack.pop_back()) {
    errorCount++;
    this->reportError<BraceUnclosed>(*segmentNodes[stack.back().first]);
  }

  if (errorCount) {
    return;
  }

  // add brace id and check tilde expansion
  stack.clear();
  unsigned int braceId = 0;
  for (unsigned int i = 0; i < size; i++) {
    auto &e = segmentNodes[i];
    if (isExpandingWildCard(*e)) {
      auto &wild = cast<WildCardNode>(*e);
      switch (wild.meta) {
      case ExpandMeta::BRACE_OPEN:
      case ExpandMeta::BRACE_SEQ_OPEN:
        stack.emplace_back(braceId++, 0);
        wild.setBraceId(stack.back().first);
        break;
      case ExpandMeta::BRACE_SEP:
        wild.setBraceId(stack.back().first);
        break;
      case ExpandMeta::BRACE_CLOSE:
      case ExpandMeta::BRACE_SEQ_CLOSE:
        wild.setBraceId(stack.back().first);
        stack.pop_back();
        break;
      default:
        break;
      }
    } else if (isa<WildCardNode>(*e)) {
      auto &wild = cast<WildCardNode>(*e);
      if (wild.meta == ExpandMeta::BRACE_TILDE) {
        if (stack.empty()) {
          assert(i + 1 < size && isa<StringNode>(*segmentNodes[i + 1]));
          cast<StringNode>(*segmentNodes[i + 1]).unsetTilde();
        } else {
          wild.setExpand(true);
        }
      }
    }
  }
}

void TypeChecker::checkExpansion(CmdArgNode &node) {
  this->checkBraceExpansion(node);

  const unsigned int size = node.getSegmentNodes().size();
  unsigned int expansionSize = 0;
  for (unsigned int i = 0; i < size; i++) {
    auto &e = node.refSegmentNodes()[i];
    if (isExpandingWildCard(*e)) {
      if (expansionSize == 0 && i > 0) {
        expansionSize++;
      }
      expansionSize++;
    } else if (i > 0 && isExpandingWildCard(*node.getSegmentNodes()[i - 1])) {
      expansionSize++;
    }
  }
  node.setExpansionSize(expansionSize);

  if (node.getExpansionSize() > SYS_LIMIT_EXPANSION_FRAG_NUM) {
    this->reportError<ExpandLimit>(node);
  }
}

void TypeChecker::visitCmdArgNode(CmdArgNode &node) {
  for (auto &exprNode : node.getSegmentNodes()) {
    this->checkTypeAsExpr(*exprNode);
    assert(exprNode->getType().is(TYPE::String) || exprNode->getType().is(TYPE::StringArray) ||
           exprNode->getType().is(TYPE::FD) || exprNode->getType().isNothingType() ||
           exprNode->getType().isUnresolved());
  }
  this->checkExpansion(node);

  // not allow String Array and UnixFD type
  if (node.getSegmentNodes().size() > 1) {
    for (auto &exprNode : node.getSegmentNodes()) {
      auto *exprType = &exprNode->getType();
      if (exprType->is(TYPE::StringArray) || exprType->is(TYPE::FD)) {
        if (isa<EmbedNode>(*exprNode)) {
          exprType = &cast<EmbedNode>(*exprNode).getExprNode().getType();
        }
        this->reportError<ConcatParam>(*exprNode, exprType->getName());
      }
    }
  }
  assert(!node.getSegmentNodes().empty());
  node.setType(node.getExpansionSize() > 0 ? this->typePool.get(TYPE::StringArray)
                                           : node.getSegmentNodes()[0]->getType());
}

void TypeChecker::visitArgArrayNode(ArgArrayNode &node) {
  for (auto &argNode : node.getCmdArgNodes()) {
    this->checkTypeAsExpr(*argNode);
  }
  node.setType(this->typePool.get((TYPE::StringArray)));
}

/**
 * only allow [0-9]+ format command argument
 * @param argNode
 * @return
 */
static std::pair<int32_t, bool> toNumericCmdArg(const CmdArgNode &argNode) {
  if (argNode.getSegmentNodes().size() == 1 && isa<StringNode>(*argNode.getSegmentNodes()[0])) {
    auto &strNode = cast<StringNode>(*argNode.getSegmentNodes()[0]);
    if (strNode.getToken().size == strNode.getValue().size()) {
      StringRef ref = strNode.getValue();
      return convertToDecimal<int32_t>(ref.begin(), ref.end());
    }
  }
  return {0, false};
}

void TypeChecker::visitRedirNode(RedirNode &node) {
  {
    // check fd format
    StringRef ref = node.getFdName();
    auto pair = convertToDecimal<int32_t>(ref.begin(), ref.end());
    if (pair.second && pair.first >= 0 && pair.first <= 2) {
      node.setNewFd(pair.first);
    } else {
      this->reportError<RedirFdRange>(node, node.getFdName().c_str());
    }
  }

  auto &argNode = node.getTargetNode();
  switch (node.getRedirOp()) {
  case RedirOp::NOP:
  case RedirOp::REDIR_IN:
  case RedirOp::REDIR_OUT:
  case RedirOp::CLOBBER_OUT:
  case RedirOp::APPEND_OUT:
  case RedirOp::REDIR_OUT_ERR:
  case RedirOp::CLOBBER_OUT_ERR:
  case RedirOp::APPEND_OUT_ERR:
  case RedirOp::HERE_STR:
    this->checkType(this->typePool.get(TYPE::String), argNode);
    break;
  case RedirOp::DUP_FD: {
    auto &type = this->checkTypeExactly(argNode);
    if (!type.is(TYPE::FD)) {
      auto pair = toNumericCmdArg(argNode);
      if (pair.second && pair.first >= 0 && pair.first <= 2) {
        node.setTargetFd(pair.first);
      } else {
        this->reportError<NeedFd>(argNode);
      }
    }
    break;
  }
  }
  node.setType(this->typePool.get(TYPE::Any)); // FIXME:
}

void TypeChecker::visitWildCardNode(WildCardNode &node) {
  node.setType(this->typePool.get(TYPE::String));
}

void TypeChecker::visitBraceSeqNode(BraceSeqNode &node) {
  auto kind = node.getRange().kind;
  if (kind == BraceRange::Kind::UNINIT_CHAR || kind == BraceRange::Kind::UNINIT_INT) {
    std::string error;
    auto range = toBraceRange(this->lexer.toStrRef(node.getActualToken()),
                              kind == BraceRange::Kind::UNINIT_CHAR, error);
    switch (range.kind) {
    case BraceRange::Kind::CHAR:
    case BraceRange::Kind::INT:
      node.setRange(std::move(range));
      break;
    case BraceRange::Kind::UNINIT_CHAR:
    case BraceRange::Kind::UNINIT_INT:
      break; // unrechable
    case BraceRange::Kind::OUT_OF_RANGE:
      this->reportError<BraceOutOfRange>(node.getActualToken(), error.c_str());
      break;
    case BraceRange::Kind::OUT_OF_RANGE_STEP:
      this->reportError<BraceOutOfRangeStep>(node.getActualToken(), error.c_str());
      break;
    }
  }
  node.setType(this->typePool.get(TYPE::String));
}

void TypeChecker::visitPipelineNode(PipelineNode &node) {
  unsigned int size = node.getNodes().size();
  if (size + (node.isLastPipe() ? 0 : 1) > SYS_LIMIT_PIPE_LEN) {
    this->reportError<PipeLimit>(node);
  }

  {
    auto child = this->funcCtx->intoChild();
    for (unsigned int i = 0; i < size - 1; i++) {
      this->checkTypeExactly(*node.getNodes()[i]);
    }
  }

  if (node.isLastPipe()) {
    auto scope = this->intoBlock();
    this->addEntry(node, "%%pipe", this->typePool.get(TYPE::Any), HandleAttr::READ_ONLY);
    node.setBaseIndex(this->curScope->getBaseIndex());
    auto &type = this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(type);
  } else {
    auto child = this->funcCtx->intoChild();
    this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(this->typePool.get(TYPE::Bool));
  }
}

void TypeChecker::visitSourceNode(SourceNode &node) {
  assert(this->isTopLevel());

  // import module
  ImportedModKind importedKind{};
  if (!node.getNameInfo()) {
    setFlag(importedKind, ImportedModKind::GLOBAL);
  }
  if (node.isInlined()) {
    setFlag(importedKind, ImportedModKind::INLINED);
  }
  if (hasFlag(node.getModType().getAttr(), ModAttr::HAS_ERRORS)) { // if error recovery is enabled
    this->reportError<ErrorMod>(node, node.getPathName().c_str());
  }
  auto ret = this->curScope->importForeignHandles(this->typePool, node.getModType(), importedKind);
  if (!ret.empty()) {
    this->reportError<ConflictSymbol>(node, ret.c_str(), node.getPathName().c_str());
  }
  if (node.getNameInfo()) { // scoped import
    auto &nameInfo = *node.getNameInfo();
    auto handle = node.getModType().toAliasHandle(this->curScope->modId);

    // register actual module handle
    if (!this->curScope->defineAlias(std::string(nameInfo.getName()), handle)) {
      this->reportError<DefinedSymbol>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
    std::string cmdName = toCmdFullName(nameInfo.getName());
    if (!this->curScope->defineAlias(std::move(cmdName), handle)) { // for module subcommand
      this->reportError<DefinedCmd>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
    if (!this->curScope->defineTypeAlias(this->typePool, nameInfo.getName(), node.getModType())) {
      this->reportError<DefinedTypeAlias>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
  }
  node.setType(this->typePool.get(node.isUnreachable() ? TYPE::Nothing : TYPE::Void));
}

class SourceGlobIter {
private:
  using iterator = SourceListNode::path_iterator;

  iterator cur;
  const char *ptr{nullptr};

public:
  explicit SourceGlobIter(iterator begin) : cur(begin) {
    if (isa<StringNode>(**this->cur)) {
      this->ptr = cast<StringNode>(**this->cur).getValue().c_str();
    }
  }

  char operator*() const { return this->ptr == nullptr ? '\0' : *this->ptr; }

  bool operator==(const SourceGlobIter &other) const {
    return this->cur == other.cur && this->ptr == other.ptr;
  }

  bool operator!=(const SourceGlobIter &other) const { return !(*this == other); }

  SourceGlobIter &operator++() {
    if (this->ptr) {
      this->ptr++;
      if (*this->ptr == '\0') { // if reaches null, increment iterator
        this->ptr = nullptr;
      }
    }
    if (!this->ptr) {
      ++this->cur;
      if (isa<StringNode>(**this->cur)) {
        this->ptr = cast<StringNode>(**this->cur).getValue().c_str();
      }
    }
    return *this;
  }

  iterator getIter() const { return this->cur; }
};

struct SourceGlobMeta {
  static bool isAny(SourceGlobIter iter) {
    auto &node = **iter.getIter();
    return isa<WildCardNode>(node) && cast<WildCardNode>(node).meta == ExpandMeta::ANY;
  }

  static bool isZeroOrMore(SourceGlobIter iter) {
    auto &node = **iter.getIter();
    return isa<WildCardNode>(node) && cast<WildCardNode>(node).meta == ExpandMeta::ZERO_OR_MORE;
  }
};

static std::string concat(SourceListNode::path_iterator begin, SourceListNode::path_iterator end) {
  std::string path;
  for (; begin != end; ++begin) {
    auto &e = **begin;
    assert(isa<StringNode>(e) || isa<WildCardNode>(e));
    if (isa<StringNode>(e)) {
      path += cast<StringNode>(e).getValue();
    } else {
      path += toString(cast<WildCardNode>(e).meta);
    }
  }
  return path;
}

static bool isDirPattern(SourceListNode::path_iterator begin, SourceListNode::path_iterator end) {
  assert(begin < end);
  ssize_t size = end - begin;
  for (ssize_t i = size - 1; i > -1; i--) {
    auto &e = **(begin + i);
    if (isa<StringNode>(e)) {
      StringRef ref = cast<StringNode>(e).getValue();
      if (ref.empty()) {
        continue;
      }
      return ref.back() == '/' || ref.endsWith("/.") || ref.endsWith("/..");
    }
    break;
  }
  return false;
}

static bool appendPath(std::vector<std::shared_ptr<const std::string>> &results,
                       std::string &&path) {
  if (results.size() == SYS_LIMIT_EXPANSION_RESULTS) {
    return false;
  }
  results.push_back(std::make_shared<const std::string>(std::move(path)));
  return true;
}

void TypeChecker::reportTildeExpansionError(Token token, const std::string &path,
                                            TildeExpandStatus status) {
  assert(status != TildeExpandStatus::OK);

  StringRef ref = path;
  if (auto pos = ref.find("/"); pos != StringRef::npos) {
    assert(pos > 0);
    ref = ref.substr(0, pos);
  }
  auto value = ref.toString();

  switch (status) {
  case TildeExpandStatus::OK:
  case TildeExpandStatus::NO_TILDE:
    break;
  case TildeExpandStatus::NO_USER:
    this->reportError<TildeFail>(token, value.c_str());
    return;
  case TildeExpandStatus::NO_DIR_STACK:
    this->reportError<TildeNoDirStack>(token, value.c_str());
    return;
  case TildeExpandStatus::UNDEF_OR_EMPTY:
  case TildeExpandStatus::INVALID_NUM:
  case TildeExpandStatus::OUT_OF_RANGE:
  case TildeExpandStatus::HAS_NULL:
    break;
  }
  assert(false);
}

bool TypeChecker::applyGlob(Token token, std::vector<std::shared_ptr<const std::string>> &results,
                            const SourceListNode::path_iterator begin,
                            const SourceListNode::path_iterator end, GlobOp op) {
  if (isDirPattern(begin, end)) {
    std::string path = concat(begin, end);
    this->reportError<NoGlobDir>(token, path.c_str());
    return false;
  }

  const unsigned int oldSize = results.size();
  auto appender = [&results](std::string &&path) { return appendPath(results, std::move(path)); };
  auto option = GlobMatchOption::IGNORE_SYS_DIR | GlobMatchOption::FASTGLOB |
                GlobMatchOption::ABSOLUTE_BASE_DIR;
  if (hasFlag(op, GlobOp::TILDE)) {
    setFlag(option, GlobMatchOption::TILDE);
  }
  auto matcher = createGlobMatcher<SourceGlobMeta>(
      nullptr, SourceGlobIter(begin), SourceGlobIter(end), [] { return false; }, option);
  TildeExpandStatus tildeExpandStatus{};
  auto expander = [&tildeExpandStatus](std::string &path) {
    tildeExpandStatus = expandTilde(path, true, nullptr);
    return tildeExpandStatus == TildeExpandStatus::OK;
  };
  auto ret = matcher(std::move(expander), appender);
  if (ret == GlobMatchResult::MATCH ||
      (ret == GlobMatchResult::NOMATCH && hasFlag(op, GlobOp::OPTIONAL))) {
    std::sort(results.begin() + oldSize, results.end(),
              [](const std::shared_ptr<const std::string> &x,
                 const std::shared_ptr<const std::string> &y) { return *x < *y; });
    return true;
  } else {
    std::string path = concat(begin, end);
    if (ret == GlobMatchResult::NOMATCH) {
      this->reportError<NoGlobMatch>(token, path.c_str());
    } else if (ret == GlobMatchResult::NEED_ABSOLUTE_BASE_DIR) {
      this->reportError<NoRelativeGlob>(token, path.c_str());
    } else if (ret == GlobMatchResult::TILDE_FAIL) {
      assert(tildeExpandStatus != TildeExpandStatus::OK);
      this->reportTildeExpansionError(token, matcher.getBase(), tildeExpandStatus);
    } else {
      assert(ret == GlobMatchResult::LIMIT);
      this->reportError<ExpandRetLimit>(token);
    }
    return false;
  }
}

struct SrcExpandState {
  unsigned int index;
  unsigned int usedSize;
  unsigned int closeIndex;
  unsigned int braceId;

  struct Compare {
    bool operator()(const SrcExpandState &x, unsigned int y) const { return x.braceId < y; }

    bool operator()(unsigned int x, const SrcExpandState &y) const { return x < y.braceId; }
  };
};

static bool needGlob(SourceListNode::path_iterator begin, SourceListNode::path_iterator end) {
  for (; begin != end; ++begin) {
    auto &v = **begin;
    if (isExpandingWildCard(v)) {
      return true;
    }
  }
  return false;
}

bool TypeChecker::applyBraceExpansion(Token token,
                                      std::vector<std::shared_ptr<const std::string>> &results,
                                      const SourceListNode::path_iterator begin,
                                      const SourceListNode::path_iterator end, const GlobOp op) {
  assert(begin <= end);
  auto sentinel = std::make_unique<EmptyNode>();
  const unsigned int size = end - begin;
  FlexBuffer<SrcExpandState> stack;
  std::vector<Node *> values;
  values.resize(size + 1); // reserve sentinel
  FlexBuffer<int64_t> seqStack;
  std::vector<std::unique_ptr<Node>> seqNodes;
  unsigned int usedSize = 0;

  for (unsigned int i = 0; i < size; i++) {
    auto &v = begin[i];
    if (isExpandingWildCard(*v)) {
      auto wild = cast<WildCardNode>(v);
      switch (wild->meta) {
      case ExpandMeta::BRACE_OPEN: {
        // find close index
        unsigned int closeIndex = i + 1;
        for (int level = 1; closeIndex < size; closeIndex++) {
          if (isExpandingWildCard(*begin[closeIndex])) {
            auto next = cast<WildCardNode>(begin[closeIndex])->meta;
            if (next == ExpandMeta::BRACE_CLOSE) {
              if (--level == 0) {
                break;
              }
            } else if (next == ExpandMeta::BRACE_OPEN) {
              level++;
            }
          }
        }
        stack.push_back(SrcExpandState{
            .index = i,
            .usedSize = usedSize,
            .closeIndex = closeIndex,
            .braceId = wild->getBraceId(),
        });
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEP:
      case ExpandMeta::BRACE_CLOSE: {
        auto iter = std::lower_bound(stack.begin(), stack.end(), wild->getBraceId(),
                                     SrcExpandState::Compare());
        assert(iter != stack.end());
        (*iter).index = i;
        i = (*iter).closeIndex;
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_TILDE:
        if (usedSize) {
          goto CONTINUE;
        }
        break;
      case ExpandMeta::BRACE_SEQ_OPEN: {
        i++;
        stack.push_back(SrcExpandState{
            .index = i + 1,
            .usedSize = usedSize,
            .closeIndex = i + 1,
            .braceId = wild->getBraceId(),
        });

        auto &range = cast<BraceSeqNode>(begin[i])->getRange();
        seqStack.push_back(range.begin);
        seqNodes.push_back(nullptr);
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEQ_CLOSE: {
        auto &range = cast<BraceSeqNode>(begin[i - 1])->getRange();
        auto value =
            formatSeqValue(seqStack.back(), range.digits, range.kind == BraceRange::Kind::CHAR);
        seqNodes.back() = std::make_unique<StringNode>(std::move(value));
        values[usedSize++] = seqNodes.back().get();
        goto CONTINUE;
      }
      default:
        break;
      }
    }
    values[usedSize++] = v;

  CONTINUE:
    if (i == size - 1) {
      values[usedSize] = sentinel.get(); // sentinel

      auto vbegin = values.begin();
      auto vend = vbegin + usedSize;
      auto newOp = op;
      if (!hasFlag(newOp, GlobOp::TILDE) && usedSize > 0 && isExpandingWildCard(**vbegin) &&
          cast<WildCardNode>(*vbegin)->meta == ExpandMeta::BRACE_TILDE) {
        setFlag(newOp, GlobOp::TILDE);
        ++vbegin; // skip meta
      }

      if (needGlob(vbegin, vend)) {
        if (!this->applyGlob(token, results, vbegin, vend, newOp)) {
          return false;
        }
      } else {
        auto path = concat(vbegin, vend);
        if (hasFlag(newOp, GlobOp::TILDE)) {
          if (auto s = expandTilde(path, true, nullptr); s != TildeExpandStatus::OK) {
            this->reportTildeExpansionError(token, path, s);
            return false;
          }
        }
        if (!path.empty()) {
          if (!appendPath(results, std::move(path))) {
            this->reportError<ExpandRetLimit>(token);
            return false;
          }
        }
      }

      while (!stack.empty()) {
        unsigned int oldIndex = stack.back().index;
        auto &old = begin[oldIndex];
        assert(isExpandingWildCard(*old));
        auto meta = cast<WildCardNode>(*old).meta;
        if (meta == ExpandMeta::BRACE_CLOSE) {
          stack.pop_back();
        } else if (meta == ExpandMeta::BRACE_SEQ_CLOSE) {
          auto &range = cast<BraceSeqNode>(begin[oldIndex - 1])->getRange();
          if (tryUpdateSeqValue(seqStack.back(), range)) {
            i = oldIndex - 1;
            usedSize = stack.back().usedSize;
            break;
          } else {
            stack.pop_back();
            seqStack.pop_back();
            seqNodes.pop_back();
          }
        } else {
          i = oldIndex;
          usedSize = stack.back().usedSize;
          break;
        }
      }
    }
  }
  return true;
}

void TypeChecker::resolvePathList(SourceListNode &node) {
  if (node.getConstNodes().empty() ||
      node.getPathNode().getExpansionSize() > SYS_LIMIT_EXPANSION_FRAG_NUM) {
    return;
  }
  node.addConstNode(std::make_unique<EmptyNode>()); // sentinel
  auto &pathNode = node.getPathNode();
  auto begin = node.getConstNodes().cbegin();
  auto end = node.getConstNodes().cend() - 1;

  std::vector<std::shared_ptr<const std::string>> results;
  if (!node.isExpansion()) {
    std::string path = concat(begin, end);
    if (pathNode.isTilde()) {
      if (auto s = expandTilde(path, true, nullptr); s != TildeExpandStatus::OK) {
        this->reportTildeExpansionError(pathNode.getToken(), path, s);
        return;
      }
    }
    results.push_back(std::make_shared<const std::string>(std::move(path)));
  } else {
    GlobOp op{};
    if (pathNode.isTilde()) {
      setFlag(op, GlobOp::TILDE);
    }
    if (node.isOptional()) {
      setFlag(op, GlobOp::OPTIONAL);
    }

    bool status;
    if (pathNode.isBraceExpansion()) {
      status = this->applyBraceExpansion(pathNode.getToken(), results, begin, end, op);
    } else {
      status = this->applyGlob(pathNode.getToken(), results, begin, end, op);
    }
    if (!status) {
      return;
    }
  }
  node.setPathList(std::move(results));
}

void TypeChecker::visitSourceListNode(SourceListNode &node) {
  node.setType(this->typePool.get(TYPE::Void));
  if (!this->isTopLevel()) { // only available toplevel scope
    this->reportError<OutsideToplevel>(node, "source statement");
    return;
  }
  this->checkTypeExactly(node.getPathNode());
  auto &exprType = this->typePool.get(node.isExpansion() ? TYPE::StringArray : TYPE::String);
  this->checkType(exprType, node.getPathNode());

  std::unique_ptr<CmdArgNode> constPathNode;
  auto &pathNode = node.getPathNode();
  for (auto &e : pathNode.getSegmentNodes()) {
    auto constNode = this->evalConstant(*e);
    if (!constNode) {
      return;
    }
    assert(isa<StringNode>(*constNode) || isa<WildCardNode>(*constNode) ||
           isa<BraceSeqNode>(*constNode));
    if (isa<StringNode>(*constNode)) {
      auto ref = StringRef(cast<StringNode>(*constNode).getValue());
      if (ref.hasNullChar()) {
        this->reportError<NullInPath>(pathNode);
        return;
      }
    }
    node.addConstNode(std::move(constNode));
  }
#ifdef FUZZING_BUILD_MODE
  if (const char *env = getenv("YDSH_SUPPRESS_MOD_LOADING")) {
    return;
  }
#endif
  this->resolvePathList(node);
}

} // namespace ydsh