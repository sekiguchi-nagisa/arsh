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

#include "arg_parser_base.h"
#include "cmd_desc.h"
#include "complete.h"
#include "format_signature.h"
#include "frontend.h"
#include "logger.h"
#include "misc/edit_distance.hpp"
#include "misc/files.hpp"
#include "misc/format.hpp"
#include "paths.h"
#include "signals.h"

extern char **environ; // NOLINT

namespace arsh {

// for input completion

static bool mayBeQuoted(CompCandidateKind kind) {
  switch (kind) {
  case CompCandidateKind::COMMAND_NAME:
  case CompCandidateKind::COMMAND_NAME_PART:
  case CompCandidateKind::COMMAND_ARG:
  case CompCandidateKind::ENV_NAME:
    return true;
  default:
    return false;
  }
}

std::string CompCandidate::quote() const {
  std::string ret;
  if (const StringRef ref = this->value; mayBeQuoted(this->kind) && !ref.empty()) {
    quoteAsCmdOrShellArg(ref, ret, this->kind == CompCandidateKind::COMMAND_NAME);
  } else {
    ret += ref;
  }
  return ret;
}

std::string CompCandidate::formatTypeSignature(TypePool &pool) const {
  std::string ret;
  switch (this->kind) {
  case CompCandidateKind::VAR:
  case CompCandidateKind::VAR_IN_CMD_ARG:
    if (this->getHandle()->isFuncHandle()) { // function
      assert(this->getHandle()->isFuncHandle());
      auto &handle = cast<FuncHandle>(*this->getHandle());
      auto &type = pool.get(handle.getTypeId());
      assert(type.isFuncType());
      formatFuncSignature(cast<FunctionType>(type), handle, ret);
    } else { // variable
      auto &type = pool.get(this->getHandle()->getTypeId());
      formatVarSignature(type, ret);
    }
    break;
  case CompCandidateKind::FIELD: {
    auto &info = this->getFieldInfo();
    auto &recvType = pool.get(info.recvTypeId);
    auto &type = pool.get(info.typeId);
    formatFieldSignature(recvType, type, ret);
    break;
  }
  case CompCandidateKind::METHOD: {
    auto *hd = this->getHandle();
    assert(hd);
    assert(hd->isMethodHandle());
    formatMethodSignature(pool.get(hd->getTypeId()), *cast<MethodHandle>(hd), ret);
    break;
  }
  case CompCandidateKind::UNINIT_METHOD: {
    auto &info = this->getNativeMethodInfo();
    auto &recvType = pool.get(info.typeId);
    if (auto handle = pool.allocNativeMethodHandle(recvType, info.methodIndex)) {
      formatMethodSignature(recvType, *handle, ret);
    }
    break;
  }
  default:
    break;
  }
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

bool CompCandidate::needSuffixSpace(const StringRef value, const CompCandidateKind kind) {
  if (value.empty()) {
    return false;
  }
  switch (kind) {
  case CompCandidateKind::COMMAND_NAME:
    break;
  case CompCandidateKind::COMMAND_NAME_PART:
  case CompCandidateKind::COMMAND_ARG:
  case CompCandidateKind::COMMAND_TILDE:
  case CompCandidateKind::USER_SPECIFIED:
    if (value.back() == '/') {
      return false;
    }
    if (kind == CompCandidateKind::USER_SPECIFIED) {
      return !endsWithUnquoteSpace(value);
    }
    break;
  case CompCandidateKind::ENV_NAME:
  case CompCandidateKind::VALID_ENV_NAME:
  case CompCandidateKind::USER:
  case CompCandidateKind::GROUP:
    break;
  case CompCandidateKind::VAR:
  case CompCandidateKind::PARAM:
    return false;
  case CompCandidateKind::VAR_IN_CMD_ARG:
  case CompCandidateKind::SIGNAL:
    break;
  case CompCandidateKind::FIELD:
  case CompCandidateKind::METHOD:
  case CompCandidateKind::UNINIT_METHOD:
    return false;
  case CompCandidateKind::KEYWORD:
    break;
  case CompCandidateKind::TYPE:
    return false;
  }
  return true;
}

// ###########################
// ##     CodeCompleter     ##
// ###########################

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
  constexpr TokenKind table[] = {
#define GEN_ITEM(T) TokenKind::T,
      EACH_LA_statement(GEN_ITEM)
#undef GEN_ITEM
  };
  const bool onlyExpr = !hasFlag(option, CodeCompOp::STMT_KW);
  for (auto &e : table) {
    if (onlyExpr && !isExprKeyword(e)) {
      continue;
    }
    if (const StringRef value = toString(e); isKeyword(value) && value.startsWith(prefix)) {
      consumer(value, CompCandidateKind::KEYWORD);
    }
  }
}

static void completeEnvName(const std::string &namePrefix, CompCandidateConsumer &consumer,
                            bool validNameOnly) {
  for (unsigned int i = 0; environ[i] != nullptr; i++) {
    StringRef env(environ[i]);
    const auto r = env.indexOf("=");
    assert(r != StringRef::npos);
    if (const auto name = env.substr(0, r); name.startsWith(namePrefix)) {
      if (validNameOnly && !isValidIdentifier(name)) {
        continue;
      }
      const auto kind =
          validNameOnly ? CompCandidateKind::VALID_ENV_NAME : CompCandidateKind::ENV_NAME;
      consumer(name, kind);
    }
  }
}

static void completeSigName(const std::string &prefix, CompCandidateConsumer &consumer) {
  const SignalEntryRange ranges[] = {
      getStandardSignalEntries(),
      getRealTimeSignalEntries(),
  };
  for (auto &range : ranges) {
    for (auto &e : range) {
      if (StringRef sigName = e.getAbbrName(); sigName.startsWith(prefix)) {
        consumer(sigName, CompCandidateKind::SIGNAL);
      }
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
  scope.walk([&](StringRef udc, const Handle &handle) {
    if (isCmdFullName(udc)) {
      udc.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      if (udc.startsWith(cmdPrefix)) {
        CompCandidate candidate(udc, CompCandidateKind::COMMAND_NAME);
        candidate.setCmdNameType(handle.is(HandleKind::UDC) ? CompCandidate::CmdNameType::UDC
                                                            : CompCandidate::CmdNameType::MOD);
        consumer(candidate);
      }
    }
    return true;
  });
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

static bool completeCmdName(const NameScope &scope, const std::string &cmdPrefix,
                            const CodeCompOp option, CompCandidateConsumer &consumer,
                            ObserverPtr<CancelToken> cancel) {
  // complete user-defined command
  if (hasFlag(option, CodeCompOp::UDC)) {
    completeUDC(scope, cmdPrefix, consumer);
  }

  // complete builtin command
  if (hasFlag(option, CodeCompOp::BUILTIN)) {
    const unsigned int bsize = getBuiltinCmdSize();
    auto *cmdList = getBuiltinCmdDescList();
    for (unsigned int i = 0; i < bsize; i++) {
      if (StringRef builtin = cmdList[i].name; builtin.startsWith(cmdPrefix)) {
        CompCandidate candidate(builtin, CompCandidateKind::COMMAND_NAME);
        candidate.setCmdNameType(CompCandidate::CmdNameType::BUILTIN);
        consumer(candidate);
      }
    }
  }

  // complete external command
  if (hasFlag(option, CodeCompOp::EXTERNAL)) {
    const char *pathEnv = getenv(ENV_PATH);
    if (pathEnv == nullptr) {
      return true;
    }
    TRY(splitByDelim(pathEnv, ':', [&](const StringRef ref, bool) {
      const std::string path = ref.toString();
      auto dir = openDir(path.c_str());
      if (!dir) {
        return true;
      }
      for (dirent *entry; (entry = readdir(dir.get())) != nullptr;) {
        if (cancel && cancel()) {
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
            CompCandidate candidate(cmd, CompCandidateKind::COMMAND_NAME);
            candidate.setCmdNameType(CompCandidate::CmdNameType::EXTERNAL);
            consumer(candidate);
          }
        }
      }
      return true;
    }));
  }
  return true;
}

static bool completeFileName(const std::string &baseDir, StringRef prefix, const CodeCompOp op,
                             CompCandidateConsumer &consumer, ObserverPtr<CancelToken> cancel) {
  const auto s = prefix.lastIndexOf("/");

  // complete tilde
  if (hasFlag(op, CodeCompOp::TILDE) && prefix.startsWith("~") && s == StringRef::npos) {
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
    targetDir = expandDots(baseDir.c_str(), targetDir.c_str());
  } else {
    targetDir = expandDots(baseDir.c_str(), ".");
  }
  LOG(DUMP_CONSOLE, "targetDir = %s", targetDir.c_str());

  /**
   * resolve name
   */
  StringRef name = prefix;
  if (s != StringRef::npos) {
    name = name.substr(s + 1);
  }

  auto dir = openDir(targetDir.c_str());
  if (!dir) {
    return true;
  }
  for (dirent *entry; (entry = readdir(dir.get())) != nullptr;) {
    if (cancel && cancel()) {
      return false;
    }

    if (const StringRef relative = entry->d_name; relative.startsWith(name)) {
      if (name.empty() && (relative == ".." || relative == ".")) {
        continue;
      }

      std::string value = relative.toString();
      if (isDirectory(dir.get(), entry)) {
        value += '/';
      } else {
        if (hasFlag(op, CodeCompOp::EXEC)) {
          if (S_ISREG(getStModeAt(dirfd(dir.get()), entry->d_name)) &&
              faccessat(dirfd(dir.get()), entry->d_name, X_OK, 0) != 0) {
            continue;
          }
        } else if (hasFlag(op, CodeCompOp::DIR) && !hasFlag(op, CodeCompOp::FILE)) {
          continue;
        }
      }
      consumer(value, hasFlag(op, CodeCompOp::EXEC) ? CompCandidateKind::COMMAND_NAME_PART
                                                    : CompCandidateKind::COMMAND_ARG);
    }
  }
  return true;
}

static bool completeModule(const SysConfig &config, const std::string &scriptDir,
                           const std::string &prefix, bool tilde, CompCandidateConsumer &consumer,
                           ObserverPtr<CancelToken> cancel) {
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
  TRY(completeFileName(config.getModuleHome(), prefix, op, consumer, cancel));

  // complete from system module dir
  return completeFileName(config.getModuleDir(), prefix, op, consumer, cancel);
}

void completeVarName(const NameScope &scope, const StringRef prefix, bool inCmdArg,
                     CompCandidateConsumer &consumer) {
  const int offset = static_cast<int>(scope.getGlobalScope()->getMaxGlobalVarIndex() * 10);
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
        const auto kind = inCmdArg ? CompCandidateKind::VAR_IN_CMD_ARG : CompCandidateKind::VAR;
        CompCandidate candidate(varName, kind, priority);
        candidate.setHandle(handle);
        consumer(candidate);
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

void completeMember(const TypePool &pool, const NameScope &scope, const Type &recvType,
                    const StringRef word, CompCandidateConsumer &consumer) {
  // complete field
  auto fieldWalker = [&](StringRef name, const Handle &handle) {
    if (name.startsWith(word) && isVarName(name)) {
      if (handle.isVisibleInMod(scope.modId, name)) {
        CompCandidate candidate(name, CompCandidateKind::FIELD);
        candidate.setFieldInfo(recvType, handle);
        consumer(candidate);
      }
    }
    return true;
  };
  recvType.walkField(pool, fieldWalker);

  // complete user-defined method
  scope.walk([&](StringRef name, const Handle &handle) {
    if (!handle.isMethodHandle()) {
      return true;
    }
    auto &type = pool.get(cast<MethodHandle>(handle).getRecvTypeId());
    if (name.startsWith(word) && !isMagicMethodName(name)) {
      for (const auto *t = &recvType; t != nullptr; t = t->getSuperType()) {
        if (type == *t) {
          name = trimMethodFullNameSuffix(name);
          CompCandidate candidate(name, CompCandidateKind::METHOD);
          candidate.setHandle(handle);
          consumer(candidate);
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
          const auto init = static_cast<bool>(e.second);
          const auto kind = init ? CompCandidateKind::METHOD : CompCandidateKind::UNINIT_METHOD;
          CompCandidate candidate(name, kind);
          if (init) {
            candidate.setHandle(*e.second.handle());
          } else {
            candidate.setNativeMethodInfo(type, e.second.index());
          }
          consumer(candidate);
          break;
        }
      }
    }
  }
}

void completeType(const TypePool &pool, const NameScope &scope, const Type *recvType,
                  const StringRef word, CompCandidateConsumer &consumer) {
  if (recvType) {
    auto fieldWalker = [&](StringRef name, const Handle &handle) {
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
    if (const StringRef name = t->getNameRef();
        name.startsWith(word) && std::all_of(name.begin(), name.end(), isLetterOrDigit)) {
      consumer(name, CompCandidateKind::TYPE);
    }
  }

  // search TypeTemplate
  for (auto &e : pool.getTemplateMap()) {
    if (StringRef name = e.first; name.startsWith(word)) {
      consumer(name, CompCandidateKind::TYPE);
    }
  }

  // typeof
  if (constexpr StringRef name = "typeof"; name.startsWith(word)) {
    consumer(name, CompCandidateKind::TYPE);
  }
}

static void completeAttribute(const std::string &prefix, CompCandidateConsumer &consumer) {
  constexpr AttributeKind kinds[] = {
#define GEN_TABLE(E, S) AttributeKind::E,
      EACH_ATTRIBUTE_KIND(GEN_TABLE)
#undef GEN_TABLE
  };
  for (auto &kind : kinds) {
    if (kind == AttributeKind::NONE) {
      continue;
    }
    if (const StringRef attr = toString(kind); attr.startsWith(prefix)) {
      consumer(attr, CompCandidateKind::KEYWORD);
    }
  }
}

static void completeAttributeParam(const std::string &prefix, AttributeParamSet paramSet,
                                   CompCandidateConsumer &consumer) {
  paramSet.iterate([&](Attribute::Param param) {
    StringRef ref = toString(param);
    if (ref.startsWith(prefix)) {
      consumer(ref, CompCandidateKind::KEYWORD);
    }
  });
}

static void completeParamName(const std::vector<std::string> &paramNames, const StringRef word,
                              CompCandidateConsumer &consumer) {
  const unsigned int size = paramNames.size();
  for (unsigned int i = 0; i < size; i++) {
    if (StringRef ref = paramNames[i]; ref.startsWith(word)) {
      const int priority = 9000000 + i;
      CompCandidate candidate(paramNames[i], CompCandidateKind::PARAM, priority);
      consumer(candidate);
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

static bool completeSubcommand(const TypePool &pool, const NameScope &scope, const CmdNode &cmdNode,
                               const std::string &word, CompCandidateConsumer &consumer) {
  if (hasCmdArg(cmdNode)) {
    return false;
  }

  const std::string cmdName = toCmdFullName(cmdNode.getNameNode().getValue());
  auto handle = scope.lookup(cmdName);
  if (!handle) {
    return false;
  }

  auto &type = pool.get(handle.asOk()->getTypeId());
  if (!type.isModType()) {
    return false;
  }
  auto fieldWalker = [&](StringRef name, const Handle &) {
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

static const CLIRecordType *resolveCLIType(const TypePool &pool, const Handle &handle) {
  if (auto &type = pool.get(handle.getTypeId()); type.isFuncType()) {
    if (auto &funcType = cast<FunctionType>(type);
        funcType.getParamSize() == 1 && funcType.getParamTypeAt(0).isCLIRecordType()) {
      return cast<CLIRecordType>(&funcType.getParamTypeAt(0));
    }
  }
  return nullptr;
}

static bool completeCLIOption(const CLIRecordType &type, const std::string &word, bool firstArg,
                              CompCandidateConsumer &consumer) {
  if (word.empty() || word[0] != '-') {
    if (hasFlag(type.getAttr(), CLIRecordType::Attr::HAS_SUBCMD) && firstArg) {
      // complete sub-command
      for (auto &e : type.getEntries()) {
        if (!e.isSubCmd()) {
          continue;
        }
        if (StringRef name(e.getArgName()); name.startsWith(word)) {
          consumer(name, CompCandidateKind::COMMAND_ARG);
        }
      }
      return true;
    }
    return false;
  }

  // complete Flag, Option
  for (auto &e : type.getEntries()) {
    if (!e.isOption()) {
      continue;
    }

    // short option
    if (e.getShortName()) {
      std::string value;
      value += '-';
      value += e.getShortName();
      if (StringRef(value).startsWith(word)) {
        consumer(value, CompCandidateKind::COMMAND_ARG);
      }
    }
    // long option
    if (!e.getLongName().empty()) {
      std::string value = "--";
      value += e.getLongName();
      if (e.getParseOp() == OptParseOp::OPT_ARG) {
        value += '=';
      }
      if (StringRef(value).startsWith(word)) {
        if (value.size() == word.size() && value.back() == '=') { // 'value == word' and '--long='
          return false;
        }
        CompCandidate candidate(value, CompCandidateKind::COMMAND_ARG);
        if (value.back() == '=') {
          candidate.overrideSuffixSpace(CompCandidate::SuffixSpaceOverride::NO_SPACE);
        }
        consumer(candidate);
      } else if (StringRef ref = word; ref.endsWith("=") && e.getParseOp() == OptParseOp::HAS_ARG) {
        ref.removeSuffix(1);
        if (ref == value) {
          return false;
        }
      }
    }
  }
  return true;
}

static bool completeCLIOption(const TypePool &pool, const Lexer &lexer, const NameScope &scope,
                              const CmdNode &cmdNode, const std::string &word,
                              CompCandidateConsumer &consumer) {
  auto handle = scope.lookup(toCmdFullName(cmdNode.getNameNode().getValue()));
  if (!handle) {
    return false;
  }
  auto *cliType = resolveCLIType(pool, *handle.asOk());
  if (!cliType) {
    return false;
  }

  int latestSubCmdIndex = -1;
  const unsigned int size = cmdNode.getArgNodes().size();
  for (unsigned int i = 0; i < size; i++) {
    auto &e = cmdNode.getArgNodes()[i];
    if (isa<RedirNode>(*e)) {
      continue;
    }
    const StringRef ref = lexer.toStrRef(e->getToken());
    if (ref.empty() || ref[0] == '-') {
      break;
    }
    auto ret = cliType->findSubCmdInfo(pool, ref);
    if (ret.first) {
      cliType = ret.first;
      latestSubCmdIndex = i;
    }
  }
  const bool firstArg = size == 0 || (latestSubCmdIndex > -1 &&
                                      static_cast<unsigned int>(latestSubCmdIndex) == size - 1);
  return completeCLIOption(*cliType, word, firstArg, consumer);
}

bool CodeCompleter::invoke(const CodeCompletionContext &ctx) {
  if (!ctx.hasCompRequest()) {
    return true; // do nothing
  }

  if (ctx.has(CodeCompOp::ENV) || ctx.has(CodeCompOp::VALID_ENV)) {
    completeEnvName(ctx.getCompWord(), this->consumer, !ctx.has(CodeCompOp::ENV));
  }
  if (ctx.has(CodeCompOp::SIGNAL)) {
    completeSigName(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::EXTERNAL) || ctx.has(CodeCompOp::UDC) || ctx.has(CodeCompOp::BUILTIN)) {
    TRY(completeCmdName(ctx.getScope(), ctx.getCompWord(), ctx.getCompOp(), this->consumer,
                        this->cancel));
  }
  if (ctx.has(CodeCompOp::DYNA_UDC) && this->dynaUdcComp) {
    this->dynaUdcComp(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::USER)) {
    completeUserName(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::GROUP)) {
    completeGroupName(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::FILE) || ctx.has(CodeCompOp::EXEC) || ctx.has(CodeCompOp::DIR)) {
    const auto prefix = StringRef(ctx.getCompWord()).substr(ctx.getCompWordOffset());
    TRY(completeFileName(this->logicalWorkingDir, prefix, ctx.getCompOp(), this->consumer,
                         this->cancel));
  }
  if (ctx.has(CodeCompOp::MODULE)) {
    TRY(completeModule(this->config, ctx.getScriptDir(), ctx.getCompWord(),
                       ctx.has(CodeCompOp::TILDE), consumer, this->cancel));
  }
  if (ctx.has(CodeCompOp::STMT_KW) || ctx.has(CodeCompOp::EXPR_KW)) {
    completeKeyword(ctx.getCompWord(), ctx.getCompOp(), this->consumer);
  }
  if (ctx.has(CodeCompOp::VAR) || ctx.has(CodeCompOp::VAR_IN_CMD_ARG)) {
    const bool inCmdArg = ctx.has(CodeCompOp::VAR_IN_CMD_ARG);
    completeVarName(ctx.getScope(), ctx.getCompWord(), inCmdArg, this->consumer);
  }
  if (ctx.has(CodeCompOp::EXPECT)) {
    completeExpected(ctx.getExtraWords(), ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::MEMBER)) {
    completeMember(this->pool, ctx.getScope(), *ctx.getRecvType(), ctx.getCompWord(),
                   this->consumer);
  }
  if (ctx.has(CodeCompOp::TYPE)) {
    completeType(this->pool, ctx.getScope(), ctx.getRecvType(), ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::ATTR)) {
    completeAttribute(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::ATTR_PARAM)) {
    completeAttributeParam(ctx.getCompWord(), ctx.getTargetAttrParams(), this->consumer);
  }
  if (ctx.has(CodeCompOp::PARAM)) {
    completeParamName(ctx.getExtraWords(), ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::CMD_ARG)) {
    if (this->userDefinedComp) {
      const int s =
          this->userDefinedComp(*ctx.getLexer(), *ctx.getCmdNode(), ctx.getCompWord(),
                                hasFlag(ctx.getFallbackOp(), CodeCompOp::TILDE), this->consumer);
      if (s < 0 && errno == EINTR) {
        return false;
      }
      if (s > -1) {
        return true;
      }
    }
    if (completeCLIOption(this->pool, *ctx.getLexer(), ctx.getScope(), *ctx.getCmdNode(),
                          ctx.getCompWord(), this->consumer)) {
      return true;
    }
    if (completeSubcommand(this->pool, ctx.getScope(), *ctx.getCmdNode(), ctx.getCompWord(),
                           this->consumer)) {
      return true;
    }
    if (const auto op = ctx.getFallbackOp(); hasFlag(op, CodeCompOp::FILE)) {
      const auto prefix = StringRef(ctx.getCompWord()).substr(ctx.getCompWordOffset());
      TRY(completeFileName(this->logicalWorkingDir, prefix, op, this->consumer, this->cancel));
    }
  }
  return true;
}

static LexerPtr lex(const std::string &scriptName, StringRef ref, const std::string &scriptDir) {
  ByteBuffer buf(ref.begin(), ref.end());
  if (!buf.empty() && buf.back() == '\n') {
    buf += '\n'; // explicitly append newline for command name completion
  }
  return LexerPtr::create(scriptName.c_str(), std::move(buf), CStrPtr(strdup(scriptDir.c_str())));
}

static std::string toScriptDir(const std::string &scriptName) {
  std::string value;
  if (scriptName[0] != '/') {
    value = getCWD().get();
  } else {
    StringRef ref = scriptName;
    const auto pos = ref.lastIndexOf("/");
    ref = pos == 0 ? "/" : ref.substr(0, pos);
    value = ref.toString();
  }
  return value;
}

bool CodeCompleter::operator()(NameScopePtr scope, const std::string &scriptName, StringRef ref,
                               CodeCompOp option) {
  const auto scriptDir = toScriptDir(scriptName);
  CodeCompletionContext compCtx(std::move(scope), scriptDir);
  if (this->provider) {
    // prepare
    FrontEnd frontEnd(*this->provider, lex(scriptName, ref, scriptDir),
                      FrontEndOption::ERROR_RECOVERY, makeObserver(compCtx));

    // perform completion
    consumeAllInput(frontEnd);
    compCtx.ignore(option);
    return this->invoke(compCtx);
  }
  compCtx.addCompRequest(option, ref.toString());
  return this->invoke(compCtx);
}

class SuggestionCollector : public CompCandidateConsumer {
private:
  EditDistance editDistance;
  const StringRef src;
  StringRef target;
  unsigned int score{UINT32_MAX};
  ObserverPtr<const SuggestMemberType> targetMemberType;

public:
  explicit SuggestionCollector(StringRef name) : editDistance(3), src(name) {}

  void setTargetMemberType(const SuggestMemberType &memberType) {
    this->targetMemberType = makeObserver(memberType);
  }

  StringRef getTarget() const { return this->target; }

  unsigned int getScore() const { return this->score; }

  void operator()(const CompCandidate &candidate) override {
    if (this->targetMemberType) {
      const auto targetType = *this->targetMemberType;
      if (candidate.kind == CompCandidateKind::FIELD &&
          !hasFlag(targetType, SuggestMemberType::FIELD)) {
        return;
      }
      if ((candidate.kind == CompCandidateKind::METHOD ||
           candidate.kind == CompCandidateKind::UNINIT_METHOD) &&
          !hasFlag(targetType, SuggestMemberType::METHOD)) {
        return;
      }
    }

    const auto ref = candidate.value;
    if (this->src[0] != ref[0]) {
      return;
    }
    if (this->src[0] == '_' && this->src.size() > 1 && ref.size() > 1) {
      if (this->src[1] != ref[1]) {
        return;
      }
    }

    const auto dist = this->editDistance(this->src, ref);
    if (dist < this->score) {
      this->score = dist;
      this->target = ref;
    }
  }
};

StringRef suggestSimilarVarName(StringRef name, const NameScope &scope, unsigned int threshold) {
  if (name.empty() || name == "_") {
    return "";
  }
  SuggestionCollector collector(name);
  completeVarName(scope, "", false, collector);
  if (collector.getScore() <= threshold) {
    return collector.getTarget();
  }
  return "";
}

StringRef suggestSimilarType(StringRef name, const TypePool &pool, const NameScope &scope,
                             const Type *recvType, unsigned int threshold) {
  if (name.empty() || name == "_") {
    return "";
  }
  SuggestionCollector collector(name);
  completeType(pool, scope, recvType, "", collector);
  if (collector.getScore() <= threshold) {
    return collector.getTarget();
  }
  return "";
}

StringRef suggestSimilarMember(StringRef name, const TypePool &pool, const NameScope &scope,
                               const Type &recvType, SuggestMemberType targetType,
                               unsigned int threshold) {
  if (name.empty() || name == "_") {
    return "";
  }
  SuggestionCollector collector(name);
  collector.setTargetMemberType(targetType);
  completeMember(pool, scope, recvType, "", collector);
  if (collector.getScore() <= threshold) {
    return collector.getTarget();
  }
  return "";
}

StringRef suggestSimilarParamName(StringRef name, const std::vector<std::string> &paramNames,
                                  unsigned int threshold) {
  SuggestionCollector collector(name);
  completeParamName(paramNames, "", collector);
  if (collector.getScore() <= threshold) {
    return collector.getTarget();
  }
  return "";
}

} // namespace arsh