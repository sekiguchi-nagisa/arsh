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
  auto except =
      ErrorObject::newError(st, st.typePool.get(type), DSValue::createStr(std::move(message)));
  st.throwObject(std::move(except), status);
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

void installSignalHandler(DSState &st, int sigNum, const DSValue &handler) {
  SignalGuard guard;

  auto &DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL);
  auto &IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN);

  DSValue actualHandler;
  auto op = SignalVector::UnsafeSigOp::SET;
  switch (sigNum) {
  case SIGBUS:
  case SIGSEGV:
  case SIGILL:
  case SIGFPE:
    /**
     * not handle or ignore these signals due to prevent undefined behavior.
     * see.
     * https://wiki.sei.cmu.edu/confluence/display/c/SIG35-C.+Do+not+return+from+a+computational+exception+signal+handler
     *      http://man7.org/linux/man-pages/man2/sigaction.2.html
     */
    return;
  case SIGKILL:
  case SIGSTOP:
    /**
     * sigaction dose not accept these signals
     */
    return;
  default:
    break;
  }

  if (handler == DFL_handler) {
    op = SignalVector::UnsafeSigOp::DFL;
  } else if (handler == IGN_handler) {
    op = SignalVector::UnsafeSigOp::IGN;
  } else {
    actualHandler = handler;
  }

  st.sigVector.install(sigNum, op, actualHandler);
}

DSValue getSignalHandler(const DSState &st, int sigNum) {
  auto &DFL_handler = getBuiltinGlobal(st, VAR_SIG_DFL);
  auto &IGN_handler = getBuiltinGlobal(st, VAR_SIG_IGN);

  auto handler = st.sigVector.lookup(sigNum);

  if (handler == nullptr) {
    struct sigaction action {};
    if (sigaction(sigNum, nullptr, &action) == 0) {
      if (action.sa_handler == SIG_IGN) {
        return IGN_handler;
      }
    }
    return DFL_handler;
  }
  return handler;
}

void setJobControlSignalSetting(DSState &st, bool set) {
  SignalGuard guard;

  auto op = set ? SignalVector::UnsafeSigOp::IGN : SignalVector::UnsafeSigOp::DFL;
  DSValue handler;

  if (set) {
    st.sigVector.install(SIGINT, SignalVector::UnsafeSigOp::SET,
                         getBuiltinGlobal(st, VAR_DEF_SIGINT));
  } else {
    st.sigVector.install(SIGINT, op, handler);
  }
  st.sigVector.install(SIGQUIT, op, handler);
  st.sigVector.install(SIGTSTP, op, handler);
  st.sigVector.install(SIGTTIN, op, handler);
  st.sigVector.install(SIGTTOU, op, handler);
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
    return cast<ModType>(ret.asOk());
  }
  return nullptr;
}

class DefaultCompConsumer : public CompCandidateConsumer {
private:
  ArrayObject &reply;

public:
  explicit DefaultCompConsumer(ArrayObject &obj) : reply(obj) {}

  void consume(std::string &&value, CompCandidateKind, int) override {
    this->reply.append(DSValue::createStr(value)); // FIXME: check array size limit
  }
};

class DefaultCompCancel : public CompCancel {
public:
  bool isCanceled() const override { return DSState ::isInterrupted(); }
};

static DSValue createArgv(const TypePool &pool, const Lexer &lex, const CmdNode &cmdNode,
                          const std::string &word) {
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
    values.push_back(DSValue::createStr(word));
  }

  return DSValue::create<ArrayObject>(pool.get(TYPE::StringArray), std::move(values));
}

static bool kickCompHook(DSState &state, unsigned int tempModIndex, const Lexer &lex,
                         const CmdNode &cmdNode, const std::string &word,
                         CompCandidateConsumer &consumer) {
  auto hook = getBuiltinGlobal(state, VAR_COMP_HOOK);
  if (hook.isInvalid()) {
    return false;
  }

  // prepare argument
  auto ctx = DSValue::createDummy(state.typePool.get(TYPE::Module), tempModIndex, 0);
  auto argv = createArgv(state.typePool, lex, cmdNode, word);
  unsigned int index = typeAs<ArrayObject>(argv).size();
  if (!word.empty()) {
    index--;
  }

  // kick hook
  auto ret = VM::callFunction(state, std::move(hook),
                              makeArgs(std::move(ctx), std::move(argv), DSValue::createInt(index)));
  if (state.hasError() || typeAs<ArrayObject>(ret).size() == 0) {
    return false;
  }

  for (auto &e : typeAs<ArrayObject>(ret).getValues()) {
    consumer(e.asCStr(), CompCandidateKind::COMMAND_ARG);
  }
  return true;
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
    auto pair = convertToNum<unsigned int>(id.begin(), id.end());
    if (!pair.second || pair.first >= state.tempModScope.size()) {
      return {};
    }

    ResolvedTempMod ret = {
        .index = pair.first,
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
        auto ret = state.typePool.getModTypeById(1);
        assert(ret);
        modType = cast<ModType>(ret.asOk());
      }
    } else if (desc.startsWith(OBJ_MOD_PREFIX) && desc.endsWith(")")) {
      auto typeName = desc;
      typeName.removePrefix(strlen(OBJ_MOD_PREFIX));
      typeName.removeSuffix(1);
      if (auto ret = state.typePool.getType(typeName); ret && ret.asOk()->isModType()) {
        modType = cast<ModType>(ret.asOk());
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

Optional<unsigned int> doCodeCompletion(DSState &st, StringRef modDesc, StringRef source,
                                        const CodeCompOp option) {
  const auto resolvedMod = resolveTempModScope(st, modDesc, willKickFrontEnd(option));
  if (!resolvedMod) {
    return {};
  }

  auto result = DSValue::create<ArrayObject>(st.typePool.get(TYPE::StringArray));
  auto &compreply = typeAs<ArrayObject>(result);

  {
    auto scope = st.tempModScope[resolvedMod.index];
    DefaultModuleProvider provider(st.modLoader, st.typePool, scope);
    auto discardPoint = provider.getCurrentDiscardPoint();

    DefaultCompConsumer consumer(compreply);
    CodeCompleter codeCompleter(consumer,
                                willKickFrontEnd(option) ? makeObserver(provider) : nullptr,
                                st.sysConfig, st.typePool, st.logicalWorkingDir);
    codeCompleter.setUserDefinedComp([&st, resolvedMod](const Lexer &lex, const CmdNode &cmdNode,
                                                        const std::string &word,
                                                        CompCandidateConsumer &consumer) {
      return kickCompHook(st, resolvedMod.index, lex, cmdNode, word, consumer);
    });
    DefaultCompCancel cancel;
    codeCompleter.setCancel(cancel);

    if (!codeCompleter(scope, st.modLoader[scope->modId].first.get(), source, option)) {
      compreply.refValues().clear(); // if cancelled, clear completion results
      raiseSystemError(st, EINTR, "code completion is cancelled");
    }
    provider.discard(discardPoint);
    discardTempMod(st.tempModScope, resolvedMod);
  }

  auto &values = compreply.refValues();
  compreply.sortAsStrArray();
  auto iter = std::unique(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
    return x.asStrRef() == y.asStrRef();
  });
  values.erase(iter, values.end());
  ASSERT_ARRAY_SIZE(compreply); // FIXME: check array size limit

  // override COMPREPLY
  st.setGlobal(BuiltinVarOffset::COMPREPLY, std::move(result));
  return values.size();
}

// ##########################
// ##     SignalVector     ##
// ##########################

struct SigEntryComp {
  using Entry = std::pair<int, DSValue>;

  bool operator()(const Entry &x, int y) const { return x.first < y; }

  bool operator()(int x, const Entry &y) const { return x < y.first; }
};

void SignalVector::insertOrUpdate(int sigNum, const DSValue &func) {
  auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
  if (iter != this->data.end() && iter->first == sigNum) {
    if (func) {
      iter->second = func; // update
    } else {
      this->data.erase(iter); // remove
    }
  } else if (func) {
    this->data.insert(iter, std::make_pair(sigNum, func)); // insert
  }
}

DSValue SignalVector::lookup(int sigNum) const {
  auto iter = std::lower_bound(this->data.begin(), this->data.end(), sigNum, SigEntryComp());
  if (iter != this->data.end() && iter->first == sigNum) {
    return iter->second;
  }
  return nullptr;
}

// when called this handler, all signals are blocked due to signal mask
static void signalHandler(int sigNum) {
  DSState::pendingSigSet.add(sigNum);
  setFlag(DSState::eventDesc, VMEvent::SIGNAL);
}

void SignalVector::install(int sigNum, UnsafeSigOp op, const DSValue &handler) {
  if (sigNum == SIGCHLD) {
    op = UnsafeSigOp::SET;
  }

  // set posix signal handler
  struct sigaction action {};
  if (sigNum != SIGINT) { // always restart system call except for SIGINT
    action.sa_flags = SA_RESTART;
  }
  sigfillset(&action.sa_mask);

  switch (op) {
  case UnsafeSigOp::DFL:
    action.sa_handler = SIG_DFL;
    break;
  case UnsafeSigOp::IGN:
    action.sa_handler = SIG_IGN;
    break;
  case UnsafeSigOp::SET:
    action.sa_handler = signalHandler;
    break;
  }
  sigaction(sigNum, &action, nullptr);

  // register handler
  if (sigNum != SIGCHLD) {
    this->insertOrUpdate(sigNum, handler);
  }
}

void SignalVector::clear() {
  for (auto &e : this->data) {
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigaction(e.first, &action, nullptr);
  }
  this->data.clear();
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
  DefaultModuleProvider moduleProvider(state.modLoader, state.typePool, scope);
  auto discardPoint = moduleProvider.getCurrentDiscardPoint();
  auto lexer = LexerPtr::create("(loaded)", ByteBuffer(expr.begin(), expr.end()), getCWD());
  auto ctx = moduleProvider.newContext(std::move(lexer), toOption(option), nullptr);

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
  if (funcObj && (funcObj = getFuncObj(*funcObj))) {
    assert(state.typePool.get(funcObj->getTypeID()).isFuncType());
    return Ok(funcObj);
  } else {
    if (errorConsumer.value.empty()) { // has no error, but empty code
      errorConsumer.value = "require expression";
    }
    auto message = DSValue::createStr(std::move(errorConsumer.value));
    auto error = ErrorObject::newError(state, state.typePool.get(TYPE::InvalidOperationError),
                                       std::move(message));
    return Err(toObjPtr<ErrorObject>(error));
  }
}

std::string resolveFullCommandName(const DSState &state, StringRef name, const ModType &modType) {
  CmdResolver resolver(CmdResolver::NO_FALLBACK, FilePathCache::DIRECT_SEARCH);
  auto cmd = resolver(state, name, &modType);
  switch (cmd.kind()) {
  case ResolvedCmd::USER_DEFINED:
  case ResolvedCmd::MODULE: {
    unsigned int typeId = cmd.belongModTypeId();
    assert(typeId > 0);
    auto &type = state.typePool.get(typeId);
    assert(type.isModType());
    std::string fullname = type.getNameRef().toString();
    fullname += '\0';
    fullname += name.data();
    return fullname;
  }
  case ResolvedCmd::BUILTIN_S:
  case ResolvedCmd::BUILTIN:
    return name.toString();
  case ResolvedCmd::EXTERNAL:
    if (cmd.filePath() != nullptr && isExecutable(cmd.filePath())) {
      return cmd.filePath();
    }
    break;
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
  assert(ret.hasType(TYPE::Boolean));
  return ret.asBool();
}

static bool merge(DSState &state, ArrayObject &arrayObj, DSValueBase *buf, const DSValue &compFunc,
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
      static_cast<DSValue &>(buf[k++]) = x;
      i++;
    } else {
      static_cast<DSValue &>(buf[k++]) = y;
      j++;
    }
  }
  if (i == mid) {
    while (j < right) {
      static_cast<DSValue &>(buf[k++]) = arrayObj.getValues()[j++];
    }
  } else {
    while (i < mid) {
      static_cast<DSValue &>(buf[k++]) = arrayObj.getValues()[i++];
    }
  }
  for (size_t l = 0; l < k; l++) {
    arrayObj.refValues()[left + l] = std::move(static_cast<DSValue &>(buf[l]));
  }
  return true;
}

static bool mergeSortImpl(DSState &state, ArrayObject &arrayObj, DSValueBase *buf,
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
  const size_t bufSize = arrayObj.size();
  auto *buf = static_cast<DSValueBase *>(malloc(sizeof(DSValueBase) * bufSize));
  if (!buf) {
    std::string v = "cannot allocate temporary buffer for merge sort";
    raiseSystemError(state, ENOMEM, std::move(v));
    return false;
  }
  for (size_t i = 0; i < bufSize; i++) {
    new (&buf[i]) DSValue();
  }

  bool r = mergeSortImpl(state, arrayObj, buf, compFunc, 0, arrayObj.size());
  for (size_t i = 0; i < bufSize; i++) {
    static_cast<DSValue &>(buf[i]).~DSValue();
  }
  free(buf);
  return r;
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