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

#include "format_util.h"
#include "glob.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "paths.h"
#include "type_checker.h"

namespace arsh {

static const Node *getNonRedirNode(const CmdNode &node) {
  for (auto &e : node.getArgNodes()) {
    if (!isa<RedirNode>(*e)) {
      return e.get();
    }
  }
  return nullptr;
}

void TypeChecker::visitCmdNode(CmdNode &node) {
  this->checkType(this->typePool().get(TYPE::String), node.getNameNode());
  for (auto &argNode : node.getArgNodes()) {
    this->checkTypeAsExpr(*argNode);
  }
  if (node.getNameNode().getValue().empty()) {
    if (!node.getAllowEmpty()) {
      this->reportError<NoEmptyRedir>(node);
    }
    if (auto *n = getNonRedirNode(node)) {
      this->reportError<EmptyRedirArgs>(*n);
    }
  }
  if (node.getNameNode().getValue() == "exit" || node.getNameNode().getValue() == "_exit") {
    node.setType(this->typePool().get(TYPE::Nothing));
  } else {
    node.setType(this->typePool().get(TYPE::Bool));
    const std::string cmdName = toCmdFullName(node.getNameNode().getValue());
    if (auto ret = this->curScope->lookup(cmdName)) {
      const auto handle = std::move(ret).take();
      node.setHandle(handle);
      if (auto &type = this->typePool().get(handle->getTypeId());
          type.isFuncType()) { // resolved command may be module object
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
  // check the balance of brace expansion
  std::vector<std::pair<unsigned int, unsigned int>> stack;
  auto &segmentNodes = node.refSegmentNodes();
  const unsigned int size = segmentNodes.size();
  for (unsigned int i = 0; i < size; i++) {
    if (auto &e = *segmentNodes[i]; isa<WildCardNode>(e)) {
      auto &wild = cast<WildCardNode>(e);
      switch (wild.meta) {
      case ExpandMeta::BRACE_OPEN:
        stack.emplace_back(i, 0);
        break;
      case ExpandMeta::BRACE_CLOSE:
        if (stack.empty()) {
          node.setExpansionError(true);
          this->reportError<BraceUnopened>(wild);
        } else {
          if (stack.back().second) {
            cast<WildCardNode>(*segmentNodes[stack.back().first]).setExpand(true);
            wild.setExpand(true);
            node.enableBraceExpansion();
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
        node.enableBraceExpansion();
        break;
      default:
        break;
      }
    } else if (isa<BraceSeqNode>(e)) {
      auto &seqNode = cast<BraceSeqNode>(e);
      if (seqNode.getRange().hasError()) {
        node.setExpansionError(true);
      }
    }
  }
  for (; !stack.empty(); stack.pop_back()) {
    this->reportError<BraceUnclosed>(*segmentNodes[stack.back().first]);
    node.setExpansionError(true);
  }

  if (node.hasExpansionError()) {
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
    }
  }
}

void TypeChecker::checkTildeExpansion(CmdArgNode &node) {
  const unsigned int size = node.getSegmentNodes().size();
  for (unsigned int i = 0; i < size; i++) {
    if (auto &segNode = *node.getSegmentNodes()[i]; !isa<WildCardNode>(segNode)) {
      if (i == 0) {
        continue;
      }
      if (auto &prevNode = *node.getSegmentNodes()[i - 1]; isExpandingWildCard(prevNode)) {
        if (auto &wildNode = cast<WildCardNode>(prevNode); wildNode.meta == ExpandMeta::COLON) {
          wildNode.setExpand(false); // disable expansion for mid ':'
        }
      }
      continue;
    }

    auto &wildNode = cast<WildCardNode>(*node.getSegmentNodes()[i]);
    switch (wildNode.meta) {
    case ExpandMeta::TILDE:
      if (i == 0) { // echo ~/root
        continue;
      }
      if (auto &prevNode = *node.getSegmentNodes()[i - 1]; isExpandingWildCard(prevNode)) {
        if (auto &prevWildNode = cast<WildCardNode>(prevNode);
            prevWildNode.isBraceMeta() || prevWildNode.meta == ExpandMeta::ASSIGN ||
            prevWildNode.meta == ExpandMeta::COLON) {
          /**
           * consider the following cases
           *
           * echo {~,}/root
           * echo {a,~}/root
           * echo {,}~/root
           * echo AAA=~
           * AAA=$PATH:~
           */
          continue;
        }
      }
      wildNode.setExpand(false); // disable tilde expansion
      break;
    case ExpandMeta::ASSIGN:
    case ExpandMeta::COLON:
      if (i == size - 1) { // disable expansion of last '=', ':' (ex. echo AAA=, AAA=$PATH: )
        wildNode.setExpand(false);
      }
      break;
    default:
      break;
    }
  }
}

void TypeChecker::checkExpansion(CmdArgNode &node) {
  this->checkBraceExpansion(node);
  this->checkTildeExpansion(node);

  const unsigned int size = node.getSegmentNodes().size();
  unsigned int expansionSize = 0;
  for (unsigned int i = 0; i < size; i++) {
    if (auto &cur = *node.getSegmentNodes()[i]; isExpandingWildCard(cur) || isEscapedStr(cur)) {
      if (expansionSize == 0 && i > 0) {
        expansionSize++;
      }
      expansionSize++;
    } else if (i > 0) {
      if (auto &prev = *node.getSegmentNodes()[i - 1];
          isExpandingWildCard(prev) || isEscapedStr(prev)) {
        expansionSize++;
      }
    }
  }
  node.setExpansionSize(expansionSize);

  if (node.getExpansionSize() > SYS_LIMIT_EXPANSION_FRAG_NUM) {
    this->reportError<ExpandLimit>(node);
    node.setExpansionError(true);
  }
}

void TypeChecker::visitCmdArgNode(CmdArgNode &node) {
  for (auto &exprNode : node.getSegmentNodes()) {
    this->checkTypeAsExpr(*exprNode);
    assert(exprNode->getType().is(TYPE::String) || exprNode->getType().is(TYPE::StringArray) ||
           this->typePool().get(TYPE::FD).isSameOrBaseTypeOf(exprNode->getType()) ||
           exprNode->getType().isNothingType() || exprNode->getType().isUnresolved());
  }
  this->checkExpansion(node);

  // not allow String Array and UnixFD type
  if (node.getSegmentNodes().size() > 1) {
    for (auto &exprNode : node.getSegmentNodes()) {
      auto *exprType = &exprNode->getType();
      if (exprType->is(TYPE::StringArray) ||
          this->typePool().get(TYPE::FD).isSameOrBaseTypeOf(*exprType)) {
        if (isa<EmbedNode>(*exprNode)) {
          exprType = &cast<EmbedNode>(*exprNode).getExprNode().getType();
        }
        this->reportError<ConcatParam>(*exprNode, exprType->getName());
        node.setExpansionError(true);
      }
    }
  }
  if (!node.hasExpansionError()) {
    assert(!node.getSegmentNodes().empty());
    node.setType(node.isBraceExpansion() || node.isGlobExpansion()
                     ? this->typePool().get(TYPE::StringArray)
                     : node.getSegmentNodes()[0]->getType());
  }
}

void TypeChecker::visitArgArrayNode(ArgArrayNode &node) {
  for (auto &argNode : node.getCmdArgNodes()) {
    if (this->typePool().get(TYPE::FD).isSameOrBaseTypeOf(this->checkTypeAsExpr(*argNode))) {
      this->reportError<FdArgArray>(*argNode);
    }
  }
  node.setType(this->typePool().get((TYPE::StringArray)));
}

/**
 * only allow [0-9]+ or '-' format command argument
 * @param argNode
 * @return
 */
static std::pair<int32_t, bool> toNumericCmdArg(const CmdArgNode &argNode) {
  if (argNode.getSegmentNodes().size() == 1 && isa<StringNode>(*argNode.getSegmentNodes()[0])) {
    auto &strNode = cast<StringNode>(*argNode.getSegmentNodes()[0]);
    if (strNode.getToken().size == strNode.getValue().size()) {
      const StringRef ref = strNode.getValue();
      if (ref.size() == 1 && ref[0] == '-') {
        return {-1, true};
      }
      if (auto ret = convertToNum10<int32_t>(ref.begin(), ref.end()); ret && ret.value > -1) {
        return {ret.value, true};
      }
    }
  }
  return {0, false};
}

void TypeChecker::visitRedirNode(RedirNode &node) {
  {
    // check fd format
    const StringRef ref = node.getFdName();
    const auto pair = convertToNum10<int32_t>(ref.begin(), ref.end());
    if (pair && pair.value >= 0 && pair.value < RESERVED_FD_LIMIT) {
      node.setNewFd(static_cast<int8_t>(pair.value));
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
  case RedirOp::REDIR_IN_OUT:
  case RedirOp::HERE_DOC:
  case RedirOp::HERE_STR:
    this->checkType(this->typePool().get(TYPE::String), argNode);
    break;
  case RedirOp::DUP_FD:
    if (!this->typePool().get(TYPE::FD).isSameOrBaseTypeOf(this->checkTypeExactly(argNode))) {
      if (const auto [value, s] = toNumericCmdArg(argNode);
          s && value >= -1 && value < RESERVED_FD_LIMIT) {
        node.setTargetFd(static_cast<int8_t>(value));
      } else {
        this->reportError<NeedFd>(argNode);
      }
    }
    break;
  }
  node.setType(this->typePool().get(TYPE::Any));
}

void TypeChecker::visitWildCardNode(WildCardNode &node) {
  node.setType(this->typePool().get(TYPE::String));
}

void TypeChecker::visitBraceSeqNode(BraceSeqNode &node) {
  if (const auto kind = node.getRange().kind;
      kind == BraceRange::Kind::UNINIT_CHAR || kind == BraceRange::Kind::UNINIT_INT) {
    std::string error;
    const auto range = toBraceRange(this->lexer.get().toStrRef(node.getActualToken()),
                                    kind == BraceRange::Kind::UNINIT_CHAR, error);
    node.setRange(range);
    switch (range.kind) {
    case BraceRange::Kind::CHAR:
    case BraceRange::Kind::INT:
    case BraceRange::Kind::UNINIT_CHAR: // unreachable
    case BraceRange::Kind::UNINIT_INT:  // unreachable
      break;
    case BraceRange::Kind::OUT_OF_RANGE:
      this->reportError<BraceOutOfRange>(node.getActualToken(), error.c_str());
      break;
    case BraceRange::Kind::OUT_OF_RANGE_STEP:
      this->reportError<BraceOutOfRangeStep>(node.getActualToken(), error.c_str());
      break;
    }
  }
  node.setType(this->typePool().get(TYPE::String));
}

void TypeChecker::visitPipelineNode(PipelineNode &node) {
  const unsigned int size = node.getNodes().size();
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
    auto lastPipe = this->funcCtx->intoChild();
    auto scope = this->intoBlock();
    this->addEntry(node, node.toCtxName(), this->typePool().get(TYPE::Any), HandleAttr::READ_ONLY);
    node.setBaseIndex(this->curScope->getBaseIndex());
    auto &type = this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(type);
  } else {
    auto child = this->funcCtx->intoChild();
    this->checkTypeExactly(*node.getNodes()[size - 1]);
    node.setType(this->typePool().get(TYPE::Bool));
  }
}

void TypeChecker::visitSourceNode(SourceNode &node) {
  assert(this->isTopLevel());

  // import module
  if (hasFlag(node.getModType().getAttr(), ModAttr::HAS_ERRORS)) { // if error recovery is enabled
    this->reportError<ErrorMod>(node, node.getPathName().c_str());
  }
  const auto ret = this->curScope->importForeignHandles(this->typePool(), node.getModType(),
                                                        node.getImportedModKind());
  if (!ret.empty()) {
    this->reportError<ConflictSymbol>(node, ret.c_str(), node.getPathName().c_str());
  }
  if (node.getNameInfo()) { // scoped import
    auto &nameInfo = *node.getNameInfo();
    const auto handle = node.getModType().toAliasHandle(this->curScope->modId);

    // register actual module handle
    if (!this->curScope->defineAlias(std::string(nameInfo.getName()), handle)) {
      this->reportError<DefinedSymbol>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
    std::string cmdName = toCmdFullName(nameInfo.getName());
    if (!this->curScope->defineAlias(std::move(cmdName), handle)) { // for module subcommand
      this->reportError<DefinedCmd>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
    if (!this->curScope->defineTypeAlias(this->typePool(), nameInfo.getName(), node.getModType())) {
      this->reportError<DefinedTypeAlias>(nameInfo.getToken(), nameInfo.getName().c_str());
    }
  }
  node.setType(this->typePool().get(node.isUnreachable() ? TYPE::Nothing : TYPE::Void));
}

static std::string concat(SourceListNode::path_iterator begin, SourceListNode::path_iterator end) {
  std::string path;
  for (; begin != end; ++begin) {
    auto &e = **begin;
    assert(isa<StringNode>(e) || isa<WildCardNode>(e));
    if (isa<StringNode>(e)) {
      if (const auto &strNode = cast<StringNode>(e); strNode.isEscaped()) {
        appendAsUnescaped(strNode.getValue(), path.max_size(), path);
      } else {
        path += strNode.getValue();
      }
    } else {
      path += toString(cast<WildCardNode>(e).meta);
    }
  }
  return path;
}

static bool isDirPattern(const StringRef ref) {
  assert(!ref.empty());
  return ref.back() == '/' || ref.endsWith("/.") || ref.endsWith("/..");
}

bool TypeChecker::concatAsGlobPattern(const Token token, SourceListNode::path_iterator begin,
                                      SourceListNode::path_iterator end,
                                      GlobPatternWrapper &pattern) {
  std::string value;
  const bool tilde = begin != end && isExpandingTilde(**begin);
  for (; begin != end; ++begin) {
    auto &e = **begin;
    assert(isa<StringNode>(e) || isa<WildCardNode>(e));
    if (isa<StringNode>(e)) {
      if (const auto &strNode = cast<StringNode>(e); strNode.isEscaped()) {
        value += strNode.getValue();
      } else {
        appendAndEscapeGlobMeta(cast<StringNode>(e).getValue(), value.max_size(), value);
      }
    } else {
      value += toString(cast<WildCardNode>(e).meta);
    }
  }

  if (isDirPattern(value)) {
    this->reportError<NoGlobDir>(token, value.c_str());
    return false;
  }
  pattern = GlobPatternWrapper::create(std::move(value));
  if (tilde) {
    TildeExpandStatus status;
    if (pattern.getBaseDir().empty()) {
      status = TildeExpandStatus::NO_USER;
    } else {
      assert(pattern.getBaseDir()[0] == '~');
      status = expandTilde(pattern.refBaseDir(), true, nullptr);
    }
    if (status != TildeExpandStatus::OK) {
      auto &dir = pattern.getBaseDir();
      this->reportTildeExpansionError(token, dir.empty() ? pattern.getPattern() : dir, status);
      return false;
    }
  }
  if (pattern.getBaseDir().empty() || pattern.getBaseDir()[0] != '/') {
    this->reportError<NoRelativeGlob>(token, pattern.join().c_str());
    return false;
  }
  return true;
}

static unsigned int getExpansionLimit() {
#ifdef FUZZING_BUILD_MODE
  if (auto *env = getenv("ARSH_FUZZ_EXPAND_LIMIT")) {
    if (auto ret = convertToNum10<unsigned int>(env)) {
      return std::min<size_t>(SYS_LIMIT_EXPANSION_RESULTS, ret.value);
    }
  }
#endif
  return SYS_LIMIT_EXPANSION_RESULTS;
}

static bool appendPath(std::vector<std::shared_ptr<const std::string>> &results,
                       std::string &&path) {
  if (results.size() == getExpansionLimit()) {
    return false;
  }
  results.push_back(std::make_shared<const std::string>(std::move(path)));
  return true;
}

void TypeChecker::reportTildeExpansionError(Token token, const std::string &path,
                                            TildeExpandStatus status) {
  assert(status != TildeExpandStatus::OK);

  StringRef ref = path;
  if (const auto pos = ref.find("/"); pos != StringRef::npos) {
    assert(pos > 0);
    ref = ref.substr(0, pos);
  }
  const auto value = ref.toString();

  switch (status) {
  case TildeExpandStatus::OK:
  case TildeExpandStatus::NO_TILDE:
    break;
  case TildeExpandStatus::NO_USER:
  case TildeExpandStatus::SIZE_LIMIT: // FIXME: better error message
    this->reportError<TildeFail>(token, value.c_str());
    return;
  case TildeExpandStatus::NO_DIR_STACK:
    this->reportError<TildeNoDirStack>(token, value.c_str());
    return;
  case TildeExpandStatus::UNDEF_OR_EMPTY:
  case TildeExpandStatus::INVALID_NUM:
  case TildeExpandStatus::OUT_OF_RANGE:
  case TildeExpandStatus::HAS_NULL:
  case TildeExpandStatus::EMPTY_ASSIGN: // FIXME:
    break;
  }
  assert(false);
}

bool TypeChecker::applyGlob(const Token token,
                            std::vector<std::shared_ptr<const std::string>> &results,
                            const SourceListNode::path_iterator begin,
                            const SourceListNode::path_iterator end, const bool optional) {
  GlobPatternWrapper pattern;
  if (!this->concatAsGlobPattern(token, begin, end, pattern)) {
    return false;
  }

  const unsigned int oldSize = results.size();
  Glob glob(pattern, Glob::Option::FASTGLOB | Glob::Option::GLOB_LIMIT);
  glob.setCancelToken(this->cancelToken);
  glob.setConsumer([&results](std::string &&path) { return appendPath(results, std::move(path)); });

  std::string err;
  switch (const auto ret = glob(&err); ret) {
  case Glob::Status::MATCH:
  case Glob::Status::NOMATCH:
    if (ret == Glob::Status::MATCH || optional) {
      std::sort(results.begin() + oldSize, results.end(),
                [](const std::shared_ptr<const std::string> &x,
                   const std::shared_ptr<const std::string> &y) { return *x < *y; });
      return true;
    }
    this->reportError<NoGlobMatch>(token, pattern.join().c_str());
    return false;
  case Glob::Status::CANCELED:
  case Glob::Status::RESOURCE_LIMIT:
  case Glob::Status::RECURSION_DEPTH_LIMIT: {
    int errNum = glob.getErrNum();
    if (ret == Glob::Status::CANCELED) {
      errNum = EINTR;
    }
    std::string suffix;
    if (glob.getErrNum() != 0) {
      suffix = ", caused by `";
      suffix += strerror(errNum);
      suffix += "'";
    }
    this->reportError<GlobResource>(token, suffix.c_str());
    return false;
  }
  case Glob::Status::BAD_PATTERN:
    this->reportError<BadGlobPattern>(token, pattern.join().c_str(), err.c_str());
    return false;
  default:
    assert(ret == Glob::Status::LIMIT);
    this->reportError<ExpandRetLimit>(token);
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
    if (auto &v = **begin; isExpandingWildCard(v)) {
      if (isGlobStart(cast<WildCardNode>(v).meta)) {
        return true;
      }
    }
  }
  return false;
}

bool TypeChecker::applyBraceExpansion(const Token token,
                                      std::vector<std::shared_ptr<const std::string>> &results,
                                      const SourceListNode::path_iterator begin,
                                      const SourceListNode::path_iterator end,
                                      const bool optional) {
  assert(begin <= end);
  const auto sentinel = std::make_unique<EmptyNode>();
  const unsigned int size = end - begin;
  FlexBuffer<SrcExpandState> stack;
  std::vector<Node *> values;
  values.resize(size + 1); // reserve sentinel
  FlexBuffer<int64_t> seqStack;
  std::vector<std::unique_ptr<Node>> seqNodes;
  unsigned int usedSize = 0;
  unsigned int expandCount = 0;

  for (unsigned int i = 0; i < size; i++) {
    auto &v = begin[i];
    if (isExpandingWildCard(*v)) {
      const auto wild = cast<WildCardNode>(v);
      switch (wild->meta) {
      case ExpandMeta::BRACE_OPEN: {
        // find close index
        unsigned int closeIndex = i + 1;
        for (int level = 1; closeIndex < size; closeIndex++) {
          if (isExpandingWildCard(*begin[closeIndex])) {
            const auto next = cast<WildCardNode>(begin[closeIndex])->meta;
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
        iter->index = i;
        i = iter->closeIndex;
        goto CONTINUE;
      }
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
      if (needGlob(vbegin, vend)) {
        if (++expandCount == getExpansionLimit()) {
          this->reportError<ExpandRetLimit>(token);
          return false;
        }
        if (!this->applyGlob(token, results, vbegin, vend, optional)) {
          return false;
        }
      } else {
        auto path = concat(vbegin, vend);
        if (vbegin != vend && isExpandingTilde(**vbegin)) {
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
        const unsigned int oldIndex = stack.back().index;
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
  if (node.getConstNodes().empty() || node.getPathNode().hasExpansionError()) {
    return;
  }
  node.addConstNode(std::make_unique<EmptyNode>()); // sentinel
  auto &pathNode = node.getPathNode();
  auto begin = node.getConstNodes().cbegin();
  auto end = node.getConstNodes().cend() - 1;

  std::vector<std::shared_ptr<const std::string>> results;
  if (!node.isGlobOrBraceExpansion()) {
    std::string path = concat(begin, end);
    if (isExpandingTilde(**begin)) {
      // in source statement, only allow prefix tilde
      if (const auto s = expandTilde(path, true, nullptr); s != TildeExpandStatus::OK) {
        this->reportTildeExpansionError(pathNode.getToken(), path, s);
        return;
      }
    }
    results.push_back(std::make_shared<const std::string>(std::move(path)));
  } else {
    const bool optional = node.isOptional();
    bool status;
    if (pathNode.isBraceExpansion()) {
      status = this->applyBraceExpansion(pathNode.getToken(), results, begin, end, optional);
    } else {
      status = this->applyGlob(pathNode.getToken(), results, begin, end, optional);
    }
    if (!status) {
      return;
    }
  }
  node.setPathList(std::move(results));
}

void TypeChecker::visitSourceListNode(SourceListNode &node) {
  node.setType(this->typePool().get(TYPE::Void));
  if (!this->isTopLevel()) { // only available toplevel scope
    this->reportError<OutsideToplevel>(node, "source statement");
    return;
  }
  this->checkTypeExactly(node.getPathNode());
  auto &exprType =
      this->typePool().get(node.isGlobOrBraceExpansion() ? TYPE::StringArray : TYPE::String);
  if (this->checkType(exprType, node.getPathNode()).isUnresolved()) {
    return;
  }

  std::unique_ptr<CmdArgNode> constPathNode;
  const auto &pathNode = node.getPathNode();
  for (auto &e : pathNode.getSegmentNodes()) {
    auto constNode = this->evalConstant(*e);
    if (!constNode) {
      return;
    }
    assert(isa<StringNode>(*constNode) || isa<WildCardNode>(*constNode) ||
           isa<BraceSeqNode>(*constNode));
    if (isa<StringNode>(*constNode)) {
      if (const auto ref = StringRef(cast<StringNode>(*constNode).getValue()); ref.hasNullChar()) {
        this->reportError<NullInPath>(pathNode);
        return;
      }
    }
    node.addConstNode(std::move(constNode));
  }
  this->resolvePathList(node);
}

} // namespace arsh