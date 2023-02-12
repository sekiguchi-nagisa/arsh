/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#include <dirent.h>
#include <grp.h>
#include <pwd.h>

#include "cmd_desc.h"
#include "complete.h"
#include "frontend.h"
#include "logger.h"
#include "misc/files.h"
#include "misc/num_util.hpp"
#include "paths.h"
#include "signals.h"

extern char **environ; // NOLINT

namespace ydsh {

// for input completion

void CompCandidateConsumer::operator()(StringRef ref, CompCandidateKind kind, int priority) {
  assert(!ref.empty());
  std::string value;
  if (mayBeEscaped(kind)) {
    if (kind == CompCandidateKind::COMMAND_NAME) {
      char ch = ref[0];
      if (isDecimal(ch) || ch == '+' || ch == '-') {
        value += '\\';
        value += ch;
        ref.removePrefix(1);
      }
    }
    quoteAsShellArg(ref, value);
  } else {
    value += ref;
  }
  this->consume(std::move(value), kind, priority);
}

// ###################################
// ##     CodeCompletionHandler     ##
// ###################################

static bool isExprKeyword(TokenKind kind) {
  switch (kind) {
#define GEN_CASE(T) case TokenKind::T:
    EACH_LA_expression(GEN_CASE) return true;
#undef GEN_CASE
  default:
    return false;
  }
}

static void completeKeyword(const std::string &prefix, CodeCompOp option,
                            CompCandidateConsumer &consumer) {
  TokenKind table[] = {
#define GEN_ITEM(T) TokenKind::T,
      EACH_LA_statement(GEN_ITEM)
#undef GEN_ITEM
  };
  bool onlyExpr = !hasFlag(option, CodeCompOp::STMT_KW);
  for (auto &e : table) {
    if (onlyExpr && !isExprKeyword(e)) {
      continue;
    }
    StringRef value = toString(e);
    if (isKeyword(value) && value.startsWith(prefix)) {
      consumer(value, CompCandidateKind::KEYWORD);
    }
  }
}

static void completeEnvName(const std::string &namePrefix, CompCandidateConsumer &consumer) {
  for (unsigned int i = 0; environ[i] != nullptr; i++) {
    StringRef env(environ[i]);
    auto r = env.indexOf("=");
    assert(r != StringRef::npos);
    auto name = env.substr(0, r);
    if (name.startsWith(namePrefix)) {
      consumer(name, CompCandidateKind::ENV);
    }
  }
}

static void completeSigName(const std::string &prefix, CompCandidateConsumer &consumer) {
  auto *list = getSignalList();
  for (unsigned int i = 0; list[i].name != nullptr; i++) {
    StringRef sigName = list[i].name;
    if (sigName.startsWith(prefix)) {
      consumer(sigName, CompCandidateKind::SIGNAL);
    }
  }
}

static void completeUserName(const std::string &prefix, CompCandidateConsumer &consumer) {
  setpwent();
  for (struct passwd *pw; (pw = getpwent()) != nullptr;) {
    StringRef pname = pw->pw_name;
    if (pname.startsWith(prefix)) {
      consumer(pname, CompCandidateKind::USER);
    }
  }
  endpwent();
}

static void completeGroupName(const std::string &prefix, CompCandidateConsumer &consumer) {
  setgrent();
  for (struct group *gp; (gp = getgrent()) != nullptr;) {
    StringRef gname = gp->gr_name;
    if (gname.startsWith(prefix)) {
      consumer(gname, CompCandidateKind::GROUP);
    }
  }
  endgrent();
}

static void completeUDC(const NameScope &scope, const std::string &cmdPrefix,
                        CompCandidateConsumer &consumer) {
  scope.walk([&](StringRef udc, const Handle &) {
    if (isCmdFullName(udc)) {
      udc.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      if (udc.startsWith(cmdPrefix)) {
        consumer(udc, CompCandidateKind::COMMAND_NAME);
      }
    }
    return true;
  });
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (!E) {                                                                                      \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

template <typename T>
static constexpr bool iterate_requirement_v =
    std::is_same_v<bool, std::invoke_result_t<T, const std::string &>>;

template <typename Func, enable_when<iterate_requirement_v<Func>> = nullptr>
static bool iteratePathList(const char *value, Func func) {
  StringRef ref = value;
  std::string path;
  for (StringRef::size_type pos = 0;;) {
    auto ret = ref.find(':', pos);
    path = "";
    path += ref.slice(pos, ret);
    TRY(func(path));
    if (ret == StringRef::npos) {
      break;
    }
    pos = ret + 1;
  }
  return true;
}

static bool completeCmdName(const NameScope &scope, const std::string &cmdPrefix,
                            const CodeCompOp option, CompCandidateConsumer &consumer,
                            ObserverPtr<CompCancel> cancel) {
  // complete user-defined command
  if (hasFlag(option, CodeCompOp::UDC)) {
    completeUDC(scope, cmdPrefix, consumer);
  }

  // complete builtin command
  if (hasFlag(option, CodeCompOp::BUILTIN)) {
    unsigned int bsize = getBuiltinCmdSize();
    auto *cmdList = getBuiltinCmdDescList();
    for (unsigned int i = 0; i < bsize; i++) {
      StringRef builtin = cmdList[i].name;
      if (builtin.startsWith(cmdPrefix)) {
        consumer(builtin, CompCandidateKind::COMMAND_NAME);
      }
    }
  }

  // complete external command
  if (hasFlag(option, CodeCompOp::EXTERNAL)) {
    const char *pathEnv = getenv(ENV_PATH);
    if (pathEnv == nullptr) {
      return true;
    }
    TRY(iteratePathList(pathEnv, [&](const std::string &path) {
      DIR *dir = opendir(path.c_str());
      if (dir == nullptr) {
        return true;
      }
      auto cleanup = finally([dir] { closedir(dir); });
      for (dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if (cancel && cancel->isCanceled()) {
          return false;
        }

        StringRef cmd = entry->d_name;
        if (cmd.startsWith(cmdPrefix)) {
          std::string fullPath = path;
          if (fullPath.back() != '/') {
            fullPath += '/';
          }
          fullPath += cmd.data();
          if (isExecutable(fullPath.c_str())) {
            consumer(cmd, CompCandidateKind::COMMAND_NAME);
          }
        }
      }
      return true;
    }));
  }
  return true;
}

static bool completeFileName(const char *baseDir, StringRef prefix, const CodeCompOp op,
                             CompCandidateConsumer &consumer, ObserverPtr<CompCancel> cancel) {
  const auto s = prefix.lastIndexOf("/");

  // complete tilde
  if (hasFlag(op, CodeCompOp::TILDE) && prefix[0] == '~' && s == StringRef::npos) {
    setpwent();
    for (struct passwd *entry; (entry = getpwent()) != nullptr;) {
      StringRef pwname = entry->pw_name;
      auto tmp = prefix;
      tmp.removePrefix(1); // skip '~'
      if (pwname.startsWith(tmp)) {
        std::string name("~");
        name += entry->pw_name;
        name += '/';
        consumer(name, CompCandidateKind::COMMAND_TILDE);
      }
    }
    endpwent();
    return true;
  }

  // complete file name

  /**
   * resolve directory path
   */
  std::string targetDir;
  if (s == 0) {
    targetDir = "/";
  } else if (s != StringRef::npos) {
    targetDir = prefix.substr(0, s).toString();
    if (hasFlag(op, CodeCompOp::TILDE)) {
      expandTilde(targetDir, true, nullptr);
    }
    targetDir = expandDots(baseDir, targetDir.c_str());
  } else {
    targetDir = expandDots(baseDir, ".");
  }
  LOG(DUMP_CONSOLE, "targetDir = %s", targetDir.c_str());

  /**
   * resolve name
   */
  StringRef name = prefix;
  if (s != StringRef::npos) {
    name = name.substr(s + 1);
  }

  DIR *dir = opendir(targetDir.c_str());
  if (dir == nullptr) {
    return true;
  }

  auto cleanup = finally([dir] { closedir(dir); });
  for (dirent *entry; (entry = readdir(dir)) != nullptr;) {
    if (cancel && cancel->isCanceled()) {
      return false;
    }

    StringRef dname = entry->d_name;
    if (dname.startsWith(name)) {
      if (name.empty() && (dname == ".." || dname == ".")) {
        continue;
      }

      std::string fullPath(targetDir);
      if (fullPath.back() != '/') {
        fullPath += '/';
      }
      fullPath += entry->d_name;

      if (isDirectory(dir, entry)) {
        fullPath += '/';
      } else {
        if (hasFlag(op, CodeCompOp::EXEC)) {
          if (S_ISREG(getStMode(fullPath.c_str())) && access(fullPath.c_str(), X_OK) != 0) {
            continue;
          }
        } else if (hasFlag(op, CodeCompOp::DIR) && !hasFlag(op, CodeCompOp::FILE)) {
          continue;
        }
      }

      StringRef fileName = fullPath;
      unsigned int len = strlen(entry->d_name);
      if (fileName.back() == '/') {
        len++;
      }
      fileName.removePrefix(fileName.size() - len);
      consumer(fileName, hasFlag(op, CodeCompOp::EXEC) ? CompCandidateKind::COMMAND_NAME_PART
                                                       : CompCandidateKind::COMMAND_ARG);
    }
  }
  return true;
}

static bool completeModule(const SysConfig &config, const char *scriptDir,
                           const std::string &prefix, bool tilde, CompCandidateConsumer &consumer,
                           ObserverPtr<CompCancel> cancel) {
  CodeCompOp op{};
  if (tilde) {
    op = CodeCompOp::TILDE;
  }

  // complete from SCRIPT_DIR
  TRY(completeFileName(scriptDir, prefix, op, consumer, cancel));

  if (!prefix.empty() && prefix[0] == '/') {
    return true;
  }

  // complete from local module dir
  TRY(completeFileName(config.getModuleHome().c_str(), prefix, op, consumer, cancel));

  // complete from system module dir
  return completeFileName(config.getModuleDir().c_str(), prefix, op, consumer, cancel);
}

/**
 *
 * @param scope
 * @param prefix
 * not start with '$'
 * @param consumer
 */
static void completeVarName(const NameScope &scope, const std::string &prefix, bool inCmdArg,
                            CompCandidateConsumer &consumer) {
  int offset = ({
    const NameScope *cur = &scope;
    while (!cur->isGlobal()) {
      cur = cur->parent.get();
      assert(cur);
    }
    static_cast<int>(cur->getMaxGlobalVarIndex() * 10);
  });

  unsigned int funcScopeDepth = 0;
  for (const auto *cur = &scope; cur != nullptr; cur = cur->parent.get()) {
    for (auto &e : cur->getHandles()) {
      StringRef varName = e.first;
      auto &handle = *e.second.first;
      if (handle.has(HandleAttr::UNCAPTURED) && funcScopeDepth) {
        continue;
      }

      if (varName.startsWith(prefix) && isVarName(varName)) {
        int priority = static_cast<int>(handle.getIndex());
        if (!handle.has(HandleAttr::GLOBAL)) {
          priority += offset;
        }
        priority *= -1;
        consumer(varName, inCmdArg ? CompCandidateKind::VAR_IN_CMD_ARG : CompCandidateKind::VAR,
                 priority);
      }
    }
    if (cur->isFunc()) {
      funcScopeDepth++;
    }
  }
}

static void completeExpected(const std::vector<std::string> &expected, const std::string &prefix,
                             CompCandidateConsumer &consumer) {
  for (auto &e : expected) {
    if (isKeyword(e)) {
      if (StringRef(e).startsWith(prefix)) {
        consumer(e, CompCandidateKind::KEYWORD);
      }
    }
  }
}

static void completeMember(const TypePool &pool, const NameScope &scope, const DSType &recvType,
                           const std::string &word, CompCandidateConsumer &consumer) {
  // complete field
  std::function<bool(StringRef, const Handle &)> fieldWalker = [&](StringRef name,
                                                                   const Handle &handle) {
    if (name.startsWith(word) && isVarName(name)) {
      if (handle.isVisibleInMod(scope.modId, name)) {
        consumer(name, CompCandidateKind::FIELD);
      }
    }
    return true;
  };
  recvType.walkField(pool, fieldWalker);

  // complete user-defined method
  scope.walk([&](StringRef name, const Handle &handle) {
    if (!handle.isMethod()) {
      return true;
    }
    auto &type = pool.get(cast<MethodHandle>(handle).getRecvTypeId());
    if (name.startsWith(word) && !isMagicMethodName(name)) {
      for (const auto *t = &recvType; t != nullptr; t = t->getSuperType()) {
        if (type == *t) {
          name = trimMethodFullNameSuffix(name);
          consumer(name, CompCandidateKind::METHOD);
          break;
        }
      }
    }
    return true;
  });

  // complete builtin method
  for (auto &e : pool.getMethodMap()) {
    StringRef name = e.first.ref;
    assert(!name.empty());
    auto &type = pool.get(e.first.id);
    if (name.startsWith(word) && !isMagicMethodName(name)) {
      for (const auto *t = &recvType; t != nullptr; t = t->getSuperType()) {
        if (type == *t) {
          consumer(name, CompCandidateKind::METHOD);
          break;
        }
      }
    }
  }
}

static void completeType(const TypePool &pool, const DSType *recvType, const NameScope &scope,
                         const std::string &word, CompCandidateConsumer &consumer) {
  if (recvType) {
    std::function<bool(StringRef, const Handle &)> fieldWalker = [&](StringRef name,
                                                                     const Handle &handle) {
      if (name.startsWith(word) && isTypeAliasFullName(name)) {
        if (handle.isVisibleInMod(scope.modId, name)) {
          name.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
          consumer(name, CompCandidateKind::TYPE);
        }
      }
      return true;
    };
    recvType->walkField(pool, fieldWalker);
    return;
  }

  // search scope
  scope.walk([&](StringRef name, const Handle &) {
    if (name.startsWith(word) && isTypeAliasFullName(name)) {
      name.removeSuffix(strlen(TYPE_ALIAS_SYMBOL_SUFFIX));
      consumer(name, CompCandidateKind::TYPE);
    }
    return true;
  });

  // search TypePool
  for (auto &t : pool.getTypeTable()) {
    if (t->isModType() || t->isArrayType() || t->isMapType() || t->isOptionType() ||
        t->isFuncType()) {
      continue;
    }
    StringRef name = t->getNameRef();
    if (name.startsWith(word) && std::all_of(name.begin(), name.end(), isalnum)) {
      consumer(name, CompCandidateKind::TYPE);
    }
  }

  // search TypeTemplate
  for (auto &e : pool.getTemplateMap()) {
    StringRef name = e.first;
    if (name.startsWith(word)) {
      consumer(name, CompCandidateKind::TYPE);
    }
  }

  // typeof
  if (StringRef name = "typeof"; name.startsWith(word)) {
    consumer(name, CompCandidateKind::TYPE);
  }
}

static bool hasCmdArg(const CmdNode &node) {
  for (auto &e : node.getArgNodes()) {
    if (e->is(NodeKind::CmdArg)) {
      return true;
    }
  }
  return false;
}

static bool completeSubcommand(const TypePool &pool, NameScope &scope, const CmdNode &cmdNode,
                               const std::string &word, CompCandidateConsumer &consumer) {
  if (hasCmdArg(cmdNode)) {
    return false;
  }

  std::string cmdName = toCmdFullName(cmdNode.getNameNode().getValue());
  auto handle = scope.lookup(cmdName);
  if (!handle) {
    return false;
  }

  auto &type = pool.get(handle.asOk()->getTypeId());
  if (!type.isModType()) {
    return false;
  }
  std::function<bool(StringRef, const Handle &)> fieldWalker = [&](StringRef name, const Handle &) {
    if (name.startsWith(word) && isCmdFullName(name)) {
      if (!name.startsWith("_")) {
        name.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
        consumer(name, CompCandidateKind::COMMAND_ARG);
      }
    }
    return true;
  };
  type.walkField(pool, fieldWalker);
  return true;
}

bool CodeCompletionHandler::invoke(CompCandidateConsumer &consumer) {
  if (!this->hasCompRequest()) {
    return true; // do nothing
  }

  if (hasFlag(this->compOp, CodeCompOp::ENV)) {
    completeEnvName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::SIGNAL)) {
    completeSigName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::EXTERNAL) || hasFlag(this->compOp, CodeCompOp::UDC) ||
      hasFlag(this->compOp, CodeCompOp::BUILTIN)) {
    TRY(completeCmdName(*this->scope, this->compWord, this->compOp, consumer, this->cancel));
  }
  if (hasFlag(this->compOp, CodeCompOp::DYNA_UDC) && this->dynaUdcComp) {
    this->dynaUdcComp(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::USER)) {
    completeUserName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::GROUP)) {
    completeGroupName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::FILE) || hasFlag(this->compOp, CodeCompOp::EXEC) ||
      hasFlag(this->compOp, CodeCompOp::DIR)) {
    auto prefix = StringRef(this->compWord).substr(this->compWordOffset);
    TRY(completeFileName(this->logicalWorkdir.c_str(), prefix, this->compOp, consumer,
                         this->cancel));
  }
  if (hasFlag(this->compOp, CodeCompOp::MODULE)) {
    TRY(completeModule(this->config, this->scriptDir.c_str(), this->compWord,
                       hasFlag(this->compOp, CodeCompOp::TILDE), consumer, this->cancel));
  }
  if (hasFlag(this->compOp, CodeCompOp::STMT_KW) || hasFlag(this->compOp, CodeCompOp::EXPR_KW)) {
    completeKeyword(this->compWord, this->compOp, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::VAR)) {
    bool inCmdArg = hasFlag(this->compOp, CodeCompOp::CMD_ARG);
    completeVarName(*this->scope, this->compWord, inCmdArg, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::EXPECT)) {
    completeExpected(this->extraWords, this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::MEMBER)) {
    completeMember(this->pool, *this->scope, *this->recvType, this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::TYPE)) {
    completeType(this->pool, this->recvType, *this->scope, this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::HOOK)) {
    if (this->userDefinedComp) {
      int s = this->userDefinedComp(*this->lex, *this->cmdNode, this->compWord, consumer);
      if (s < 0 && errno == EINTR) {
        return false;
      } else if (s > -1) {
        return true;
      }
    }
    if (!completeSubcommand(this->pool, *this->scope, *this->cmdNode, this->compWord, consumer)) {
      auto prefix = StringRef(this->compWord).substr(this->compWordOffset);
      TRY(completeFileName(this->logicalWorkdir.c_str(), prefix, this->fallbackOp, consumer,
                           this->cancel));
    }
  }
  return true;
}

void CodeCompletionHandler::addTypeNameRequest(std::string &&value, const DSType *type,
                                               NameScopePtr curScope) {
  this->scope = std::move(curScope);
  this->recvType = type;
  this->addCompRequest(CodeCompOp::TYPE, std::move(value));
}

void CodeCompletionHandler::addCmdOrKeywordRequest(std::string &&value, CMD_OR_KW_OP cmdOrKwOp) {
  // add command request
  bool tilde = hasFlag(cmdOrKwOp, CMD_OR_KW_OP::TILDE);
  bool isDir = strchr(value.c_str(), '/') != nullptr;
  if (tilde || isDir) {
    CodeCompOp op = CodeCompOp::EXEC;
    if (tilde) {
      setFlag(op, CodeCompOp::TILDE);
    }
    this->addCompRequest(op, std::move(value));
  } else {
    this->addCompRequest(CodeCompOp::COMMAND, std::move(value));
  }

  // add keyword request
  setFlag(this->compOp,
          hasFlag(cmdOrKwOp, CMD_OR_KW_OP::STMT) ? CodeCompOp::STMT_KW : CodeCompOp::EXPR_KW);
}

static LexerPtr lex(const std::string &scriptName, StringRef ref, const std::string &scriptDir) {
  return LexerPtr::create(scriptName.c_str(), ByteBuffer(ref.begin(), ref.end()),
                          CStrPtr(strdup(scriptDir.c_str())));
}

static void consumeAllInput(FrontEnd &frontEnd) {
  while (frontEnd) {
    if (!frontEnd()) {
      break;
    }
  }
}

static std::string toScriptDir(const std::string &scriptName) {
  std::string value;
  if (scriptName[0] != '/') {
    auto cwd = getCWD();
    value = cwd.get();
  } else {
    StringRef ref = scriptName;
    auto pos = ref.lastIndexOf("/");
    ref = pos == 0 ? "/" : ref.substr(0, pos);
    value = ref.toString();
  }
  return value;
}

bool CodeCompleter::operator()(NameScopePtr scope, const std::string &scriptName, StringRef ref,
                               CodeCompOp option) {
  auto scriptDir = toScriptDir(scriptName);
  CodeCompletionHandler handler(this->config, this->pool, this->logicalWorkingDir, std::move(scope),
                                scriptDir);
  handler.setUserDefinedComp(this->userDefinedComp);
  handler.setDynaUdcComp(this->dynaUdcComp);
  handler.setCancel(this->cancel);
  if (this->provider) {
    // prepare
    FrontEnd frontEnd(*this->provider, lex(scriptName, ref, scriptDir),
                      FrontEndOption::ERROR_RECOVERY, makeObserver(handler));

    // perform completion
    consumeAllInput(frontEnd);
    handler.ignore(option);
    return handler.invoke(this->consumer);
  } else {
    handler.addCompRequest(option, ref.toString());
    return handler.invoke(this->consumer);
  }
}

} // namespace ydsh