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
#include "format_util.h"
#include "frontend.h"
#include "misc/edit_distance.hpp"
#include "misc/files.hpp"
#include "misc/format.hpp"
#include "paths.h"
#include "signals.h"

extern char **environ; // NOLINT

namespace arsh {

void CodeCompletionContext::addCmdOrKeywordRequest(const Lexer &lexer, Token wordToken,
                                                   bool inStmt) {
  // add the command request
  std::string value = lexer.toCmdArg(wordToken);
  const bool isDir = strchr(value.c_str(), '/') != nullptr;
  const bool mayBeKeyword = wordToken.size == value.size();
  if (const bool tilde = lexer.startsWith(wordToken, '~'); tilde || isDir) {
    auto op = CodeCompOp::EXEC;
    if (tilde) {
      setFlag(op, CodeCompOp::TILDE);
    }
    this->addCompRequest(op, std::move(value));
  } else {
    this->addCompRequest(CodeCompOp::COMMAND, std::move(value));
  }

  // add the keyword request
  if (mayBeKeyword) {
    setFlag(this->compOp, inStmt ? CodeCompOp::STMT_KW : CodeCompOp::EXPR_KW);
  }
  if (wordToken.size && lexer.toStrRef(wordToken).back() == '\n' && lexer.isLastNewlineInserted()) {
    wordToken.size--;
  }
  this->lex = makeObserver(lexer);
  this->compWordToken = wordToken;
}

// for input completion

CompCandidate::CompCandidate(const QuoteParam *param, StringRef v, CompCandidateKind k, int p)
    : kind(k), suffixSpace(needSuffixSpace(v, k)), priority(p) {
  assert(!v.empty());
  switch (this->kind) {
  case CompCandidateKind::COMMAND_NAME:
  case CompCandidateKind::COMMAND_NAME_PART:
  case CompCandidateKind::COMMAND_ARG:
  case CompCandidateKind::COMMAND_TILDE:
  case CompCandidateKind::ENV_NAME: {
    const bool trimPrefix = param && !param->compWordToken.empty();
    if (trimPrefix) {
      assert(param->compWord.size() <= v.size());
      this->value += param->compWordToken;
      v.removePrefix(param->compWord.size());
    }
    if (this->kind != CompCandidateKind::COMMAND_TILDE) {
      quoteAsCmdOrShellArg(v, this->value,
                           this->kind == CompCandidateKind::COMMAND_NAME && !trimPrefix);
    } else {
      this->value += v;
    }
    break;
  }
  default:
    this->value += v;
    break;
  }
}

std::string CompCandidate::formatTypeSignature(const TypePool &pool) const {
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
  case CompCandidateKind::NATIVE_METHOD: {
    auto &info = this->getNativeMethodInfo();
    auto &recvType = pool.get(info.typeId);
    std::string packedParamTypes;
    if (isEqOrOrdTypeMethod(info.methodIndex)) {
      packedParamTypes += recvType.getNameRef();
    } else {
      auto typeParams = recvType.getTypeParams(pool);
      for (auto &p : typeParams) {
        if (!packedParamTypes.empty()) {
          packedParamTypes += ';';
        }
        packedParamTypes += p->getNameRef();
      }
    }
    formatNativeMethodSignature(info.methodIndex, packedParamTypes, ret);
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
  case CompCandidateKind::NATIVE_METHOD:
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
      if (StringRef sigName = e.abbrName; sigName.startsWith(prefix)) {
        consumer(sigName, CompCandidateKind::SIGNAL);
      }
    }
  }
}

static void completeUserName(const std::string &prefix, CompCandidateConsumer &consumer) {
#ifndef __ANDROID__
  setpwent();
  for (struct passwd *pw; (pw = getpwent()) != nullptr;) {
    StringRef pname = pw->pw_name;
    if (pname.startsWith(prefix)) {
      consumer(pname, CompCandidateKind::USER);
    }
  }
  endpwent();
#else
  static_cast<void>(prefix);
  static_cast<void>(consumer);
#endif
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

static void completeUDC(const StringRef compWordToken, const NameScope &scope,
                        const std::string &cmdPrefix, CompCandidateConsumer &consumer) {
  scope.walk([&](StringRef udc, const Handle &handle) {
    if (isCmdFullName(udc)) {
      udc.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      if (udc.startsWith(cmdPrefix)) {
        CompCandidate candidate({.compWordToken = compWordToken, .compWord = cmdPrefix}, udc,
                                CompCandidateKind::COMMAND_NAME);
        candidate.setCmdNameType(handle.is(HandleKind::UDC) ? CompCandidate::CmdNameType::UDC
                                                            : CompCandidate::CmdNameType::MOD);
        consumer(std::move(candidate));
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

static bool completeCmdName(const StringRef compWordToken, const NameScope &scope,
                            const std::string &cmdPrefix, const CodeCompOp option,
                            CompCandidateConsumer &consumer,
                            ObserverPtr<const CancelToken> cancel) {
  // complete user-defined command
  if (hasFlag(option, CodeCompOp::UDC)) {
    completeUDC(compWordToken, scope, cmdPrefix, consumer);
  }

  // complete builtin command
  if (hasFlag(option, CodeCompOp::BUILTIN)) {
    const auto range = getBuiltinCmdDescRange();
    for (auto &e : range) {
      if (StringRef builtin = e.name; builtin.startsWith(cmdPrefix)) {
        CompCandidate candidate({.compWordToken = compWordToken, .compWord = cmdPrefix}, builtin,
                                CompCandidateKind::COMMAND_NAME);
        candidate.setCmdNameType(CompCandidate::CmdNameType::BUILTIN);
        consumer(std::move(candidate));
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
        if (cancel && cancel->isCanceled()) {
          return false;
        }
        if (StringRef cmd = entry->d_name;
            cmd.startsWith(cmdPrefix) && isExecutable(dir.get(), entry)) {
          CompCandidate candidate({.compWordToken = compWordToken, .compWord = cmdPrefix}, cmd,
                                  CompCandidateKind::COMMAND_NAME);
          candidate.setCmdNameType(CompCandidate::CmdNameType::EXTERNAL);
          consumer(std::move(candidate));
        }
      }
      return true;
    }));
  }
  return true;
}

static bool completeFileName(StringRef compWordToken, const std::string &baseDir,
                             const StringRef prefix, const CodeCompOp op,
                             CompCandidateConsumer &consumer,
                             ObserverPtr<const CancelToken> cancel) {
  const auto dirSepIndex = prefix.lastIndexOf("/");

#ifndef __ANDROID__
  // complete tilde
  if (hasFlag(op, CodeCompOp::TILDE) && prefix.startsWith("~") && dirSepIndex == StringRef::npos) {
    setpwent();
    for (struct passwd *entry; (entry = getpwent()) != nullptr;) {
      auto tmp = prefix;
      tmp.removePrefix(1); // skip '~'
      if (StringRef pwName = entry->pw_name; pwName.startsWith(tmp)) {
        std::string value("~");
        value += pwName;
        value += '/';
        consumer(CompCandidate({.compWordToken = compWordToken, .compWord = prefix}, value,
                               CompCandidateKind::COMMAND_TILDE));
      }
    }
    endpwent();
    return true;
  }
#endif

  // complete file name

  /**
   * resolve directory path
   */
  std::string targetDir;
  if (dirSepIndex == 0) {
    targetDir = "/";
  } else if (dirSepIndex != StringRef::npos) {
    targetDir = prefix.substr(0, dirSepIndex).toString();
    if (hasFlag(op, CodeCompOp::TILDE)) {
      expandTilde(targetDir, true, nullptr);
    }
    targetDir = expandDots(baseDir.c_str(), targetDir.c_str());
  } else {
    targetDir = expandDots(baseDir.c_str(), ".");
  }

  /**
   * resolve basename
   */
  StringRef basenamePrefix = prefix;
  if (dirSepIndex != StringRef::npos) {
    basenamePrefix = basenamePrefix.substr(dirSepIndex + 1);
    if (!compWordToken.empty()) {
      const auto ss = compWordToken.lastIndexOf("/");
      assert(ss != StringRef::npos);
      compWordToken = compWordToken.substr(ss + 1);
    }
  }

  auto dir = openDir(targetDir.c_str());
  if (!dir) {
    return true;
  }
  for (dirent *entry; (entry = readdir(dir.get())) != nullptr;) {
    if (cancel && cancel->isCanceled()) {
      return false;
    }

    if (const StringRef basename = entry->d_name; basename.startsWith(basenamePrefix)) {
      if (basenamePrefix.empty() && (basename == ".." || basename == ".")) {
        continue;
      }

      std::string value;
      if (isDirectory(dir.get(), entry)) {
        value = basename.toString();
        value += '/';
      } else {
        if (hasFlag(op, CodeCompOp::EXEC)) {
          if (!isExecutable(dir.get(), entry)) {
            continue;
          }
        } else if (hasFlag(op, CodeCompOp::DIR) && !hasFlag(op, CodeCompOp::FILE)) {
          continue;
        }
        value = basename.toString();
      }
      const auto kind = hasFlag(op, CodeCompOp::EXEC) ? CompCandidateKind::COMMAND_NAME_PART
                                                      : CompCandidateKind::COMMAND_ARG;
      consumer(
          CompCandidate({.compWordToken = compWordToken, .compWord = basenamePrefix}, value, kind));
    }
  }
  return true;
}

static bool completeModule(const SysConfig &config, const StringRef compWordToken,
                           const std::string &scriptDir, const std::string &prefix, bool tilde,
                           CompCandidateConsumer &consumer, ObserverPtr<const CancelToken> cancel) {
  CodeCompOp op{};
  if (tilde) {
    op = CodeCompOp::TILDE;
  }

  // complete from SCRIPT_DIR
  TRY(completeFileName(compWordToken, scriptDir, prefix, op, consumer, cancel));

  if (!prefix.empty() && prefix[0] == '/') {
    return true;
  }

  // complete from the local module dir
  TRY(completeFileName(compWordToken, config.getModuleHome(), prefix, op, consumer, cancel));

  // complete from system module dir
  return completeFileName(compWordToken, config.getModuleDir(), prefix, op, consumer, cancel);
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
        consumer(std::move(candidate));
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
        consumer(std::move(candidate));
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
          consumer(std::move(candidate));
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
          unsigned int methodIndex = e.second ? e.second.handle()->getIndex() : e.second.index();
          CompCandidate candidate(name, CompCandidateKind::NATIVE_METHOD);
          candidate.setNativeMethodInfo(recvType, methodIndex);
          consumer(std::move(candidate));
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
      const auto priority = static_cast<int>(9000000 + i);
      consumer(paramNames[i], CompCandidateKind::PARAM, priority);
    }
  }
}

static bool completeBuiltinOption(const CmdNode &cmdNode, const Lexer &lexer,
                                  const std::string &word, CompCandidateConsumer &consumer) {
  if (cmdNode.getNameNode().getValue() != "shctl") {
    return false;
  }

  if (auto pair = cmdNode.findConstCmdArgNode(0); pair.first) {
    const auto ref = lexer.toStrRef(pair.first->getToken());
    if (ref == "set" || ref == "unset") {
      for (auto &e : getRuntimeOptionEntries()) {
        if (StringRef name = e.name; name.startsWith(word)) {
          consumer(name, CompCandidateKind::COMMAND_ARG);
        }
      }
    }
  } else { // only complete shctl sub-commands
    for (auto &e : getSHCTLSubCmdEntries()) {
      if (StringRef name = e.name; name.startsWith(word)) {
        consumer(name, CompCandidateKind::COMMAND_ARG);
      }
    }
  }
  return true;
}

static bool completeCLIOptionImpl(const CLIRecordType &type, const std::string &word, bool firstArg,
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
          candidate.overrideSuffixSpace(false);
        }
        consumer(std::move(candidate));
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

static bool completeCLIOption(const TypePool &pool, const Lexer &lexer, const CLIRecordType &type,
                              const CmdNode &cmdNode, const std::string &word,
                              CompCandidateConsumer &consumer) {
  const auto *cliType = &type;
  int latestSubCmdIndex = -1;
  const unsigned int size = cmdNode.getArgNodes().size();
  for (unsigned int i = 0; i < size; i++) {
    auto &e = cmdNode.getArgNodes()[i];
    if (isa<RedirNode>(*e) || !isa<CmdArgNode>(*e) || !cast<CmdArgNode>(*e).isConstArg()) {
      continue;
    }
    const StringRef ref = lexer.toStrRef(e->getToken());
    if (ref.empty() || ref[0] == '-') {
      break;
    }
    auto ret = cliType->findSubCmdInfo(pool, ref);
    if (ret.first) {
      cliType = ret.first;
      latestSubCmdIndex = static_cast<int>(i);
    }
  }
  const bool firstArg = size == 0 || (latestSubCmdIndex > -1 &&
                                      static_cast<unsigned int>(latestSubCmdIndex) == size - 1);
  return completeCLIOptionImpl(*cliType, word, firstArg, consumer);
}

static const CLIRecordType *resolveCLIType(const Type &type) {
  if (type.isFuncType()) {
    if (auto &funcType = cast<FunctionType>(type);
        funcType.getParamSize() == 1 && funcType.getParamTypeAt(0).isCLIRecordType()) {
      return cast<CLIRecordType>(&funcType.getParamTypeAt(0));
    }
  }
  return nullptr;
}

static bool completeSubCmdOrCLIOption(const TypePool &pool, const Lexer &lexer,
                                      const NameScope &scope, const CmdNode &cmdNode,
                                      const std::string &word, CompCandidateConsumer &consumer) {
  auto handle = scope.lookup(toCmdFullName(cmdNode.getNameNode().getValue()));
  if (!handle) { // try complete builtin command options
    return completeBuiltinOption(cmdNode, lexer, word, consumer);
  }
  auto &type = pool.get(handle.asOk()->getTypeId());
  if (const auto *cliType = resolveCLIType(type)) { // CLI
    return completeCLIOption(pool, lexer, *cliType, cmdNode, word, consumer);
  }

  // sub-command
  const auto *curModType = checked_cast<ModType>(&type);
  for (unsigned int offset = 0; curModType;) {
    auto [constNode, index] = cmdNode.findConstCmdArgNode(offset);
    if (!constNode) {
      if (index == cmdNode.getArgNodes().size()) { // reach end
        break;
      }
      return false;
    }
    auto hd = curModType->lookup(pool, toCmdFullName(constNode->getValue()));
    if (!hd) {
      return false;
    }
    curModType = checked_cast<ModType>(&pool.get(hd->getTypeId()));
    offset = index + 1;
  }
  if (!curModType) {
    return false;
  }
  curModType->walkField(pool, [&word, &consumer](StringRef name, const Handle &) {
    if (name.startsWith(word) && isCmdFullName(name)) {
      if (!name.startsWith("_")) {
        name.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
        consumer(name, CompCandidateKind::COMMAND_ARG);
      }
    }
    return true;
  });
  return true;
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
    StringRef compWordToken;
    if (ctx.getLexer()) {
      compWordToken = ctx.getLexer()->toStrRef(ctx.getCompWordToken());
    }
    TRY(completeCmdName(compWordToken, ctx.getScope(), ctx.getCompWord(), ctx.getCompOp(),
                        this->consumer, this->cancel));
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
    StringRef compWordToken;
    if (ctx.getLexer()) {
      compWordToken =
          ctx.getLexer()->toStrRef(ctx.getCompWordToken().sliceFrom(ctx.getCompWordTokenOffset()));
    }
    TRY(completeFileName(compWordToken, this->logicalWorkingDir, prefix, ctx.getCompOp(),
                         this->consumer, this->cancel));
  }
  if (ctx.has(CodeCompOp::MODULE)) {
    StringRef compWordToken;
    if (ctx.getLexer()) {
      compWordToken = ctx.getLexer()->toStrRef(ctx.getCompWordToken());
    }
    TRY(completeModule(this->config, compWordToken, ctx.getScriptDir(), ctx.getCompWord(),
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
    if (completeSubCmdOrCLIOption(this->pool, *ctx.getLexer(), ctx.getScope(), *ctx.getCmdNode(),
                                  ctx.getCompWord(), this->consumer)) {
      return true;
    }
    if (const auto op = ctx.getFallbackOp(); hasFlag(op, CodeCompOp::FILE)) {
      const auto prefix = StringRef(ctx.getCompWord()).substr(ctx.getCompWordOffset());
      StringRef compWordToken;
      if (ctx.getLexer()) {
        compWordToken = ctx.getLexer()->toStrRef(
            ctx.getCompWordToken().sliceFrom(ctx.getCompWordTokenOffset()));
      }
      TRY(completeFileName(compWordToken, this->logicalWorkingDir, prefix, op, this->consumer,
                           this->cancel));
    }
  }
  return true;
}

static LexerPtr lex(const std::string &scriptName, StringRef ref, const std::string &scriptDir) {
  ByteBuffer buf(ref.begin(), ref.end());
  if (!buf.empty() && buf.back() == '\n') {
    buf += '\n'; // explicitly append a newline for command name completion
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
  std::string target;
  unsigned int score{UINT32_MAX};
  ObserverPtr<const SuggestMemberType> targetMemberType;

public:
  explicit SuggestionCollector(StringRef name) : editDistance(3), src(name) {}

  void setTargetMemberType(const SuggestMemberType &memberType) {
    this->targetMemberType = makeObserver(memberType);
  }

  std::string take() && { return std::move(this->target); }

  unsigned int getScore() const { return this->score; }

  void operator()(CompCandidate &&candidate) override {
    if (this->targetMemberType) {
      const auto targetType = *this->targetMemberType;
      if (candidate.kind == CompCandidateKind::FIELD &&
          !hasFlag(targetType, SuggestMemberType::FIELD)) {
        return;
      }
      if ((candidate.kind == CompCandidateKind::METHOD ||
           candidate.kind == CompCandidateKind::NATIVE_METHOD) &&
          !hasFlag(targetType, SuggestMemberType::METHOD)) {
        return;
      }
    }

    const StringRef ref = candidate.value;
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
      this->target = std::move(candidate.value);
    }
  }
};

std::string suggestSimilarVarName(StringRef name, const NameScope &scope, unsigned int threshold) {
  if (name.empty() || name == "_") {
    return "";
  }
  SuggestionCollector collector(name);
  completeVarName(scope, "", false, collector);
  if (collector.getScore() <= threshold) {
    return std::move(collector).take();
  }
  return "";
}

std::string suggestSimilarType(StringRef name, const TypePool &pool, const NameScope &scope,
                               const Type *recvType, unsigned int threshold) {
  if (name.empty() || name == "_") {
    return "";
  }
  SuggestionCollector collector(name);
  completeType(pool, scope, recvType, "", collector);
  if (collector.getScore() <= threshold) {
    return std::move(collector).take();
  }
  return "";
}

std::string suggestSimilarMember(StringRef name, const TypePool &pool, const NameScope &scope,
                                 const Type &recvType, SuggestMemberType targetType,
                                 unsigned int threshold) {
  if (name.empty() || name == "_") {
    return "";
  }
  SuggestionCollector collector(name);
  collector.setTargetMemberType(targetType);
  completeMember(pool, scope, recvType, "", collector);
  if (collector.getScore() <= threshold) {
    return std::move(collector).take();
  }
  return "";
}

std::string suggestSimilarParamName(StringRef name, const std::vector<std::string> &paramNames,
                                    unsigned int threshold) {
  SuggestionCollector collector(name);
  completeParamName(paramNames, "", collector);
  if (collector.getScore() <= threshold) {
    return std::move(collector).take();
  }
  return "";
}

} // namespace arsh