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

#include <sys/wait.h>

#include <algorithm>
#include <cassert>

#include "candidates.h"
#include "compiler.h"
#include "format_util.h"
#include "logger.h"
#include "misc/files.hpp"
#include "misc/inlined_stack.hpp"
#include "misc/num_util.hpp"
#include "misc/pty.hpp"
#include "node.h"
#include "object_util.h"
#include "vm.h"

extern char **environ; // NOLINT

namespace arsh {

// core api definition
const Value &getBuiltinGlobal(const ARState &st, const char *varName) {
  auto &modType = st.typePool.getBuiltinModType();
  auto handle = modType.lookup(st.typePool, varName);
  assert(handle != nullptr);
  return st.getGlobal(handle->getIndex());
}

void reassignReplyVar(ARState &st) {
  if (auto &obj = typeAs<OrderedMapObject>(st.getGlobal(BuiltinVarOffset::REPLY_VAR));
      obj.getRefcount() > 1) {
    auto &type = st.typePool.get(obj.getTypeID());
    st.setGlobal(BuiltinVarOffset::REPLY_VAR,
                 Value::create<OrderedMapObject>(type, st.getRng().next()));
  } else { // reuse existing object
    obj.clear();
  }
}

bool syncWinSize(ARState &st, WinSize *size) {
  errno = 0;
  const auto [s, b] = getWinSize(st.getTTYFd());
  const int old = errno;
  if (b) {
    if (size) {
      *size = s;
    }
    st.setGlobal(BuiltinVarOffset::LINES, Value::createInt(s.rows));
    st.setGlobal(BuiltinVarOffset::COLUMNS, Value::createInt(s.cols));
  }
  errno = old;
  return b;
}

void raiseError(ARState &st, TYPE type, std::string &&message, int64_t status) {
  assert(!st.hasError());
  message.resize(std::min(message.size(), SYS_LIMIT_ERROR_MSG_MAX)); // truncate
  auto except = ErrorObject::newError(st, st.typePool.get(type),
                                      Value::createStr(std::move(message)), status);
  st.throwObject(std::move(except));
}

void raiseSystemError(ARState &st, int errorNum, std::string &&message) {
  assert(errorNum != 0);
  if (errorNum == EINTR) {
    /**
     * if EINTR, already raised SIGINT. and SIGINT handler also raises SystemError.
     * due to eliminate redundant SystemError, force clear SIGINT
     */
    SignalGuard guard;
    ARState::clearPendingSignal(SIGINT);
  }
  std::string str(std::move(message));
  if (!str.empty()) {
    str += ", ";
  }
  str += "caused by `";
  str += strerror(errorNum);
  str += "'";
  raiseError(st, TYPE::SystemError, std::move(str));
}

static std::string toPrintable(const TypePool &pool, const Value &value) {
  StrAppender appender(SYS_LIMIT_PRINTABLE_MAX >> 2);
  Stringifier stringifier(pool, appender);
  appender.setAppendOp(StrAppender::Op::PRINTABLE);
  stringifier.addAsStr(value);
  return std::move(appender).take();
}

void raiseAssertFail(ARState &st, Value &&msg, const AssertOp op, Value &&left, Value &&right) {
  static_assert(SYS_LIMIT_PRINTABLE_MAX <= SYS_LIMIT_ERROR_MSG_MAX);
  constexpr auto MAX_PRINTABLE = SYS_LIMIT_PRINTABLE_MAX >> 2;
  std::string value;
  if (op != AssertOp::DEFAULT) {
    const StringRef ref = msg.asStrRef();
    value.append(ref.data(), std::min(ref.size(), MAX_PRINTABLE));
  }

  switch (op) {
  case AssertOp::DEFAULT:
    break;
  case AssertOp::EQ:
  case AssertOp::MATCH: {
    value += "\nbinary expression `<LHS> ";
    value += op == AssertOp::EQ ? "==" : "=~";
    value += " <RHS>' is false\n";
    value += "  <LHS>: ";
    value += st.typePool.get(left.getTypeID()).getNameRef();
    value += " = ";
    value += toPrintable(st.typePool, left);
    value += "\n  <RHS>: ";
    value += st.typePool.get(right.getTypeID()).getNameRef();
    value += " = ";
    value += toPrintable(st.typePool, right);
    msg = Value::createStr(std::move(value));
    break;
  }
  case AssertOp::IS: {
    value += "\nbinary expression `<EXPR> is <TYPE>' is false\n";
    value += "  <EXPR>: ";
    if (left.isInvalid()) {
      value += "(invalid)";
    } else {
      auto &exprType = st.typePool.get(left.getTypeID());
      appendAsPrintable(exprType.getNameRef(), MAX_PRINTABLE + value.size(), value);
    }
    value += "\n  <TYPE>: ";
    auto &targetType = st.typePool.get(right.getTypeID());
    appendAsPrintable(targetType.getNameRef(), MAX_PRINTABLE + value.size(), value);
    msg = Value::createStr(std::move(value));
    break;
  }
  }
  auto except = ErrorObject::newError(st, st.typePool.get(TYPE::AssertFail_), std::move(msg), 1);
  st.throwObject(std::move(except));
}

bool printErrorAt(const ARState &state, const ArrayObject &argvObj, StringRef sub, int errNum,
                  const char *fmt, ...) {
  // get current frame
  std::string sourceName;
  unsigned int lineNum = 0;
  state.getCallStack().fillStackTrace([&sourceName, &lineNum](StackTraceElement &&f) {
    sourceName = f.getSourceName();
    lineNum = f.getLineNum();
    return false; // get current frame only
  });
  std::string out;
  if (sourceName.empty()) {
    out += "arsh: ";
  } else {
    StringRef ref = sourceName;
    if (auto r = ref.lastIndexOf("/"); r != StringRef ::npos) {
      ref = ref.substr(r + 1);
    }
    out += ref;
    out += ":";
    out += std::to_string(lineNum);
    out += ": ";
  }
  if (const StringRef cmdName = argvObj[0].asStrRef(); !cmdName.empty()) {
    appendAsPrintable(cmdName, SYS_LIMIT_PRINTABLE_MAX, out);
    if (!sub.empty()) {
      out += " ";
      appendAsPrintable(sub, SYS_LIMIT_PRINTABLE_MAX, out);
    }
    out += ": ";
  }

  va_list arg;
  va_start(arg, fmt);
  vformatTo(out, fmt, arg);
  va_end(arg);

  if (errNum) {
    out += ": ";
    out += strerror(errNum);
  }
  out += "\n";
  return fwrite(out.c_str(), sizeof(char), out.size(), stderr) == 0;
}

static bool isUnhandledSignal(int sigNum) {
  switch (sigNum) {
  case SIGBUS:
  case SIGSEGV:
  case SIGILL:
  case SIGFPE:
    /**
     * not handle or ignore these signals due to prevent undefined behavior.
     * see.
     * https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
     * http://man7.org/linux/man-pages/man2/sigaction.2.html
     */
    return true;
  case SIGKILL:
  case SIGSTOP:
    /**
     * sigaction does not accept these signals
     */
    return true;
  case SIGCHLD:
    /**
     * for automatically wait process termination
     */
    return true;
  default:
    return false;
  }
}

// when called this handler, all signals are blocked due to signal mask
static void signalHandler(int sigNum) { ARState::pendingSigSet.add(sigNum); }

static struct sigaction newSigaction(int sigNum) {
  struct sigaction action{};
  if (sigNum != SIGINT) { // always restart system call except for SIGINT
    action.sa_flags = SA_RESTART;
  }
  sigfillset(&action.sa_mask);
  return action;
}

static ObjPtr<Object> installUnblock(ARState &st, const int sigNum, ObjPtr<Object> handler) {
  auto DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL).toPtr();
  auto IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN).toPtr();

  // save old handler
  auto oldHandler = st.sigVector.lookup(sigNum);

  // set actual signal handler
  struct sigaction oldAction{};
  struct sigaction newAction = newSigaction(sigNum);
  const struct sigaction *action = nullptr;
  if (handler && !isUnhandledSignal(sigNum)) {
    if (handler == DFL_handler) {
      newAction.sa_handler = SIG_DFL;
      handler = nullptr;
    } else if (handler == IGN_handler) {
      newAction.sa_handler = SIG_IGN;
      handler = nullptr;
    } else {
      newAction.sa_handler = signalHandler;
    }
    if (sigNum == SIGWINCH) { // always set handler
      newAction.sa_handler = signalHandler;
    }
    action = &newAction;
    st.sigVector.insertOrUpdate(sigNum, std::move(handler));
  }
  sigaction(sigNum, action, &oldAction);

  if (!oldHandler) {
    if (oldAction.sa_handler == SIG_IGN) {
      oldHandler = IGN_handler;
    } else {
      oldHandler = DFL_handler;
    }
  }
  return oldHandler;
}

ObjPtr<Object> installSignalHandler(ARState &st, int sigNum, ObjPtr<Object> handler) {
  SignalGuard guard;
  return installUnblock(st, sigNum, std::move(handler));
}

void installSignalHandler(ARState &st, AtomicSigSet &sigSet, const ObjPtr<Object> &handler) {
  SignalGuard guard;
  while (!sigSet.empty()) {
    int sigNum = sigSet.popPendingSig();
    installUnblock(st, sigNum, handler);
  }
}

void setJobControlSignalSetting(ARState &st, bool set) {
  SignalGuard guard;

  auto DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL).toPtr();
  auto IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN).toPtr();

  auto handle = set ? IGN_handler : DFL_handler;
  if (set) {
    installUnblock(st, SIGINT, toObjPtr<FuncObject>(getBuiltinGlobal(st, VAR_DEF_SIGINT)));
  } else {
    installUnblock(st, SIGINT, handle);
  }
  installUnblock(st, SIGQUIT, handle);
  installUnblock(st, SIGTSTP, handle);
  installUnblock(st, SIGTTIN, handle);
  installUnblock(st, SIGTTOU, handle);
}

void setSignalSetting(const ARState &state) {
  SignalGuard guard;

  AtomicSigSet set;
  for (auto &e : state.sigVector.getData()) {
    int sigNum = e.first;
    set.add(sigNum);
    assert(!isUnhandledSignal(sigNum));
    auto action = newSigaction(sigNum);
    action.sa_handler = signalHandler;
    sigaction(sigNum, &action, nullptr);
  }
  {
    auto action = newSigaction(SIGCHLD);
    action.sa_handler = signalHandler;
    sigaction(SIGCHLD, &action, nullptr);
  }
  if (constexpr int sig = SIGWINCH; !set.has(sig)) {
    auto action = newSigaction(sig);
    action.sa_handler = signalHandler;
    sigaction(sig, &action, nullptr);
  }
}

void resetSignalSettingUnblock(ARState &state) {
  for (auto &e : state.sigVector.getData()) {
    if (e.first == SIGWINCH) {
      continue;
    }
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigaction(e.first, &action, nullptr);
  }
  state.sigVector.clear();
}

void setLocaleSetting() {
  setlocale(LC_ALL, "");
  setlocale(LC_MESSAGES, "C");
  setlocale(LC_NUMERIC, "C"); // always use C locale (for std::to_string)
  Locale::restore();
}

const ModType *getRuntimeModuleByLevel(const ARState &state, const unsigned int callLevel) {
  const CompiledCode *code = nullptr;
  unsigned int depth = 0;
  state.getCallStack().walkFrames([&](const ControlFrame &frame) {
    auto *c = frame.code;
    if (c->is(CodeKind::NATIVE)) {
      return true; // continue
    }
    if (depth == callLevel) {
      code = cast<CompiledCode>(c);
      return false;
    }
    depth++;
    return true;
  });
  if (code) {
    auto ret = state.typePool.getModTypeById(code->getBelongedModId());
    assert(ret);
    return ret;
  }
  return nullptr;
}

const ModType &getCurRuntimeModule(const ARState &state) {
  if (auto *type = getRuntimeModuleByLevel(state, 0)) {
    return *type;
  }
  auto *type = state.typePool.getModTypeById(ROOT_MOD_ID);
  assert(type);
  return *type;
}

bool RuntimeCancelToken::isCanceled() const {
  const bool s = ARState::isInterrupted();
  if (s && this->clearSignal) {
    ARState::clearPendingSignal(SIGINT);
  }
  return s;
}

class DefaultCompConsumer : public CompCandidateConsumer {
private:
  ARState &state;
  ObjPtr<CandidatesObject> reply;
  bool overflow{false};
  const bool putDesc;

public:
  DefaultCompConsumer(ARState &state, bool putDesc) : state(state), putDesc(putDesc) {}

  void operator()(CompCandidate &&candidate) override {
    if (!this->reply) {
      this->reply = createObject<CandidatesObject>();
    }
    if (this->overflow) {
      return; // do nothing
    }
    const bool needSpace = candidate.needSuffixSpace();
    if (this->putDesc) {
      if (candidate.kind == CompCandidateKind::COMMAND_NAME) {
        auto kind = CandidateAttr::Kind::NONE;
        switch (candidate.getCmdNameType()) {
        case CompCandidate::CmdNameType::MOD:
          kind = CandidateAttr::Kind::CMD_MOD;
          break;
        case CompCandidate::CmdNameType::UDC:
          kind = CandidateAttr::Kind::CMD_UDC;
          break;
        case CompCandidate::CmdNameType::BUILTIN:
          kind = CandidateAttr::Kind::CMD_BUILTIN;
          break;
        case CompCandidate::CmdNameType::DYNA_UDC:
          kind = CandidateAttr::Kind::CMD_DYNA;
          break;
        case CompCandidate::CmdNameType::EXTERNAL:
          kind = CandidateAttr::Kind::CMD_EXTERNAL;
          break;
        }
        const CandidateAttr attr{kind, needSpace};
        this->overflow =
            !this->reply->addNewCandidateFrom(this->state, std::move(candidate.value), attr);
        return;
      }
      if (candidate.kind == CompCandidateKind::KEYWORD && isIdentifierStart(candidate.value[0])) {
        this->overflow =
            !this->reply->addNewCandidateFrom(this->state, std::move(candidate.value),
                                              CandidateAttr{CandidateAttr::Kind::KEYWORD, true});
        return;
      }

      const std::string typeSig = candidate.formatTypeSignature(this->state.typePool);
      if (!typeSig.empty()) {
        auto suffix = CandidateAttr::Suffix::NONE;
        if (needSpace) {
          suffix = CandidateAttr::Suffix::SPACE;
        } else if (candidate.kind == CompCandidateKind::METHOD ||
                   candidate.kind == CompCandidateKind::NATIVE_METHOD) {
          suffix = StringRef(typeSig).startsWith("()") ? CandidateAttr::Suffix::PAREN_PAIR
                                                       : CandidateAttr::Suffix::PAREN;
        }
        const CandidateAttr attr{CandidateAttr::Kind::TYPE_SIGNATURE, suffix};
        this->overflow =
            !this->reply->addNewCandidateWith(this->state, candidate.value, typeSig, attr);
        return;
      }
    }
    if (!this->reply->addNewCandidateFrom(this->state, std::move(candidate.value), needSpace)) {
      this->overflow = true;
    }
  }

  void addAll(ObjPtr<CandidatesObject> &&o) {
    if (!this->overflow) {
      if (!this->reply) {
        if (o->getRefcount() > 1) {
          this->reply = o->copy();
        } else {
          this->reply = std::move(o);
        }
        return;
      }
      if (!this->reply->addAll(this->state, *o)) {
        this->overflow = true;
      }
    }
  }

  ObjPtr<CandidatesObject> finalize() && {
    if (this->reply) {
      this->reply->sortAndDedup();
    } else {
      this->reply = createObject<CandidatesObject>();
    }
    return std::move(this->reply);
  }
};

static Value getFullNameFromTempMod(const ARState &state, const unsigned int temModIndex,
                                    const CmdNode &cmdNode, unsigned int offset,
                                    const ModType *cmdModType) {
  const StringRef name = !cmdModType
                             ? cmdNode.getNameNode().getValue()
                             : *cast<CmdArgNode>(*cmdNode.getArgNodes()[offset]).getConstArg();
  Value cmdName;
  const ModType *modType = cmdModType;
  if (!modType) {
    modType = state.typePool.getModTypeById(state.tempModScope[temModIndex]->modId);
  }
  if (modType) {
    if (auto retName = resolveFullCommandName(state, name, *modType, true); !retName.empty()) {
      cmdName = Value::createStr(std::move(retName));
    }
  }
  if (!cmdName) {
    cmdName = Value::createStr(name);
  }
  return cmdName;
}

static Value createArgv(const ARState &state, unsigned int tempModIndex,
                        const CodeCompletionContext &ctx, unsigned int offset,
                        const ModType *cmdModType) {
  std::vector<Value> values;

  // add cmd
  values.push_back(
      getFullNameFromTempMod(state, tempModIndex, *ctx.getCmdNode(), offset, cmdModType));

  // add args
  auto &argNodes = ctx.getCmdNode()->getArgNodes();
  for (unsigned int i = offset + (cmdModType ? 1 : 0); i < argNodes.size(); i++) {
    if (auto &e = *argNodes[i]; isa<CmdArgNode>(e)) {
      values.push_back(Value::createStr(ctx.getLexer()->toStrRef(e.getToken())));
    }
  }

  // add last arg
  if (auto &word = ctx.getCompWord(); !word.empty()) {
    std::string actual;
    if (!hasFlag(ctx.getFallbackOp(), CodeCompOp::TILDE) && StringRef(word).startsWith("~")) {
      actual += "\\";
    }
    actual += word;
    values.push_back(Value::createStr(std::move(actual)));
  }

  return Value::create<ArrayObject>(state.typePool.get(TYPE::StringArray), std::move(values));
}

static CmdArgCompStatus callCompCallback(ARState &state, Value &&func, CallArgs &&args,
                                         DefaultCompConsumer &consumer) {
  auto oldStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto oldIFS = state.getGlobal(BuiltinVarOffset::IFS);
  state.setGlobal(BuiltinVarOffset::IFS, Value::createStr(VAL_DEFAULT_IFS)); // set to default
  auto ret = VM::callFunction(state, std::move(func), std::move(args));
  state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldStatus));
  state.setGlobal(BuiltinVarOffset::IFS, std::move(oldIFS));
  if (state.hasError()) {
    return CmdArgCompStatus::CANCEL;
  }
  if (ret.isInvalid()) {
    return CmdArgCompStatus::INVALID;
  }
  consumer.addAll(toObjPtr<CandidatesObject>(std::move(ret)));
  return CmdArgCompStatus::OK;
}

static CmdArgCompStatus kickCompHook(ARState &state, const unsigned int tempModIndex,
                                     const CodeCompletionContext &ctx, const unsigned int offset,
                                     const ModType *cmdModType, DefaultCompConsumer &consumer) {
  auto hook = getBuiltinGlobal(state, VAR_COMP_HOOK);
  if (hook.isInvalid()) {
    return CmdArgCompStatus::INVALID;
  }

  // prepare argument
  auto dummyMod = Value::createDummy(state.typePool.get(TYPE::Module), tempModIndex);
  auto argv = createArgv(state, tempModIndex, ctx, offset, cmdModType);
  unsigned int index = typeAs<ArrayObject>(argv).size();
  if (!ctx.getCompWord().empty()) {
    index--;
  }

  // kick completion callback
  return callCompCallback(state, std::move(hook),
                          makeArgs(std::move(dummyMod), std::move(argv), Value::createInt(index)),
                          consumer);
}

struct ResolvedTempMod {
  unsigned int index{0};
  bool needDiscard{false};
  bool valid{false};

  explicit operator bool() const { return this->valid; }
};

static ResolvedTempMod resolveTempModScope(ARState &state, StringRef desc, bool dupMod) {
  if (desc.startsWith(OBJ_TEMP_MOD_PREFIX) && desc.endsWith(")")) {
    auto id = desc;
    id.removePrefix(strlen(OBJ_TEMP_MOD_PREFIX));
    id.removeSuffix(1);
    auto pair = convertToNum10<unsigned int>(id.begin(), id.end());
    if (!pair || pair.value >= state.tempModScope.size()) {
      return {};
    }

    ResolvedTempMod ret = {
        .index = pair.value,
        .needDiscard = false,
        .valid = true,
    };
    if (dupMod) {
      state.tempModScope.push_back(state.tempModScope[ret.index]->cloneGlobal());
      ret.index = state.tempModScope.size() - 1;
      ret.needDiscard = true;
    }
    return ret;
  } else {
    const ModType *modType = nullptr;
    if (desc.empty()) {
      modType = &getCurRuntimeModule(state);
    } else if (desc.startsWith(OBJ_MOD_PREFIX) && desc.endsWith(")")) {
      auto typeName = desc;
      typeName.removePrefix(strlen(OBJ_MOD_PREFIX));
      typeName.removeSuffix(1);
      if (auto ret = state.typePool.getType(typeName); ret && ret->isModType()) {
        modType = cast<ModType>(ret);
      }
    }
    if (!modType) {
      return {};
    }
    state.tempModScope.push_back(NameScope::reopen(state.typePool, *state.rootModScope, *modType));
    return {
        .index = static_cast<unsigned int>(state.tempModScope.size() - 1),
        .needDiscard = true,
        .valid = true,
    };
  }
}

static void discardTempMod(std::vector<NameScopePtr> &tempModScopes, ResolvedTempMod resolved) {
  assert(!tempModScopes.empty());
  if (resolved.needDiscard && resolved.index == tempModScopes.size() - 1) {
    tempModScopes.pop_back();
  }
}

class DefaultForeignCompHandler : public ForeignCompHandler {
private:
  ARState &state;
  ResolvedTempMod resolvedMod;

public:
  DefaultForeignCompHandler(ARState &state, ResolvedTempMod resolvedMod)
      : state(state), resolvedMod(resolvedMod) {}

  ~DefaultForeignCompHandler() override = default;

  CmdArgCompStatus callUserDefinedComp(const CodeCompletionContext &ctx, unsigned offset,
                                       const ModType *cmdModType,
                                       CompCandidateConsumer &consumer) override {
    return kickCompHook(this->state, this->resolvedMod.index, ctx, offset, cmdModType,
                        static_cast<DefaultCompConsumer &>(consumer));
  }

  CmdArgCompStatus callCLIComp(const FuncHandle &handle, StringRef opt, StringRef word,
                               CompCandidateConsumer &consumer) override {
    auto func = this->state.tryToGetGlobal(handle.getIndex());
    if (!func) { // maybe uninitialized
      return CmdArgCompStatus::INVALID;
    }
    return callCompCallback(this->state, std::move(func),
                            makeArgs(Value::createStr(opt), Value::createStr(word)),
                            static_cast<DefaultCompConsumer &>(consumer));
  }

  void completeDynamicUdc(const std::string &word, CompCandidateConsumer &consumer) override {
    auto &dynaUdcs = typeAs<OrderedMapObject>(this->state.getGlobal(BuiltinVarOffset::DYNA_UDCS));
    for (auto &e : dynaUdcs.getEntries()) {
      if (!e) {
        continue;
      }
      if (const auto name = e.getKey().asStrRef(); name.startsWith(word)) {
        CompCandidate candidate(word, CompCandidateKind::COMMAND_NAME, name);
        candidate.setCmdNameType(CompCandidate::CmdNameType::DYNA_UDC);
        consumer(std::move(candidate));
      }
    }
  }
};

static bool completeImpl(ARState &st, ResolvedTempMod resolvedMod, StringRef source,
                         const CodeCompOp option, DefaultCompConsumer &consumer) {
  auto scope = st.tempModScope[resolvedMod.index];
  DefaultModuleProvider provider(st.modLoader, st.typePool, scope,
                                 std::make_unique<RuntimeCancelToken>());
  auto discardPoint = provider.getCurrentDiscardPoint();

  CodeCompleter codeCompleter(consumer, willKickFrontEnd(option) ? makeObserver(provider) : nullptr,
                              st.sysConfig, st.typePool, st.logicalWorkingDir);
  DefaultForeignCompHandler foreignHandler(st, resolvedMod);
  codeCompleter.setForeignCompHandler(foreignHandler);
  codeCompleter.setCancel(provider.getCancelToken());

  bool ret = codeCompleter(scope, st.modLoader[scope->modId].first.get(), source, option);
  provider.discard(discardPoint);
  discardTempMod(st.tempModScope, resolvedMod);
  return ret;
}

int doCodeCompletion(ARState &st, const StringRef modDesc, const DoCodeCompletionOption option,
                     const StringRef source) {
  const auto resolvedMod = resolveTempModScope(st, modDesc, willKickFrontEnd(option.op));
  if (!resolvedMod) {
    errno = EINVAL;
    return -1;
  }

  DefaultCompConsumer consumer(st, option.putDesc);
  const bool ret = completeImpl(st, resolvedMod, source, option.op, consumer);
  st.setGlobal(BuiltinVarOffset::COMPREPLY, std::move(consumer).finalize()); // override COMPREPLY

  // check space insertion
  auto &obj = typeAs<CandidatesObject>(st.getGlobal(BuiltinVarOffset::COMPREPLY));
  if (!ret || ARState::isInterrupted() || st.hasError()) {
    obj.clearAndShrink(); // if canceled, force clear completion results
    if (!st.hasError()) {
      raiseSystemError(st, EINTR, "code completion is cancelled");
    }
    ARState::clearPendingSignal(SIGINT);
    errno = EINTR;
    return -1;
  }
  const size_t size = obj.size();
  assert(size <= ArrayObject::MAX_SIZE);
  return static_cast<int>(size);
}

// ##########################
// ##     SignalVector     ##
// ##########################

struct SigEntryComp {
  using Entry = std::pair<int, ObjPtr<Object>>;

  bool operator()(const Entry &x, int y) const { return x.first < y; }

  bool operator()(int x, const Entry &y) const { return x < y.first; }
};

void SignalVector::insertOrUpdate(int sigNum, ObjPtr<Object> value) {
  auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
  if (iter != this->data.end() && iter->first == sigNum) {
    if (value) {
      iter->second = std::move(value); // update
    } else {
      this->data.erase(iter); // remove
    }
  } else if (value) {
    this->data.insert(iter, std::make_pair(sigNum, std::move(value))); // insert
  }
}

ObjPtr<Object> SignalVector::lookup(int sigNum) const {
  auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
  if (iter != this->data.end() && iter->first == sigNum) {
    return iter->second;
  }
  return nullptr;
}

struct StrErrorConsumer : public ErrorConsumer {
  std::string value;

  bool colorSupported() const override { return false; }

  void consume(std::string &&message) override { this->value += message; }

  void consume(ARError &&error) override { ARError_release(&error); }
};

static ObjPtr<FuncObject> getFuncObj(const FuncObject &funcObject) {
  for (auto *ptr = funcObject.getCode().getConstPool(); *ptr; ptr++) {
    if (ptr->isObject() && ptr->get()->getKind() == ObjectKind::Func) {
      return toObjPtr<FuncObject>(*ptr);
    }
  }
  return nullptr;
}

ObjPtr<FuncObject> compileAsFunc(ARState &state, StringRef expr, const ModType &modType,
                                 bool singleExpr) {
  if (expr.size() > SYS_LIMIT_INPUT_SIZE) {
    raiseError(state, TYPE::ArgumentError, "too large input");
    return nullptr;
  }

  // prepare
  auto scope = NameScope::reopen(state.typePool, *state.rootModScope, modType);
  CompileOption option = singleExpr ? CompileOption::SINGLE_EXPR : CompileOption::IMPLICIT_BLOCK;
  DefaultModuleProvider moduleProvider(state.modLoader, state.typePool, scope,
                                       std::make_unique<RuntimeCancelToken>());
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  auto lexer = LexerPtr::create("(loaded)", ByteBuffer(expr.begin(), expr.end()), getCWD());
  auto ctx = moduleProvider.newContext(std::move(lexer));

  // compile
  CompileDumpTarget dumpTarget(state.dumpTarget.files);
  StrErrorConsumer errorConsumer;
  Compiler compiler(moduleProvider, std::move(ctx), option, &dumpTarget, errorConsumer);
  ObjPtr<FuncObject> funcObj;
  if (int ret = compiler(funcObj); ret != 0) {
    moduleProvider.discard(discardPoint);
  }

  // get result
  if (funcObj) {
    funcObj = getFuncObj(*funcObj);
    if (funcObj) {
      assert(state.typePool.get(funcObj->getTypeID()).isFuncType());
      return funcObj;
    }
  }

  if (errorConsumer.value.empty()) { // has no error, but empty code
    errorConsumer.value = "require expression";
  }
  errorConsumer.value.resize(std::min(errorConsumer.value.size(), SYS_LIMIT_ERROR_MSG_MAX));
  raiseError(state, TYPE::ArgumentError, std::move(errorConsumer.value));
  return nullptr;
}

std::string resolveFullCommandName(const ARState &state, StringRef ref, const ModType &modType,
                                   const bool udcOnly) {
  auto op = CmdResolver::Op::FROM_FQN_UDC;
  if (!udcOnly) {
    setFlag(op, CmdResolver::Op::NO_FALLBACK);
  }
  const auto cmd = CmdResolver(op, FilePathCache::SearchOp::DIRECT_SEARCH)(state, ref, &modType);
  switch (cmd.kind()) {
  case ResolvedCmd::USER_DEFINED:
  case ResolvedCmd::MODULE: {
    if (const auto r = ref.find('\0'); r != StringRef::npos) { // already fullname
      ref = ref.substr(r + 1);
    }
    auto *belongedModType = state.typePool.getModTypeById(cmd.belongModId());
    assert(belongedModType);
    std::string fullname = belongedModType->getNameRef().toString();
    fullname += '\0';
    fullname += ref;
    return fullname;
  }
  case ResolvedCmd::BUILTIN_S:
  case ResolvedCmd::BUILTIN:
  case ResolvedCmd::CMD_OBJ:
    return ref.toString();
  case ResolvedCmd::EXTERNAL:
    if (cmd.filePath() != nullptr && isExecutable(cmd.filePath())) {
      return cmd.filePath();
    }
    break;
  case ResolvedCmd::FALLBACK:
  case ResolvedCmd::INVALID:
  case ResolvedCmd::ILLEGAL_UDC:
    break;
  }
  return "";
}

static bool checkExistenceOfPathLikeLiteral(const ARState &state, StringRef literal) {
  const bool tilde = !literal.empty() && literal[0] == '~';
  std::string path = unquoteCmdArgLiteral(literal, true);
  if (tilde) {
    expandTilde(path, true, nullptr);
  }
  if (path == "." || path == "..") {
    return true;
  }
  const auto &modType = getCurRuntimeModule(state);
  const auto cmd = CmdResolver(CmdResolver::Op::NO_FALLBACK,
                               FilePathCache::SearchOp::DIRECT_SEARCH)(state, path, &modType);
  switch (cmd.kind()) {
  case ResolvedCmd::USER_DEFINED:
  case ResolvedCmd::MODULE:
  case ResolvedCmd::BUILTIN_S:
  case ResolvedCmd::BUILTIN:
  case ResolvedCmd::CMD_OBJ:
    return true;
  case ResolvedCmd::EXTERNAL:
    return cmd.filePath() != nullptr &&
           (isExecutable(cmd.filePath()) || S_ISDIR(getStMode(cmd.filePath())));
  case ResolvedCmd::FALLBACK:
  case ResolvedCmd::INVALID:
  case ResolvedCmd::ILLEGAL_UDC:
    break;
  }
  return false;
}

bool PathLikeChecker::operator()(const StringRef literal) {
  std::string key = literal.toString(); // not contains null characters
  auto iter = this->cache.find(key);
  if (iter == this->cache.end()) {
    bool r = checkExistenceOfPathLikeLiteral(this->state, literal);
    iter = this->cache.emplace(std::move(key), r).first;
  }
  return iter->second;
}

static bool merge(ARState &state, ArrayObject &arrayObj, Value *buf, const Value &compFunc,
                  size_t left, size_t mid, size_t right) {
  size_t i = left;
  size_t j = mid;
  size_t k = 0;

  while (i < mid && j < right) {
    auto &x = arrayObj[i];
    auto &y = arrayObj[j];
    auto v = VM::callFunction(state, Value(compFunc), makeArgs(x, y));
    if (state.hasError()) {
      return false;
    }
    assert(v.hasType(TYPE::Int));
    const int64_t ret = v.asInt();
    if (ret <= 0) {
      buf[k++] = x;
      i++;
    } else {
      buf[k++] = y;
      j++;
    }
  }
  if (i == mid) {
    while (j < right) {
      buf[k++] = arrayObj[j++];
    }
  } else {
    while (i < mid) {
      buf[k++] = arrayObj[i++];
    }
  }
  for (size_t l = 0; l < k; l++) {
    arrayObj[left + l] = buf[l];
  }
  return true;
}

static unsigned int getStackDepth(unsigned int size) {
  unsigned int depth = 0;
  for (; depth < 32; depth++) {
    if (size <= (1u << depth)) {
      break;
    }
  }
  return depth;
}

struct MergeSortFrame {
  unsigned int left;
  unsigned int right;
  unsigned int count;

  static MergeSortFrame create(const unsigned int left, const unsigned int right) {
    return {.left = left, .right = right, .count = 0};
  }
};

bool mergeSort(ARState &state, ArrayObject &arrayObj, const Value &compFunc) {
  auto buf = std::make_unique<Value[]>(arrayObj.size());
  assert(!arrayObj.locking());
  arrayObj.lock(ArrayObject::LockType::SORT_BY);
  auto cleanup = finally([&arrayObj] { arrayObj.unlock(); });

  InlinedStack<MergeSortFrame, 9> frames;
  static_assert(sizeof(frames) <= 128);
  frames.reserve(getStackDepth(arrayObj.size()));
  for (frames.push(MergeSortFrame::create(0, arrayObj.size())); frames.size(); frames.pop()) {
  NEXT: {
    auto &frame = frames.back();
    if (frame.left + 1 >= frame.right) {
      continue;
    }
    const unsigned int mid = (frame.left + frame.right) / 2;
    if (frame.count < 2) {
      unsigned int left;
      unsigned int right;
      if (frame.count == 0) {
        left = frame.left;
        right = mid;
      } else {
        left = mid;
        right = frame.right;
      }
      frame.count++;
      frames.push(MergeSortFrame::create(left, right));
      goto NEXT;
    }
    if (!merge(state, arrayObj, buf.get(), compFunc, frame.left, mid, frame.right)) {
      return false;
    }
  }
  }
  return true;
}

static int64_t compare(ARState &state, const Value &x, const Value &y, const Value &compFunc) {
  if (compFunc) {
    auto v = VM::callFunction(state, Value(compFunc), makeArgs(x, y));
    if (state.hasError()) {
      return -static_cast<int64_t>(ArrayObject::MAX_SIZE);
    }
    assert(v.hasType(TYPE::Int));
    return v.asInt();
  }
  return x.compare(state, y); // skip error check
}

int64_t searchSorted(ARState &state, const Value &value, ArrayObject &arrayObj,
                     const Value &compFunc) {
  arrayObj.lock(ArrayObject::LockType::SEARCH_SORTED_BY);
  auto cleanup = finally([&arrayObj] { arrayObj.unlock(); });

  int64_t first = 0;
  for (auto size = static_cast<int64_t>(arrayObj.size()); size;) {
    const int64_t halfSize = size / 2;
    const int64_t mid = first + halfSize;
    auto &midValue = arrayObj[mid];
    const int64_t ret = compare(state, midValue, value, compFunc);
    if (state.hasError()) {
      return ret;
    }
    if (ret < 0) {
      size -= halfSize + 1;
      first = mid + 1;
    } else {
      size = halfSize;
    }
  }
  if (first == static_cast<int64_t>(arrayObj.size()) ||
      compare(state, value, arrayObj[first], compFunc) != 0) {
    return -first - 1;
  }
  return first;
}

int xexecve(const char *filePath, const ArrayObject &argvObj, char *const *envp) {
  if (filePath == nullptr) {
    errno = ENOENT;
    return -1;
  }

  // set env
  setenv("_", filePath, 1);
  if (envp == nullptr) {
    envp = environ;
  }

  LOG_EXPR(DUMP_EXEC, [&] {
    std::string str = filePath;
    str += ", [";
    unsigned int count = 0;
    for (auto &e : argvObj) {
      if (count++ > 0) {
        str += ", ";
      }
      str += e.asCStr();
    }
    str += "]";
    return str;
  });

  // prepare argv
  char **argv = nullptr;
  char *stacked[10]; // for small number of arguments
  void *ptr = nullptr;
  const unsigned int allocSize = argvObj.size() + 2; // reserve sentinel and /bin/sh fallback
  if (allocSize <= std::size(stacked)) {
    argv = stacked;
  } else {
    ptr = malloc(sizeof(char *) * allocSize);
    if (!ptr) {
      return -1;
    }
    argv = static_cast<char **>(ptr);
  }
  for (unsigned int i = 0; i < allocSize - 2; i++) {
    argv[i] = const_cast<char *>(argvObj[i].asCStr());
  }
  argv[allocSize - 2] = nullptr;
  argv[allocSize - 1] = nullptr;

  // execute external command
  int ret = execve(filePath, argv, envp);
  int old = errno;
  if (errno == ENOEXEC) { // fallback to /bin/sh
    memmove(argv + 1, argv, sizeof(char *) * argvObj.size());
    argv[0] = const_cast<char *>("/bin/sh");
    ret = execve(argv[0], argv, envp);
    old = errno;
  }
  free(ptr);
  errno = old;
  return ret;
}

bool setEnv(FilePathCache &pathCache, const char *name, const char *value) {
  if (StringRef(name) == ENV_PATH) {
    pathCache.clear(); // if PATH is modified, clear file path cache like other shells
  }
  errno = 0;
  if (value) {
    return setenv(name, value, 1) == 0;
  }
  return unsetenv(name) == 0;
}

ModResult FakeModuleLoader::addNewModEntry(CStrPtr &&ptr) {
  this->path = std::move(ptr);
  return this->path.get();
}

} // namespace arsh