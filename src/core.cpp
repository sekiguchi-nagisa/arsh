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

#include "compiler.h"
#include "logger.h"
#include "misc/files.h"
#include "misc/num_util.hpp"
#include "node.h"
#include "vm.h"

extern char **environ; // NOLINT

namespace ydsh {

// core api definition
const DSValue &getBuiltinGlobal(const DSState &st, const char *varName) {
  auto &modType = st.typePool.getBuiltinModType();
  auto handle = modType.lookup(st.typePool, varName);
  assert(handle != nullptr);
  return st.getGlobal(handle->getIndex());
}

void raiseError(DSState &st, TYPE type, std::string &&message, int64_t status) {
  auto except = ErrorObject::newError(st, st.typePool.get(type),
                                      DSValue::createStr(std::move(message)), status);
  st.throwObject(std::move(except));
}

void raiseSystemError(DSState &st, int errorNum, std::string &&message) {
  assert(errorNum != 0);
  if (errorNum == EINTR) {
    /**
     * if EINTR, already raised SIGINT. and SIGINT handler also raises SystemError.
     * due to eliminate redundant SystemError, force clear SIGINT
     */
    SignalGuard guard;
    DSState::clearPendingSignal(SIGINT);
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
static void signalHandler(int sigNum) { DSState::pendingSigSet.add(sigNum); }

static struct sigaction newSigaction(int sigNum) {
  struct sigaction action {};
  if (sigNum != SIGINT) { // always restart system call except for SIGINT
    action.sa_flags = SA_RESTART;
  }
  sigfillset(&action.sa_mask);
  return action;
}

static ObjPtr<DSObject> installUnblock(DSState &st, int sigNum, ObjPtr<DSObject> handler) {
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

ObjPtr<DSObject> installSignalHandler(DSState &st, int sigNum, ObjPtr<DSObject> handler) {
  SignalGuard guard;
  return installUnblock(st, sigNum, std::move(handler));
}

void installSignalHandler(DSState &st, SigSet sigSet, const ObjPtr<DSObject> &handler) {
  SignalGuard guard;
  while (!sigSet.empty()) {
    int sigNum = sigSet.popPendingSig();
    installUnblock(st, sigNum, handler);
  }
}

void setJobControlSignalSetting(DSState &st, bool set) {
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

void setSignalSetting(DSState &state) {
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

void resetSignalSettingUnblock(DSState &state) {
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

const ModType *getRuntimeModuleByLevel(const DSState &state, const unsigned int callLevel) {
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
  bool s = DSState::isInterrupted();
  if (s && this->clearSignal) {
    DSState::clearPendingSignal(SIGINT);
  }
  return s;
}

class DefaultCompConsumer : public CompCandidateConsumer {
private:
  DSState &state;
  DSValue reply;
  CompCandidateKind kind{CompCandidateKind::COMMAND_NAME};
  bool overflow{false};

public:
  explicit DefaultCompConsumer(DSState &state)
      : state(state),
        reply(DSValue::create<ArrayObject>(this->state.typePool.get(TYPE::StringArray))) {}

  void operator()(const CompCandidate &candidate) override {
    if (candidate.value.empty()) {
      return;
    }
    if (!this->overflow) {
      auto &obj = typeAs<ArrayObject>(this->reply);
      if (!obj.append(this->state, DSValue::createStr(candidate.quote()))) {
        this->overflow = true;
        return;
      }
      this->kind = candidate.kind;
    }
  }

  CompCandidateKind getKind() const { return this->kind; }

  DSValue finalize() && {
    if (auto &values = typeAs<ArrayObject>(this->reply).refValues(); values.size() > 1) {
      typeAs<ArrayObject>(this->reply).sortAsStrArray();
      auto iter = std::unique(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
        return x.asStrRef() == y.asStrRef();
      });
      values.erase(iter, values.end());
    }
    return std::move(this->reply);
  }
};

static DSValue createArgv(const TypePool &pool, const Lexer &lex, const CmdNode &cmdNode,
                          const std::string &word, bool tilde) {
  std::vector<DSValue> values;

  // add cmd
  values.push_back(DSValue::createStr(cmdNode.getNameNode().getValue()));

  // add args
  for (auto &e : cmdNode.getArgNodes()) {
    if (isa<RedirNode>(*e)) {
      continue;
    }
    values.push_back(DSValue::createStr(lex.toStrRef(e->getToken())));
  }

  // add last arg
  if (!word.empty()) {
    std::string actual;
    if (!tilde && StringRef(word).startsWith("~")) {
      actual += "\\";
    }
    actual += word;
    values.push_back(DSValue::createStr(std::move(actual)));
  }

  return DSValue::create<ArrayObject>(pool.get(TYPE::StringArray), std::move(values));
}

static int kickCompHook(DSState &state, unsigned int tempModIndex, const Lexer &lex,
                        const CmdNode &cmdNode, const std::string &word, bool tilde,
                        CompCandidateConsumer &consumer) {
  auto hook = getBuiltinGlobal(state, VAR_COMP_HOOK);
  if (hook.isInvalid()) {
    errno = EINVAL;
    return -1;
  }

  // prepare argument
  auto ctx = DSValue::createDummy(state.typePool.get(TYPE::Module), tempModIndex, 0);
  auto argv = createArgv(state.typePool, lex, cmdNode, word, tilde);
  unsigned int index = typeAs<ArrayObject>(argv).size();
  if (!word.empty()) {
    index--;
  }

  // kick hook
  auto oldStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto oldIFS = state.getGlobal(BuiltinVarOffset::IFS);
  state.setGlobal(BuiltinVarOffset::IFS, DSValue::createStr(VAL_DEFAULT_IFS)); // set to default
  auto ret = VM::callFunction(state, std::move(hook),
                              makeArgs(std::move(ctx), std::move(argv), DSValue::createInt(index)));
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
  unsigned int size = typeAs<ArrayObject>(ret).size();
  for (auto &e : typeAs<ArrayObject>(ret).getValues()) {
    consumer(e.asCStr(), CompCandidateKind::COMMAND_ARG_NO_QUOTE);
  }
  return static_cast<int>(size);
}

struct ResolvedTempMod {
  unsigned int index{0};
  bool needDiscard{false};
  bool valid{false};

  explicit operator bool() const { return this->valid; }
};

static ResolvedTempMod resolveTempModScope(DSState &state, StringRef desc, bool dupMod) {
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
        modType = state.typePool.getModTypeById(1);
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

static bool completeImpl(DSState &st, ResolvedTempMod resolvedMod, StringRef source,
                         const CodeCompOp option, DefaultCompConsumer &consumer) {
  auto scope = st.tempModScope[resolvedMod.index];
  DefaultModuleProvider provider(st.modLoader, st.typePool, scope,
                                 std::make_unique<RuntimeCancelToken>());
  auto discardPoint = provider.getCurrentDiscardPoint();

  CodeCompleter codeCompleter(consumer, willKickFrontEnd(option) ? makeObserver(provider) : nullptr,
                              st.sysConfig, st.typePool, st.logicalWorkingDir);
  codeCompleter.setUserDefinedComp([&st, resolvedMod](const Lexer &lex, const CmdNode &cmdNode,
                                                      const std::string &word, bool tilde,
                                                      CompCandidateConsumer &consumer) {
    return kickCompHook(st, resolvedMod.index, lex, cmdNode, word, tilde, consumer);
  });
  codeCompleter.setDynaUdcComp([&st](const std::string &word, CompCandidateConsumer &consumer) {
    auto &dynaUdcs = typeAs<OrderedMapObject>(st.getGlobal(BuiltinVarOffset::DYNA_UDCS));
    for (auto &e : dynaUdcs.getEntries()) {
      if (!e) {
        continue;
      }
      auto name = e.getKey().asStrRef();
      if (name.startsWith(word)) {
        consumer(name, CompCandidateKind::COMMAND_NAME);
      }
    }
  });
  codeCompleter.setCancel(provider.getCancelToken());

  bool ret = codeCompleter(scope, st.modLoader[scope->modId].first.get(), source, option);
  provider.discard(discardPoint);
  discardTempMod(st.tempModScope, resolvedMod);
  return ret;
}

static bool endsWithUnquoteSpace(StringRef ref) {
  if (!ref.endsWith(" ")) {
    return false;
  }
  ref.removeSuffix(1);
  unsigned int count = 0;
  while (ref.endsWith("\\")) {
    count++;
    ref.removeSuffix(1);
  }
  return count % 2 == 0;
}

static bool needSpace(const ArrayObject &obj, CompCandidateKind kind) {
  if (obj.size() != 1) {
    return false; // do nothing
  }

  StringRef first = obj.getValues()[0].asStrRef();
  switch (kind) {
  case CompCandidateKind::COMMAND_NAME:
    break;
  case CompCandidateKind::COMMAND_NAME_PART:
  case CompCandidateKind::COMMAND_ARG:
  case CompCandidateKind::COMMAND_TILDE:
  case CompCandidateKind::COMMAND_ARG_NO_QUOTE:
    if (first.back() == '/') {
      return false;
    } else if (kind == CompCandidateKind::COMMAND_ARG_NO_QUOTE) {
      return !endsWithUnquoteSpace(first);
    }
    break;
  case CompCandidateKind::ENV_NAME:
  case CompCandidateKind::VALID_ENV_NAME:
  case CompCandidateKind::USER:
  case CompCandidateKind::GROUP:
    break;
  case CompCandidateKind::VAR:
    return false;
  case CompCandidateKind::VAR_IN_CMD_ARG:
    break;
  case CompCandidateKind::SIGNAL:
    break;
  case CompCandidateKind::FIELD:
  case CompCandidateKind::METHOD:
    return false;
  case CompCandidateKind::KEYWORD:
    break;
  case CompCandidateKind::TYPE:
    return false;
  }
  return true;
}

int doCodeCompletion(DSState &st, StringRef modDesc, StringRef source, bool insertSpace,
                     const CodeCompOp option) {
  const auto resolvedMod = resolveTempModScope(st, modDesc, willKickFrontEnd(option));
  if (!resolvedMod) {
    errno = EINVAL;
    return -1;
  }

  DefaultCompConsumer consumer(st);
  const bool ret = completeImpl(st, resolvedMod, source, option, consumer);
  const auto candidateKind = consumer.getKind();
  st.setGlobal(BuiltinVarOffset::COMPREPLY, std::move(consumer).finalize()); // override COMPREPLY

  // check space insertion
  auto &reply = typeAs<ArrayObject>(st.getGlobal(BuiltinVarOffset::COMPREPLY));
  if (!ret || DSState::isInterrupted() || st.hasError()) {
    reply.refValues().clear(); // if cancelled, clear completion results
    reply.refValues().shrink_to_fit();
    if (!st.hasError()) {
      raiseSystemError(st, EINTR, "code completion is cancelled");
    }
    DSState::clearPendingSignal(SIGINT);
    errno = EINTR;
    return -1;
  } else {
    size_t size = reply.size();
    assert(size <= ArrayObject::MAX_SIZE);
    if (insertSpace && needSpace(reply, candidateKind)) {
      assert(size == 1);
      auto ref = reply.getValues()[0].asStrRef();
      auto v = ref.toString();
      v += " ";
      reply.refValues()[0] = DSValue::createStr(std::move(v));
    }
    return static_cast<int>(size);
  }
}

// ##########################
// ##     SignalVector     ##
// ##########################

struct SigEntryComp {
  using Entry = std::pair<int, ObjPtr<DSObject>>;

  bool operator()(const Entry &x, int y) const { return x.first < y; }

  bool operator()(int x, const Entry &y) const { return x < y.first; }
};

void SignalVector::insertOrUpdate(int sigNum, ObjPtr<DSObject> value) {
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

ObjPtr<DSObject> SignalVector::lookup(int sigNum) const {
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

  void consume(DSError &&error) override { DSError_release(&error); }
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
 * if compilation failed, return ErrorObject
 */
Result<ObjPtr<FuncObject>, ObjPtr<ErrorObject>> loadExprAsFunc(DSState &state, StringRef expr,
                                                               const ModType &modType) {
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
  int ret = compiler(funcObj);
  if (ret != 0) {
    moduleProvider.discard(discardPoint);
  }

  // get result
  if (funcObj) {
    funcObj = getFuncObj(*funcObj);
    if (funcObj) {
      assert(state.typePool.get(funcObj->getTypeID()).isFuncType());
      return Ok(funcObj);
    }
  }

  if (errorConsumer.value.empty()) { // has no error, but empty code
    errorConsumer.value = "require expression";
  }
  auto message = DSValue::createStr(std::move(errorConsumer.value));
  auto error = ErrorObject::newError(state, state.typePool.get(TYPE::InvalidOperationError),
                                     std::move(message), 1);
  return Err(toObjPtr<ErrorObject>(error));
}

std::string resolveFullCommandName(const DSState &state, const DSValue &name,
                                   const ModType &modType) {
  CmdResolver resolver(CmdResolver::NO_FALLBACK, FilePathCache::DIRECT_SEARCH);
  auto cmd = resolver(state, name, &modType);
  const auto ref = name.asStrRef();
  switch (cmd.kind()) {
  case ResolvedCmd::USER_DEFINED:
  case ResolvedCmd::MODULE: {
    auto ret = state.typePool.getModTypeById(cmd.belongModId());
    assert(ret);
    std::string fullname = ret->getNameRef().toString();
    fullname += '\0';
    fullname += ref.data();
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

static bool compare(DSState &state, const DSValue &x, const DSValue &y, const DSValue &compFunc) {
  auto ret = VM::callFunction(state, DSValue(compFunc), makeArgs(x, y));
  if (state.hasError()) {
    return false;
  }
  assert(ret.hasType(TYPE::Bool));
  return ret.asBool();
}

static bool merge(DSState &state, ArrayObject &arrayObj, DSValue *buf, const DSValue &compFunc,
                  size_t left, size_t mid, size_t right) {
  size_t i = left;
  size_t j = mid;
  size_t k = 0;

  while (i < mid && j < right) {
    const size_t oldSize = arrayObj.size();
    auto &x = arrayObj.getValues()[i];
    auto &y = arrayObj.getValues()[j];
    bool ret = !compare(state, y, x, compFunc);
    if (state.hasError()) {
      return false;
    }
    if (oldSize != arrayObj.size()) {
      raiseError(state, TYPE::InvalidOperationError,
                 "array size has been changed during sortWith method");
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

static bool mergeSortImpl(DSState &state, ArrayObject &arrayObj, DSValue *buf,
                          const DSValue &compFunc, size_t left, size_t right) {
  if (left + 1 >= right) {
    return true;
  }

  size_t mid = (left + right) / 2;
  return mergeSortImpl(state, arrayObj, buf, compFunc, left, mid) &&
         mergeSortImpl(state, arrayObj, buf, compFunc, mid, right) &&
         merge(state, arrayObj, buf, compFunc, left, mid, right);
}

bool mergeSort(DSState &state, ArrayObject &arrayObj, const DSValue &compFunc) {
  auto buf = std::make_unique<DSValue[]>(arrayObj.size());
  return mergeSortImpl(state, arrayObj, buf.get(), compFunc, 0, arrayObj.size());
}

int xexecve(const char *filePath, char *const *argv, char *const *envp) {
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
    for (unsigned int i = 0; argv[i] != nullptr; i++) {
      if (i > 0) {
        str += ", ";
      }
      str += argv[i];
    }
    str += "]";
    return str;
  });

  // execute external command
  int ret = execve(filePath, argv, envp);
  if (errno == ENOEXEC) { // fallback to /bin/sh
    unsigned int size = 0;
    for (; argv[size]; size++)
      ;
    size++;
    char *newArgv[size + 1];
    newArgv[0] = const_cast<char *>("/bin/sh");
    memcpy(newArgv + 1, argv, sizeof(char *) * size);
    return execve(newArgv[0], newArgv, envp);
  }
  return ret;
}

ModResult FakeModuleLoader::addNewModEntry(CStrPtr &&ptr) {
  this->path = std::move(ptr);
  return this->path.get();
}

} // namespace ydsh