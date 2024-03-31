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
#include <cstdarg>

#include "candidates.h"
#include "compiler.h"
#include "logger.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "node.h"
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

void raiseError(ARState &st, TYPE type, std::string &&message, int64_t status) {
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

void raiseShellExit(ARState &st, int64_t status) {
  if (st.has(RuntimeOption::HUP_EXIT)) {
    st.jobTable.send(SIGHUP);
  }
  int s = maskExitStatus(status);
  std::string str = "terminated by exit ";
  str += std::to_string(s);
  raiseError(st, TYPE::ShellExit_, std::move(str), s);
}

bool printErrorAt(const ARState &state, StringRef cmdName, int errNum, const char *fmt, ...) {
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
  if (!cmdName.empty()) {
    appendAsPrintable(cmdName, SYS_LIMIT_PRINTABLE_MAX, out);
    out += ": ";
  }

  va_list arg;
  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("");
  }
  va_end(arg);

  out += str;
  free(str);
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
  struct sigaction action {};
  if (sigNum != SIGINT) { // always restart system call except for SIGINT
    action.sa_flags = SA_RESTART;
  }
  sigfillset(&action.sa_mask);
  return action;
}

static ObjPtr<Object> installUnblock(ARState &st, int sigNum, ObjPtr<Object> handler) {
  auto DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL).toPtr();
  auto IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN).toPtr();

  // save old handler
  auto oldHandler = st.sigVector.lookup(sigNum);

  // set actual signal handler
  struct sigaction oldAction {};
  struct sigaction newAction = newSigaction(sigNum);
  struct sigaction *action = nullptr;
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

void installSignalHandler(ARState &st, SigSet sigSet, const ObjPtr<Object> &handler) {
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

void setSignalSetting(ARState &state) {
  SignalGuard guard;

  for (auto &e : state.sigVector.getData()) {
    int sigNum = e.first;
    assert(!isUnhandledSignal(sigNum));
    auto action = newSigaction(sigNum);
    action.sa_handler = signalHandler;
    sigaction(sigNum, &action, nullptr);
  }
  auto action = newSigaction(SIGCHLD);
  action.sa_handler = signalHandler;
  sigaction(SIGCHLD, &action, nullptr);
}

void resetSignalSettingUnblock(ARState &state) {
  for (auto &e : state.sigVector.getData()) {
    struct sigaction action {};
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

bool RuntimeCancelToken::operator()() {
  bool s = ARState::isInterrupted();
  if (s && this->clearSignal) {
    ARState::clearPendingSignal(SIGINT);
  }
  return s;
}

class DefaultCompConsumer : public CompCandidateConsumer {
private:
  ARState &state;
  CandidatesWrapper reply;
  bool overflow{false};
  const bool putDesc;

public:
  DefaultCompConsumer(ARState &state, bool putDesc)
      : state(state), reply(CandidatesWrapper(state.typePool)), putDesc(putDesc) {}

  void operator()(const CompCandidate &candidate) override {
    if (this->overflow) {
      return; // do nothing
    }
    const bool needSpace = candidate.needSuffixSpace();
    if (this->putDesc) {
      if (candidate.kind == CompCandidateKind::COMMAND_NAME) {
        const char *desc = "";
        auto kind = CandidateAttr::Kind::NONE;
        switch (candidate.getCmdNameType()) {
        case CompCandidate::CmdNameType::UDC:
          desc = "user-defined";
          kind = CandidateAttr::Kind::CMD_UDC;
          break;
        case CompCandidate::CmdNameType::BUILTIN:
          desc = "builtin";
          kind = CandidateAttr::Kind::CMD_BUILTIN;
          break;
        case CompCandidate::CmdNameType::DYNA_UDC:
          desc = "dynamic";
          kind = CandidateAttr::Kind::CMD_DYNA;
          break;
        case CompCandidate::CmdNameType::EXTERNAL:
          desc = "command";
          kind = CandidateAttr::Kind::CMD_EXTERNAL;
          break;
        }
        const CandidateAttr attr{kind, needSpace};
        this->overflow = !this->reply.addNewCandidateWith(this->state, candidate.value, desc, attr);
        return;
      }

      const std::string typeSig = candidate.formatTypeSignature(this->state.typePool);
      if (!typeSig.empty()) {
        const CandidateAttr attr{CandidateAttr::Kind::TYPE_SIGNATURE, needSpace};
        this->overflow =
            !this->reply.addNewCandidateWith(this->state, candidate.value, typeSig, attr);
        return;
      }
    }
    if (!this->reply.addAsCandidate(this->state, Value::createStr(candidate.quote()), needSpace)) {
      this->overflow = true;
    }
  }

  void addAll(const ArrayObject &o) {
    if (!this->overflow) {
      if (!this->reply.addAll(this->state, o)) {
        this->overflow = true;
      }
    }
  }

  ObjPtr<ArrayObject> finalize() && {
    this->reply.sortAndDedup(0);
    return std::move(this->reply).take();
  }
};

static Value getFullNameFromTempMod(const ARState &state, const unsigned int temModIndex,
                                    const std::string &name) {
  auto cmdName = Value::createStr(name);
  const auto modId = state.tempModScope[temModIndex]->modId;
  if (auto *modType = state.typePool.getModTypeById(modId)) {
    auto retName = resolveFullCommandName(state, cmdName, *modType, true);
    if (!retName.empty()) {
      cmdName = Value::createStr(std::move(retName));
    }
  }
  return cmdName;
}

static Value createArgv(const ARState &state, const Lexer &lex, const CmdNode &cmdNode,
                        const unsigned int temModIndex, const std::string &word, bool tilde) {
  std::vector<Value> values;

  // add cmd
  values.push_back(getFullNameFromTempMod(state, temModIndex, cmdNode.getNameNode().getValue()));

  // add args
  for (auto &e : cmdNode.getArgNodes()) {
    if (isa<RedirNode>(*e)) {
      continue;
    }
    values.push_back(Value::createStr(lex.toStrRef(e->getToken())));
  }

  // add last arg
  if (!word.empty()) {
    std::string actual;
    if (!tilde && StringRef(word).startsWith("~")) {
      actual += "\\";
    }
    actual += word;
    values.push_back(Value::createStr(std::move(actual)));
  }

  return Value::create<ArrayObject>(state.typePool.get(TYPE::StringArray), std::move(values));
}

static int kickCompHook(ARState &state, const unsigned int tempModIndex, const Lexer &lex,
                        const CmdNode &cmdNode, const std::string &word, bool tilde,
                        DefaultCompConsumer &consumer) {
  auto hook = getBuiltinGlobal(state, VAR_COMP_HOOK);
  if (hook.isInvalid()) {
    errno = EINVAL;
    return -1;
  }

  // prepare argument
  auto ctx = Value::createDummy(state.typePool.get(TYPE::Module), tempModIndex, 0);
  auto argv = createArgv(state, lex, cmdNode, tempModIndex, word, tilde);
  unsigned int index = typeAs<ArrayObject>(argv).size();
  if (!word.empty()) {
    index--;
  }

  // kick hook
  auto oldStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto oldIFS = state.getGlobal(BuiltinVarOffset::IFS);
  state.setGlobal(BuiltinVarOffset::IFS, Value::createStr(VAL_DEFAULT_IFS)); // set to default
  auto ret = VM::callFunction(state, std::move(hook),
                              makeArgs(std::move(ctx), std::move(argv), Value::createInt(index)));
  state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldStatus));
  state.setGlobal(BuiltinVarOffset::IFS, std::move(oldIFS));
  if (state.hasError()) {
    errno = EINTR;
    return -1;
  }
  if (ret.isInvalid()) {
    errno = EINVAL;
    return -1;
  }
  auto &obj = typeAs<ArrayObject>(ret);
  consumer.addAll(obj);
  return static_cast<int>(obj.size());
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
    auto pair = convertToDecimal<unsigned int>(id.begin(), id.end());
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
      modType = getCurRuntimeModule(state);
      if (!modType) {
        modType = state.typePool.getModTypeById(ROOT_MOD_ID);
        assert(modType);
      }
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

static bool completeImpl(ARState &st, ResolvedTempMod resolvedMod, StringRef source,
                         const CodeCompOp option, DefaultCompConsumer &consumer) {
  auto scope = st.tempModScope[resolvedMod.index];
  DefaultModuleProvider provider(st.modLoader, st.typePool, scope,
                                 std::make_unique<RuntimeCancelToken>());
  auto discardPoint = provider.getCurrentDiscardPoint();

  CodeCompleter codeCompleter(consumer, willKickFrontEnd(option) ? makeObserver(provider) : nullptr,
                              st.sysConfig, st.typePool, st.logicalWorkingDir);
  codeCompleter.setUserDefinedComp([&st, resolvedMod](const Lexer &lex, const CmdNode &cmdNode,
                                                      const std::string &word, bool tilde,
                                                      CompCandidateConsumer &cc) {
    return kickCompHook(st, resolvedMod.index, lex, cmdNode, word, tilde,
                        static_cast<DefaultCompConsumer &>(cc));
  });
  codeCompleter.setDynaUdcComp([&st](const std::string &word, CompCandidateConsumer &consumer) {
    auto &dynaUdcs = typeAs<OrderedMapObject>(st.getGlobal(BuiltinVarOffset::DYNA_UDCS));
    for (auto &e : dynaUdcs.getEntries()) {
      if (!e) {
        continue;
      }
      if (const auto name = e.getKey().asStrRef(); name.startsWith(word)) {
        CompCandidate candidate(name, CompCandidateKind::COMMAND_NAME);
        candidate.setCmdNameType(CompCandidate::CmdNameType::DYNA_UDC);
        consumer(candidate);
      }
    }
  });
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
  CandidatesWrapper wrapper(toObjPtr<ArrayObject>(st.getGlobal(BuiltinVarOffset::COMPREPLY)));
  if (!ret || ARState::isInterrupted() || st.hasError()) {
    wrapper.clearAndShrink(); // if cancelled, force clear completion results
    if (!st.hasError()) {
      raiseSystemError(st, EINTR, "code completion is cancelled");
    }
    ARState::clearPendingSignal(SIGINT);
    errno = EINTR;
    return -1;
  }
  const size_t size = wrapper.size();
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

/**
 * compile string as function
 * @param state
 * @param expr
 * @param modType
 * globally imported to fresh module-context
 * @return
 * compiled FuncObject.
 * if compilation failed, return null
 */
ObjPtr<FuncObject> loadExprAsFunc(ARState &state, StringRef expr, const ModType &modType) {
  if (expr.size() > SYS_LIMIT_INPUT_SIZE) {
    raiseError(state, TYPE::ArgumentError, "too large input");
    return nullptr;
  }

  // prepare
  auto scope = NameScope::reopen(state.typePool, *state.rootModScope, modType);
  CompileOption option = CompileOption::SINGLE_EXPR;
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

std::string resolveFullCommandName(const ARState &state, const Value &name, const ModType &modType,
                                   const bool udcOnly) {
  auto op = CmdResolver::FROM_FQN_UDC;
  if (!udcOnly) {
    setFlag(op, CmdResolver::NO_FALLBACK);
  }
  const auto cmd = CmdResolver(op, FilePathCache::DIRECT_SEARCH)(state, name, &modType);
  StringRef ref = name.asStrRef();
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

static bool compare(ARState &state, const Value &x, const Value &y, const Value &compFunc) {
  auto ret = VM::callFunction(state, Value(compFunc), makeArgs(x, y));
  if (state.hasError()) {
    return false;
  }
  assert(ret.hasType(TYPE::Bool));
  return ret.asBool();
}

static bool merge(ARState &state, ArrayObject &arrayObj, Value *buf, const Value &compFunc,
                  size_t left, size_t mid, size_t right) {
  size_t i = left;
  size_t j = mid;
  size_t k = 0;

  while (i < mid && j < right) {
    auto &x = arrayObj.getValues()[i];
    auto &y = arrayObj.getValues()[j];
    bool ret = !compare(state, y, x, compFunc);
    if (state.hasError()) {
      return false;
    }
    if (ret) {
      buf[k++] = x;
      i++;
    } else {
      buf[k++] = y;
      j++;
    }
  }
  if (i == mid) {
    while (j < right) {
      buf[k++] = arrayObj.getValues()[j++];
    }
  } else {
    while (i < mid) {
      buf[k++] = arrayObj.getValues()[i++];
    }
  }
  for (size_t l = 0; l < k; l++) {
    arrayObj.refValues()[left + l] = buf[l];
  }
  return true;
}

static bool mergeSortImpl(ARState &state, ArrayObject &arrayObj, Value *buf, const Value &compFunc,
                          size_t left, size_t right) {
  if (left + 1 >= right) {
    return true;
  }

  size_t mid = (left + right) / 2;
  return mergeSortImpl(state, arrayObj, buf, compFunc, left, mid) &&
         mergeSortImpl(state, arrayObj, buf, compFunc, mid, right) &&
         merge(state, arrayObj, buf, compFunc, left, mid, right);
}

bool mergeSort(ARState &state, ArrayObject &arrayObj, const Value &compFunc) {
  auto buf = std::make_unique<Value[]>(arrayObj.size());
  assert(!arrayObj.locking());
  arrayObj.lock(ArrayObject::LockType::SORT_WITH);
  bool r = mergeSortImpl(state, arrayObj, buf.get(), compFunc, 0, arrayObj.size());
  arrayObj.unlock();
  return r;
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
    for (auto &e : argvObj.getValues()) {
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
  const unsigned int allocSize = argvObj.size() + 2; // reserve sentienl and /bin/sh fallback
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
    argv[i] = const_cast<char *>(argvObj.getValues()[i].asCStr());
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

ModResult FakeModuleLoader::addNewModEntry(CStrPtr &&ptr) {
  this->path = std::move(ptr);
  return this->path.get();
}

} // namespace arsh