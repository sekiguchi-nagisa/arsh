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

static bool needEscape(int ch, CompCandidateKind kind) {
  switch (ch) {
  case ' ':
  case ';':
  case '\'':
  case '"':
  case '`':
  case '|':
  case '&':
  case '<':
  case '>':
  case '(':
  case ')':
  case '$':
  case '#':
  case '~':
    return true;
  case '{':
  case '}':
    if (kind == CompCandidateKind::COMMAND_NAME || kind == CompCandidateKind::COMMAND_NAME_PART) {
      return true;
    }
    break;
  default:
    if ((ch >= 0 && ch < 32) || ch == 127) {
      return true;
    }
    break;
  }
  return false;
}

static std::string escape(StringRef ref, CompCandidateKind kind) {
  std::string buf;
  if (!mayBeEscaped(kind)) {
    buf += ref;
    return buf;
  }

  auto iter = ref.begin();
  const auto end = ref.end();

  if (kind == CompCandidateKind::COMMAND_NAME) {
    char ch = *iter;
    if (isDecimal(ch) || ch == '+' || ch == '-' || ch == '[' || ch == ']') {
      buf += '\\';
      buf += ch;
      iter++;
    }
  }

  while (iter != end) {
    int ch = *(iter++);
    if (ch == '\\' && iter != end && needEscape(*iter, kind)) {
      buf += '\\';
      ch = *(iter++);
    } else if (needEscape(ch, kind)) {
      if ((ch >= 0 && ch < 32) || ch == 127) {
        char d[32];
        snprintf(d, std::size(d), "$'\\x%02x'", ch);
        buf += d;
        continue;
      } else {
        buf += '\\';
      }
    }
    buf += static_cast<char>(ch);
  }
  return buf;
}

void CompCandidateConsumer::operator()(StringRef ref, CompCandidateKind kind, int priority) {
  std::string estr = escape(ref, kind);
  this->consume(std::move(estr), kind, priority);
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
    if (hasFlag(option, CodeCompOp::NO_IDENT) && isIDStart(value[0])) {
      continue;
    }
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

static std::vector<std::string> computePathList(const char *pathVal) {
  std::vector<std::string> result;
  result.emplace_back();
  assert(pathVal != nullptr);

  for (unsigned int i = 0; pathVal[i] != '\0'; i++) {
    char ch = pathVal[i];
    if (ch == ':') {
      result.emplace_back();
    } else {
      result.back() += ch;
    }
  }
  return result;
}

static void completeUDC(const NameScope &scope, const std::string &cmdPrefix,
                        CompCandidateConsumer &consumer, bool ignoreIdent) {
  scope.walk([&](StringRef udc, const Handle &) {
    if (isCmdFullName(udc)) {
      udc.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      if (udc.startsWith(cmdPrefix)) {
        if (ignoreIdent && isIDStart(udc[0])) {
          return true;
        }
        consumer(udc, CompCandidateKind::COMMAND_NAME);
      }
    }
    return true;
  });
}

static void completeCmdName(const NameScope &scope, const std::string &cmdPrefix,
                            const CodeCompOp option, CompCandidateConsumer &consumer) {
  // complete user-defined command
  if (hasFlag(option, CodeCompOp::UDC)) {
    completeUDC(scope, cmdPrefix, consumer, hasFlag(option, CodeCompOp::NO_IDENT));
  }

  // complete builtin command
  if (hasFlag(option, CodeCompOp::BUILTIN)) {
    unsigned int bsize = getBuiltinCmdSize();
    auto *cmdList = getBuiltinCmdDescList();
    for (unsigned int i = 0; i < bsize; i++) {
      StringRef builtin = cmdList[i].name;
      if (hasFlag(option, CodeCompOp::NO_IDENT) && isIDStart(builtin[0])) {
        continue;
      }
      if (builtin.startsWith(cmdPrefix)) {
        consumer(builtin, CompCandidateKind::COMMAND_NAME);
      }
    }
  }

  // complete external command
  if (hasFlag(option, CodeCompOp::EXTERNAL)) {
    const char *path = getenv(ENV_PATH);
    if (path == nullptr) {
      return;
    }
    auto pathList(computePathList(path));
    for (const auto &p : pathList) {
      DIR *dir = opendir(p.c_str());
      if (dir == nullptr) {
        continue;
      }
      for (dirent *entry; (entry = readdir(dir)) != nullptr;) {
        StringRef cmd = entry->d_name;
        if (hasFlag(option, CodeCompOp::NO_IDENT) && isIDStart(cmd[0])) {
          continue;
        }
        if (cmd.startsWith(cmdPrefix)) {
          std::string fullPath(p);
          if (fullPath.back() != '/') {
            fullPath += '/';
          }
          fullPath += cmd.data();
          if (isExecutable(fullPath.c_str())) {
            consumer(cmd, CompCandidateKind::COMMAND_NAME);
          }
        }
      }
      closedir(dir);
    }
  }
}

static void completeFileName(const char *baseDir, const std::string &prefix, const CodeCompOp op,
                             CompCandidateConsumer &consumer) {
  const auto s = prefix.find_last_of('/');

  // complete tilde
  if (prefix[0] == '~' && s == std::string::npos && hasFlag(op, CodeCompOp::TILDE)) {
    setpwent();
    for (struct passwd *entry; (entry = getpwent()) != nullptr;) {
      StringRef pwname = entry->pw_name;
      if (pwname.startsWith(prefix.c_str() + 1)) {
        std::string name("~");
        name += entry->pw_name;
        name += '/';
        consumer(name, CompCandidateKind::COMMAND_TILDE);
      }
    }
    endpwent();
    return;
  }

  // complete file name

  /**
   * resolve directory path
   */
  std::string targetDir;
  if (s == 0) {
    targetDir = "/";
  } else if (s != std::string::npos) {
    targetDir = prefix.substr(0, s);
    if (hasFlag(op, CodeCompOp::TILDE)) {
      expandTilde(targetDir, true);
    }
    targetDir = expandDots(baseDir, targetDir.c_str());
  } else {
    targetDir = expandDots(baseDir, ".");
  }
  LOG(DUMP_CONSOLE, "targetDir = %s", targetDir.c_str());

  /**
   * resolve name
   */
  std::string name;
  if (s != std::string::npos) {
    name = prefix.substr(s + 1);
  } else {
    name = prefix;
  }

  DIR *dir = opendir(targetDir.c_str());
  if (dir == nullptr) {
    return;
  }

  for (dirent *entry; (entry = readdir(dir)) != nullptr;) {
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
  closedir(dir);
}

static void completeModule(const SysConfig &config, const char *scriptDir,
                           const std::string &prefix, bool tilde, CompCandidateConsumer &consumer) {
  CodeCompOp op{};
  if (tilde) {
    op = CodeCompOp::TILDE;
  }

  // complete from SCRIPT_DIR
  completeFileName(scriptDir, prefix, op, consumer);

  if (!prefix.empty() && prefix[0] == '/') {
    return;
  }

  // complete from local module dir
  completeFileName(config.getModuleHome().c_str(), prefix, op, consumer);

  // complete from system module dir
  completeFileName(config.getModuleDir().c_str(), prefix, op, consumer);
}

/**
 *
 * @param scope
 * @param prefix
 * not start with '$'
 * @param consumer
 */
static void completeVarName(const NameScope &scope, const std::string &prefix,
                            CompCandidateConsumer &consumer) {
  int offset = ({
    const NameScope *cur = &scope;
    while (!cur->isGlobal()) {
      cur = cur->parent.get();
      assert(cur);
    }
    cur->getMaxGlobalVarIndex() * 10;
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
        int priority = handle.getIndex();
        if (!handle.has(HandleAttr::GLOBAL)) {
          priority += offset;
        }
        priority *= -1;
        consumer(varName, CompCandidateKind::VAR, priority);
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

void CodeCompletionHandler::invoke(CompCandidateConsumer &consumer) {
  if (!this->hasCompRequest()) {
    return; // do nothing
  }

  if (hasFlag(this->compOp, CodeCompOp::ENV)) {
    completeEnvName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::SIGNAL)) {
    completeSigName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::EXTERNAL) || hasFlag(this->compOp, CodeCompOp::UDC) ||
      hasFlag(this->compOp, CodeCompOp::BUILTIN)) {
    completeCmdName(*this->scope, this->compWord, this->compOp, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::USER)) {
    completeUserName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::GROUP)) {
    completeGroupName(this->compWord, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::FILE) || hasFlag(this->compOp, CodeCompOp::EXEC) ||
      hasFlag(this->compOp, CodeCompOp::DIR)) {
    completeFileName(this->logicalWorkdir.c_str(), this->compWord, this->compOp, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::MODULE)) {
    completeModule(this->config, this->scriptDir.c_str(), this->compWord,
                   hasFlag(this->compOp, CodeCompOp::TILDE), consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::STMT_KW) || hasFlag(this->compOp, CodeCompOp::EXPR_KW)) {
    completeKeyword(this->compWord, this->compOp, consumer);
  }
  if (hasFlag(this->compOp, CodeCompOp::VAR)) {
    completeVarName(*this->scope, this->compWord, consumer);
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
    if (this->userDefinedComp &&
        this->userDefinedComp(*this->lex, *this->cmdNode, this->compWord, consumer)) {
      return;
    }
    if (!completeSubcommand(this->pool, *this->scope, *this->cmdNode, this->compWord, consumer)) {
      completeFileName(this->logicalWorkdir.c_str(), this->compWord, this->fallbackOp, consumer);
    }
  }
}

void CodeCompletionHandler::addVarNameRequest(std::string &&value, NameScopePtr curScope) {
  this->scope = std::move(curScope);
  this->addCompRequest(CodeCompOp::VAR, std::move(value));
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
  if (hasFlag(cmdOrKwOp, CMD_OR_KW_OP::NO_IDENT)) {
    setFlag(this->compOp, CodeCompOp::NO_IDENT);
  }
}

static LexerPtr lex(StringRef ref, const std::string &scriptDir) {
  return LexerPtr::create("<line>", ByteBuffer(ref.begin(), ref.end()),
                          CStrPtr(strdup(scriptDir.c_str())));
}

static void consumeAllInput(FrontEnd &frontEnd) {
  while (frontEnd) {
    if (!frontEnd()) {
      break;
    }
  }
}

void CodeCompleter::operator()(StringRef ref, CodeCompOp option) {
  CodeCompletionHandler handler(this->config, this->pool, this->logicalWorkingDir, this->scope,
                                this->scriptDir);
  handler.setUserDefinedComp(this->userDefinedComp);
  if (this->provider) {
    // prepare
    FrontEnd frontEnd(*this->provider, lex(ref, this->scriptDir), FrontEndOption::ERROR_RECOVERY,
                      makeObserver(handler));

    // perform completion
    consumeAllInput(frontEnd);
    handler.ignore(option);
    handler.invoke(this->consumer);
  } else {
    handler.addCompRequest(option, ref.toString());
    handler.invoke(this->consumer);
  }
}

} // namespace ydsh