/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#include "glob.h"
#include "lexer.h"
#include "misc/format.hpp"
#include "vm.h"

namespace arsh {

static const char *toMessagePrefix(TildeExpandStatus status) {
  switch (status) {
  case TildeExpandStatus::OK:
  case TildeExpandStatus::NO_TILDE:
  case TildeExpandStatus::SIZE_LIMIT:
    break;
  case TildeExpandStatus::NO_USER:
    return "no such user: ";
  case TildeExpandStatus::NO_DIR_STACK:
    break;
  case TildeExpandStatus::UNDEF_OR_EMPTY:
    return "undefined or empty: ";
  case TildeExpandStatus::INVALID_NUM:
    return "invalid number format: ";
  case TildeExpandStatus::OUT_OF_RANGE:
    return "directory stack index out of range, up to directory stack limit ";
  case TildeExpandStatus::HAS_NULL:
    return "expanded path contains null characters: ";
  case TildeExpandStatus::EMPTY_ASSIGN:
    return "left-hand side of `=~' is empty";
  }
  assert(false);
  return "";
}

static void raiseTildeError(ARState &state, const DirStackProvider &provider,
                            const std::string &path, TildeExpandStatus status) {
  assert(status != TildeExpandStatus::OK);
  if (status == TildeExpandStatus::SIZE_LIMIT) {
    raiseStringLimit(state);
    return;
  }
  StringRef ref = path;
  if (const auto pos = ref.find("/"); pos != StringRef::npos) {
    ref = ref.substr(0, pos);
  }
  std::string str = toMessagePrefix(status);
  if (status == TildeExpandStatus::OUT_OF_RANGE) {
    str += "(";
    str += std::to_string(provider.size());
    str += "): ";
  }
  if (status != TildeExpandStatus::EMPTY_ASSIGN) {
    appendAsPrintable(ref, SYS_LIMIT_ERROR_MSG_MAX, str);
  }
  raiseError(state, TYPE::TildeError, std::move(str));
}

class DefaultDirStackProvider : public DirStackProvider {
private:
  const ARState &state;
  CStrPtr cwd;

  const ArrayObject &dirStack() const {
    return typeAs<ArrayObject>(this->state.getGlobal(BuiltinVarOffset::DIRSTACK));
  }

public:
  explicit DefaultDirStackProvider(const ARState &state) : state(state) {}

  size_t size() const override {
    return std::min(this->dirStack().size(), SYS_LIMIT_DIRSTACK_SIZE);
  }

  StringRef get(size_t index) override {
    const size_t size = this->size();
    if (index < size) {
      return this->dirStack().getValues()[index].asStrRef();
    }
    if (index == size) {
      this->cwd = this->state.getWorkingDir();
      return this->cwd.get();
    }
    return "";
  }
};

static bool expandTildeWithLeftHandSide(ARState &state, const StringRef left, std::string &path) {
  DefaultDirStackProvider dirStackProvider(state);
  TildeExpandStatus s;
  if (left == "=") {
    s = TildeExpandStatus::EMPTY_ASSIGN;
  } else {
    s = expandTilde(path, true, &dirStackProvider, StringObject::MAX_SIZE);
  }
  if (s != TildeExpandStatus::OK && state.has(RuntimeOption::FAIL_TILDE)) {
    raiseTildeError(state, dirStackProvider, path, s);
    return false;
  }
  return true;
}

bool VM::applyTildeExpansion(ARState &state, StringRef path, bool assign) {
  std::string str = path.toString();
  if (const StringRef left = assign ? state.stack.peek().asStrRef() : "";
      !expandTildeWithLeftHandSide(state, left, str)) {
    return false;
  }
  auto ret = Value::createStr(std::move(str));
  if (assign) {
    auto left = state.stack.pop();
    const bool r = concatAsStr(state, left, ret);
    if (r) {
      state.stack.push(std::move(left));
    }
    return r;
  }
  state.stack.push(std::move(ret));
  return true;
}

static void raiseGlobbingErrorWithNull(ARState &state, const GlobPatternWrapper &wrapper) {
  auto pattern = wrapper.join();
  std::string value = "glob pattern has null characters `";
  appendAsPrintable(pattern, SYS_LIMIT_ERROR_MSG_MAX - 1, value);
  value += "'";
  raiseError(state, TYPE::GlobError, std::move(value));
}

static void raiseGlobbingError(ARState &state, const GlobPatternWrapper &pattern,
                               std::string &&message) {
  std::string value = std::move(message);
  value += " `";
  pattern.join(SYS_LIMIT_ERROR_MSG_MAX - 1, value); // FIXME:
  value += "'";
  raiseError(state, TYPE::GlobError, std::move(value));
}

struct TildeAssign {
  unsigned int tildeCount{0};
  unsigned int assignCount{0};
};

static bool isTildeExpansion(const Value *begin, const Value *cur, TildeAssign p) {
  assert(cur->kind() == ValueKind::EXPAND_META);
  if (cur == begin || ((cur - 1)->kind() == ValueKind::EXPAND_META &&
                       (cur - 1)->asExpandMeta().first == ExpandMeta::ASSIGN)) {
    const auto meta = cur->asExpandMeta().first;
    return meta == ExpandMeta::TILDE && p.tildeCount == 0 && p.assignCount <= 1;
  }
  return false;
}

static Value concatPath(ARState &state, const Value *const constPool, const Value *const begin,
                        const Value *const end) {
  std::string out;
  std::string tmp;
  TildeAssign param;
  bool tilde = false;
  for (auto cur = begin; cur != end; ++cur) {
    if (cur->hasStrRef()) {
      if (!checkedAppend(cur->asStrRef(), StringObject::MAX_SIZE, tmp)) {
        goto RAISE;
      }
    } else if (cur->kind() == ValueKind::NUMBER) { // for escaped string
      const StringRef ref = constPool[cur->asNum()].asStrRef();
      if (!appendAsUnescaped(ref, StringObject::MAX_SIZE, tmp)) {
        goto RAISE;
      }
    } else {
      assert(cur->kind() == ValueKind::EXPAND_META);
      if (isTildeExpansion(begin, cur, param)) {
        tilde = true;
        if (!checkedAppend(tmp, StringObject::MAX_SIZE, out)) {
          goto RAISE;
        }
        tmp.clear();
      }

      const auto meta = cur->asExpandMeta().first;
      if (meta == ExpandMeta::TILDE) {
        param.tildeCount++;
      } else if (meta == ExpandMeta::ASSIGN) {
        param.assignCount++;
      }
      if (!checkedAppend(toString(meta), StringObject::MAX_SIZE, tmp)) {
        goto RAISE;
      }
    }
  }

  if (tilde) {
    assert(tmp[0] == '~');
    if (!expandTildeWithLeftHandSide(state, out, tmp)) {
      return {};
    }
  }
  if (out.empty()) {
    out = std::move(tmp);
  } else if (!checkedAppend(tmp, StringObject::MAX_SIZE, out)) {
    goto RAISE;
  }
  assert(out.size() <= StringObject::MAX_SIZE);
  return Value::createStr(std::move(out));

RAISE:
  raiseStringLimit(state);
  return {};
}

static Value joinAsPath(ARState &state, const GlobPatternWrapper &pattern) {
  auto ret = Value::createStr();
  if (!pattern.getBaseDir().empty()) {
    assert(pattern.getBaseDir().back() == '/');
    if (!ret.appendAsStr(state, pattern.getBaseDir())) {
      return {};
    }
  }
  std::string tmp;
  tmp.reserve(pattern.getPattern().size());
  if (const auto remain = StringObject::MAX_SIZE - ret.asStrRef().size();
      appendAsUnescaped(pattern.getPattern(), remain, tmp) && ret.appendAsStr(state, tmp)) {
    return ret;
  }
  return {};
}

static bool concatAsGlobPattern(ARState &state, const Value *const constPool,
                                const Value *const begin, const Value *const end,
                                GlobPatternWrapper &pattern) {
  std::string prefix;
  std::string out;
  bool tilde = false;
  bool globMeta = false;
  for (auto cur = begin; cur != end; ++cur) {
    if (auto &v = *cur; v.hasStrRef()) {
      if (!appendAndEscapeGlobMeta(v.asStrRef(), StringObject::MAX_SIZE, out)) {
        goto NOMEM;
      }
    } else if (v.kind() == ValueKind::NUMBER) { // for escaped string
      if (const StringRef ref = constPool[v.asNum()].asStrRef();
          !checkedAppend(ref, StringObject::MAX_SIZE, out)) {
        goto NOMEM;
      }
    } else {
      assert(v.kind() == ValueKind::EXPAND_META);
      const auto meta = v.asExpandMeta().first;
      if (meta == ExpandMeta::TILDE && !tilde && (cur == begin || !prefix.empty())) {
        tilde = true;
      }
      if (!checkedAppend(toString(meta), StringObject::MAX_SIZE, out)) {
        goto NOMEM;
      }
      if (meta == ExpandMeta::ASSIGN && prefix.empty() && !globMeta && !tilde) {
        prefix = out;
        out.clear();
      }
      if (isGlobStart(meta)) {
        globMeta = true;
      }
    }
  }

  assert(!out.empty());
  pattern = GlobPatternWrapper::create(std::move(out));
  if (tilde) {
    if (pattern.getBaseDir().empty()) {
      DefaultDirStackProvider provider(state); // for dummy
      raiseTildeError(state, provider, pattern.getPattern(), TildeExpandStatus::NO_USER);
      return {};
    }
    assert(pattern.getBaseDir()[0] == '~');
    if (!expandTildeWithLeftHandSide(state, prefix, pattern.refBaseDir())) {
      return {};
    }
  }
  if (const auto size = pattern.getBaseDir().size();
      size > StringObject::MAX_SIZE || prefix.size() > StringObject::MAX_SIZE - size) {
    goto NOMEM;
  }
  pattern.refBaseDir().insert(0, prefix);
  return true;

NOMEM:
  raiseStringLimit(state);
  return false;
}

bool VM::addGlobbingPath(ARState &state, ArrayObject &argv, const Value *const begin,
                         const Value *const end) {
  GlobPatternWrapper pattern;
  if (!concatAsGlobPattern(state, cast<CompiledCode>(state.stack.code())->getConstPool(), begin,
                           end, pattern)) {
    return false;
  }
  if (pattern.getBaseDir().empty() && pattern.getPattern().size() == 1 &&
      pattern.getPattern() == "[") { // ignore '['
    return argv.append(state, Value::createStr(pattern.getPattern()));
  }

  Glob::Option option{};
  if (state.has(RuntimeOption::DOTGLOB)) {
    setFlag(option, Glob::Option::DOTGLOB);
  }
  if (state.has(RuntimeOption::FASTGLOB)) {
    setFlag(option, Glob::Option::FASTGLOB);
  }
  if (state.has(RuntimeOption::GLOBSTAR)) {
    setFlag(option, Glob::Option::GLOBSTAR);
  }

  // check if glob path fragments have null character
  if (unlikely(StringRef(pattern.getBaseDir()).hasNullChar() ||
               StringRef(pattern.getPattern()).hasNullChar())) {
    if (state.has(RuntimeOption::NULLGLOB)) {
      return true; // do nothing
    }
    if (state.has(RuntimeOption::FAIL_GLOB)) {
      raiseGlobbingErrorWithNull(state, pattern);
      return false;
    }
    auto path = joinAsPath(state, pattern);
    return path && argv.append(state, std::move(path));
  }

  const unsigned int oldSize = argv.size();
  RuntimeCancelToken cancel;
  Glob glob(pattern, option);
  glob.setCancelToken(cancel);
  glob.setConsumer([&argv, &state](std::string &&path) {
    return argv.append(state, Value::createStr(std::move(path)));
  });

  std::string err;
  switch (const auto ret = glob(&err); ret) {
  case Glob::Status::MATCH:
  case Glob::Status::NOMATCH: {
    if (ret == Glob::Status::MATCH || state.has(RuntimeOption::NULLGLOB)) {
      argv.sortAsStrArray(oldSize); // not check iterator invalidation
      return true;
    }
    if (state.has(RuntimeOption::FAIL_GLOB)) {
      raiseGlobbingError(state, pattern, "no matches for glob pattern");
      return false;
    }
    auto path = joinAsPath(state, pattern);
    return path && argv.append(state, std::move(path));
  }
  case Glob::Status::CANCELED:
    raiseSystemError(state, EINTR, "glob expansion is canceled");
    return false;
  case Glob::Status::BAD_PATTERN:
    raiseGlobbingError(state, pattern, err + " in");
    return false;
  case Glob::Status::RESOURCE_LIMIT:
    raiseSystemError(state, glob.getErrNum(), "glob expansion failed");
    return false;
  case Glob::Status::RECURSION_DEPTH_LIMIT:
    raiseError(state, TYPE::StackOverflowError, "glob recursion depth reaches limit");
    return false;
  default:
    assert(ret == Glob::Status::LIMIT && state.hasError());
    return false;
  }
}

struct ExpandState {
  unsigned char index;
  unsigned char usedSize;
  unsigned char closeIndex;
  unsigned char braceId;

  struct Compare {
    bool operator()(const ExpandState &x, unsigned int y) const { return x.braceId < y; }

    bool operator()(unsigned int x, const ExpandState &y) const { return x < y.braceId; }
  };
};

static bool needGlob(const Value *begin, const Value *end) {
  for (; begin != end; ++begin) {
    if (const auto &v = *begin; v.kind() == ValueKind::EXPAND_META) {
      if (isGlobStart(v.asExpandMeta().first)) {
        return true;
      }
    }
  }
  return false;
}

/**
 *
 * @param cur
 * @param obj
 * (begin, end, step, (digits, kind))
 * @return
 */
static bool tryUpdate(int64_t &cur, const BaseObject &obj) {
  auto &nums = obj[3].asNumList();
  const BraceRange range = {
      .begin = obj[0].asInt(),
      .end = obj[1].asInt(),
      .step = obj[2].asInt(),
      .digits = nums[0],
      .kind = static_cast<BraceRange::Kind>(nums[1]),
  };
  return tryUpdateSeqValue(cur, range);
}

bool VM::applyBraceExpansion(ARState &state, ArrayObject &argv, const Value *begin,
                             const Value *end) {
  const unsigned int size = end - begin;
  assert(size <= UINT8_MAX);
  FlexBuffer<ExpandState> stack;
  auto values = std::make_unique<Value[]>(size + 1); // reserve sentinel
  FlexBuffer<int64_t> seqStack;
  unsigned int usedSize = 0;

  for (unsigned int i = 0; i < size; i++) {
    auto &v = begin[i];
    if (v.kind() == ValueKind::EXPAND_META) {
      auto meta = v.asExpandMeta();
      switch (meta.first) {
      case ExpandMeta::BRACE_OPEN: {
        // find close index
        unsigned int closeIndex = i + 1;
        for (int level = 1; closeIndex < size; closeIndex++) {
          if (begin[closeIndex].kind() == ValueKind::EXPAND_META) {
            const auto next = begin[closeIndex].asExpandMeta();
            if (next.first == ExpandMeta::BRACE_CLOSE) {
              if (--level == 0) {
                break;
              }
            } else if (next.first == ExpandMeta::BRACE_OPEN) {
              level++;
            }
          }
        }
        stack.push_back(ExpandState{
            .index = static_cast<unsigned char>(i),
            .usedSize = static_cast<unsigned char>(usedSize),
            .closeIndex = static_cast<unsigned char>(closeIndex),
            .braceId = static_cast<unsigned char>(meta.second),
        });
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEP:
      case ExpandMeta::BRACE_CLOSE: {
        const auto iter =
            std::lower_bound(stack.begin(), stack.end(), meta.second, ExpandState::Compare());
        assert(iter != stack.end());
        iter->index = i;
        i = iter->closeIndex;
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEQ_OPEN: {
        i++;
        stack.push_back(ExpandState{
            .index = static_cast<unsigned char>(i + 1),
            .usedSize = static_cast<unsigned char>(usedSize),
            .closeIndex = static_cast<unsigned char>(i + 1),
            .braceId = static_cast<unsigned char>(meta.second),
        });

        auto &seq = typeAs<BaseObject>(begin[i]); // (begin, end, step, (digits, kind))
        assert(seq.getFieldSize() == 4);
        seqStack.push_back(seq[0].asInt());
        goto CONTINUE;
      }
      case ExpandMeta::BRACE_SEQ_CLOSE: {
        auto &nums = typeAs<BaseObject>(begin[i - 1])[3].asNumList();
        const unsigned int digits = nums[0];
        const auto kind = static_cast<BraceRange::Kind>(nums[1]);
        values[usedSize++] = Value::createStr(
            formatSeqValue(seqStack.back(), digits, kind == BraceRange::Kind::CHAR));
        goto CONTINUE;
      }
      default:
        break;
      }
    }
    values[usedSize++] = v;

  CONTINUE:
    if (i == size - 1) {
      if (unlikely(ARState::isInterrupted())) {
        raiseSystemError(state, EINTR, "brace expansion is canceled");
        return false;
      }

      values[usedSize] = Value(); // sentinel

      const auto *vbegin = values.get();
      const auto *vend = vbegin + usedSize;
      if (needGlob(vbegin, vend)) {
        if (!addGlobbingPath(state, argv, vbegin, vend)) {
          return false;
        }
      } else {
        Value path =
            concatPath(state, cast<CompiledCode>(state.stack.code())->getConstPool(), vbegin, vend);
        if (!path) {
          return false;
        }
        if (!path.asStrRef().empty()) { // skip empty string
          if (!argv.append(state, std::move(path))) {
            return false;
          }
        }
      }

      while (!stack.empty()) {
        const unsigned int oldIndex = stack.back().index;
        auto &old = begin[oldIndex];
        assert(old.kind() == ValueKind::EXPAND_META);
        const auto meta = old.asExpandMeta().first;
        if (meta == ExpandMeta::BRACE_CLOSE) {
          stack.pop_back();
        } else if (meta == ExpandMeta::BRACE_SEQ_CLOSE) {
          if (tryUpdate(seqStack.back(), typeAs<BaseObject>(begin[oldIndex - 1]))) {
            i = oldIndex - 1;
            usedSize = stack.back().usedSize;
            break;
          } else {
            stack.pop_back();
            seqStack.pop_back();
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

bool VM::addExpandingPath(ARState &state, const unsigned int size, ExpandOp expandOp) {
  /**
   * stack layout
   *
   * ===========> stack grow
   * +------+-------+--------+     +--------+----------+
   * | argv | redir | param1 | ~~~ | paramN | paramN+1 |
   * +------+-------+--------+     +--------+----------+
   *
   * after evaluation
   * +------+-------+
   * | argv | redir |
   * +------+-------+
   */
  auto &argv = typeAs<ArrayObject>(state.stack.peekByOffset(size + 2));
  const auto *begin = &state.stack.peekByOffset(size);
  const auto *end = &state.stack.peek();
  bool ret;
  if (hasFlag(expandOp, ExpandOp::BRACE)) {
    ret = applyBraceExpansion(state, argv, begin, end);
  } else {
    ret = addGlobbingPath(state, argv, begin, end);
  }

  if (ret) { // pop operands
    for (unsigned int i = 0; i <= size; i++) {
      state.stack.popNoReturn();
    }
  }
  return ret;
}

} // namespace arsh