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
  }
  assert(false);
  return "";
}

static void raiseTildeError(ARState &state, const DirStackProvider &provider,
                            const std::string &path, TildeExpandStatus status) {
  assert(status != TildeExpandStatus::OK);
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
  str += ref;
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

bool VM::applyTildeExpansion(ARState &state, StringRef path) {
  std::string str = path.toString();
  DefaultDirStackProvider dirStackProvider(state);
  if (const auto s = expandTilde(str, true, &dirStackProvider);
      s != TildeExpandStatus::OK && state.has(RuntimeOption::FAIL_TILDE)) {
    raiseTildeError(state, dirStackProvider, str, s);
    return false;
  }
  state.stack.push(Value::createStr(std::move(str)));
  return true;
}

static void raiseGlobbingErrorWithNull(ARState &state, const std::string &pattern) {
  std::string value = "glob pattern has null characters `";
  splitByDelim(pattern, '\0', [&value](StringRef ref, bool delim) {
    value += ref;
    if (delim) {
      value += "\\x00";
    }
    return true;
  });
  value += "'";
  raiseError(state, TYPE::GlobbingError, std::move(value));
}

static void raiseGlobbingError(ARState &state, const GlobPatternWrapper &pattern,
                               std::string &&message) {
  std::string value = std::move(message);
  value += " `";
  pattern.join(StringObject::MAX_SIZE, value); // FIXME: check length
  value += "'";
  raiseError(state, TYPE::GlobbingError, std::move(value));
}

static Value concatPath(ARState &state, const Value *const constPool, const Value *begin,
                        const Value *end) {
  auto ret = Value::createStr();
  for (; begin != end; ++begin) {
    if (begin->hasStrRef()) {
      if (ret.appendAsStr(state, begin->asStrRef())) {
        continue;
      }
    } else if (begin->kind() == ValueKind::NUMBER) { // for escaped string
      const StringRef ref = constPool[begin->asNum()].asStrRef();
      std::string tmp;
      const bool r = appendAsUnescaped(ref, StringObject::MAX_SIZE, tmp);
      (void)r;
      assert(r);
      if (ret.appendAsStr(state, tmp)) {
        continue;
      }
    } else {
      assert(begin->kind() == ValueKind::EXPAND_META);
      if (ret.appendAsStr(state, begin->toString())) {
        continue;
      }
    }
    return {};
  }
  return ret;
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
  const bool r = appendAsUnescaped(pattern.getPattern(), StringObject::MAX_SIZE, tmp);
  (void)r;
  assert(r);
  if (!ret.appendAsStr(state, tmp)) {
    return {};
  }
  return ret;
}

static bool concatAsGlobPattern(ARState &state, const Value *const constPool, const Value *begin,
                                const Value *end, const bool tilde, GlobPatternWrapper &pattern) {
  std::string out;
  for (; begin != end; ++begin) {
    if (auto &v = *begin; v.hasStrRef()) {
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
      if (!checkedAppend(v.toString(), StringObject::MAX_SIZE, out)) {
        goto NOMEM;
      }
    }
  }
  // check if glob path fragments have null character
  if (const StringRef ref = out; unlikely(ref.hasNullChar())) {
    raiseGlobbingErrorWithNull(state, out);
    return false;
  }
  pattern = GlobPatternWrapper::create(std::move(out));
  if (tilde && !pattern.getBaseDir().empty()) {
    DefaultDirStackProvider provider(state);
    if (const auto s = pattern.expandTilde(&provider);
        s != TildeExpandStatus::OK && state.has(RuntimeOption::FAIL_TILDE)) {
      raiseTildeError(state, provider, pattern.getBaseDir(), s);
      return false;
    }
  }
  return true;

NOMEM:
  raiseError(state, TYPE::OutOfRangeError, ERROR_STRING_LIMIT);
  return false;
}

bool VM::addGlobbingPath(ARState &state, ArrayObject &argv, const Value *const begin,
                         const Value *const end, const bool tilde) {
  GlobPatternWrapper pattern;
  if (!concatAsGlobPattern(state, cast<CompiledCode>(state.stack.code())->getConstPool(), begin,
                           end, tilde, pattern)) {
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
    //  case Glob::Status::RESOURCE_LIMIT:
    //    return false; //FIXME:
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
    if (const auto &v = *begin;
        v.kind() == ValueKind::EXPAND_META && v.asExpandMeta().first != ExpandMeta::BRACKET_CLOSE) {
      return true;
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
                             const Value *end, ExpandOp expandOp) {
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
      case ExpandMeta::BRACE_TILDE:
        if (usedSize) {
          goto CONTINUE;
        }
        break;
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
      bool tilde = hasFlag(expandOp, ExpandOp::TILDE);
      if (!tilde && usedSize > 0 && vbegin->kind() == ValueKind::EXPAND_META &&
          vbegin->asExpandMeta().first == ExpandMeta::BRACE_TILDE) {
        tilde = true;
        ++vbegin; // skip meta
      }

      if (needGlob(vbegin, vend)) {
        if (!addGlobbingPath(state, argv, vbegin, vend, tilde)) {
          return false;
        }
      } else {
        Value path =
            concatPath(state, cast<CompiledCode>(state.stack.code())->getConstPool(), vbegin, vend);
        if (!path) {
          return false;
        }
        if (tilde) {
          DefaultDirStackProvider dirStackProvider(state);
          std::string str = path.asStrRef().toString();
          if (const auto s = expandTilde(str, true, &dirStackProvider);
              s != TildeExpandStatus::OK && state.has(RuntimeOption::FAIL_TILDE)) {
            raiseTildeError(state, dirStackProvider, str, s);
            return false;
          }
          path = Value::createStr(std::move(str));
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
    ret = applyBraceExpansion(state, argv, begin, end, expandOp);
  } else {
    ret = addGlobbingPath(state, argv, begin, end, hasFlag(expandOp, ExpandOp::TILDE));
  }

  if (ret) { // pop operands
    for (unsigned int i = 0; i <= size; i++) {
      state.stack.popNoReturn();
    }
  }
  return ret;
}

} // namespace arsh