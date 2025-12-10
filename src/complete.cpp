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

CompCandidate::CompCandidate(const CompPrefix &prefix, CompCandidateKind k, StringRef v, int p)
    : kind(k), suffixSpace(needSuffixSpace(v, k)), priority(p),
      prefixSize(std::max(prefix.compWordToken.size(), prefix.compWord.size())) {
  assert(!v.empty());
  switch (this->kind) {
  case CompCandidateKind::COMMAND_NAME:
  case CompCandidateKind::COMMAND_NAME_PART:
  case CompCandidateKind::COMMAND_ARG:
  case CompCandidateKind::COMMAND_TILDE:
  case CompCandidateKind::ENV_NAME: {
    const bool trimPrefix = !prefix.compWordToken.empty();
    if (trimPrefix) {
      assert(prefix.compWord.size() <= v.size());
      this->value += prefix.compWordToken;
      v.removePrefix(prefix.compWord.size());
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
      consumer(prefix, CompCandidateKind::KEYWORD, value);
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
      consumer(namePrefix, kind, name);
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
        consumer(prefix, CompCandidateKind::SIGNAL, sigName);
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
      consumer(prefix, CompCandidateKind::USER, pname);
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
      consumer(prefix, CompCandidateKind::GROUP, gname);
    }
  }
  endgrent();
}

static auto udcCandidateConsumer(const CompPrefix &prefix, CompCandidateConsumer &consumer,
                                 const bool allowPrivate = true) {
  return [allowPrivate, &prefix, &consumer](StringRef udc, const Handle &handle) {
    if (isCmdFullName(udc)) {
      udc.removeSuffix(strlen(CMD_SYMBOL_SUFFIX));
      if (udc.startsWith(prefix.compWord) && (allowPrivate || !udc.startsWith("_"))) {
        CompCandidate candidate(prefix, CompCandidateKind::COMMAND_NAME, udc);
        candidate.setCmdNameType(handle.is(HandleKind::UDC) ? CompCandidate::CmdNameType::UDC
                                                            : CompCandidate::CmdNameType::MOD);
        consumer(std::move(candidate));
      }
    }
    return true;
  };
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

static bool completeCmdName(const CompPrefix &prefix, const NameScope &scope,
                            const CodeCompOp option, CompCandidateConsumer &consumer,
                            ObserverPtr<const CancelToken> cancel) {
  // complete user-defined command
  if (hasFlag(option, CodeCompOp::UDC)) {
    scope.walk(udcCandidateConsumer(prefix, consumer));
  }

  // complete builtin command
  if (hasFlag(option, CodeCompOp::BUILTIN)) {
    const auto range = getBuiltinCmdDescRange();
    for (auto &e : range) {
      if (StringRef builtin = e.name; builtin.startsWith(prefix.compWord)) {
        CompCandidate candidate(prefix, CompCandidateKind::COMMAND_NAME, builtin);
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
            cmd.startsWith(prefix.compWord) && isExecutable(dir.get(), entry)) {
          CompCandidate candidate(prefix, CompCandidateKind::COMMAND_NAME, cmd);
          candidate.setCmdNameType(CompCandidate::CmdNameType::EXTERNAL);
          consumer(std::move(candidate));
        }
      }
      return true;
    }));
  }
  return true;
}

static bool completeFileName(const CompPrefix &prefix, const std::string &baseDir,
                             const CodeCompOp op, CompCandidateConsumer &consumer,
                             ObserverPtr<const CancelToken> cancel) {
  const auto dirSepIndex = prefix.compWord.lastIndexOf("/");

#ifndef __ANDROID__
  // complete tilde
  if (hasFlag(op, CodeCompOp::TILDE) && prefix.compWord.startsWith("~") &&
      dirSepIndex == StringRef::npos) {
    setpwent();
    for (struct passwd *entry; (entry = getpwent()) != nullptr;) {
      auto tmp = prefix.compWord;
      tmp.removePrefix(1); // skip '~'
      if (StringRef pwName = entry->pw_name; pwName.startsWith(tmp)) {
        std::string value("~");
        value += pwName;
        value += '/';
        consumer(prefix, CompCandidateKind::COMMAND_TILDE, value);
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
    targetDir = prefix.compWord.substr(0, dirSepIndex).toString();
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
  CompPrefix basenamePrefix = prefix;
  if (dirSepIndex != StringRef::npos) {
    basenamePrefix.compWord = basenamePrefix.compWord.substr(dirSepIndex + 1);
    if (!basenamePrefix.compWordToken.empty()) {
      const auto ss = basenamePrefix.compWordToken.lastIndexOf("/");
      assert(ss != StringRef::npos);
      basenamePrefix.compWordToken = basenamePrefix.compWordToken.substr(ss + 1);
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

    if (const StringRef basename = entry->d_name; basename.startsWith(basenamePrefix.compWord)) {
      if (basenamePrefix.compWord.empty() && (basename == ".." || basename == ".")) {
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
      consumer(basenamePrefix, kind, value);
    }
  }
  return true;
}

static bool completeModule(const SysConfig &config, const CompPrefix &prefix,
                           const std::string &scriptDir, bool tilde,
                           CompCandidateConsumer &consumer, ObserverPtr<const CancelToken> cancel) {
  CodeCompOp op{};
  if (tilde) {
    op = CodeCompOp::TILDE;
  }

  // complete from SCRIPT_DIR
  TRY(completeFileName(prefix, scriptDir, op, consumer, cancel));

  if (!prefix.compWord.empty() && prefix.compWord[0] == '/') {
    return true;
  }

  // complete from the local module dir
  TRY(completeFileName(prefix, config.getModuleHome(), op, consumer, cancel));

  // complete from system module dir
  return completeFileName(prefix, config.getModuleDir(), op, consumer, cancel);
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
        CompCandidate candidate(prefix, kind, varName, priority);
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
        consumer(prefix, CompCandidateKind::KEYWORD, e);
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
        CompCandidate candidate(word, CompCandidateKind::FIELD, name);
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
          CompCandidate candidate(word, CompCandidateKind::METHOD, name);
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
          CompCandidate candidate(word, CompCandidateKind::NATIVE_METHOD, name);
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
          consumer(word, CompCandidateKind::TYPE, name);
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
      consumer(word, CompCandidateKind::TYPE, name);
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
      consumer(word, CompCandidateKind::TYPE, name);
    }
  }

  // search TypeTemplate
  for (auto &e : pool.getTemplateMap()) {
    if (StringRef name = e.first; name.startsWith(word)) {
      consumer(word, CompCandidateKind::TYPE, name);
    }
  }

  // typeof
  if (constexpr StringRef name = "typeof"; name.startsWith(word)) {
    consumer(word, CompCandidateKind::TYPE, name);
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
      consumer(prefix, CompCandidateKind::KEYWORD, attr);
    }
  }
}

static void completeAttributeParam(const std::string &prefix, AttributeParamSet paramSet,
                                   CompCandidateConsumer &consumer) {
  paramSet.iterate([&](Attribute::Param param) {
    StringRef ref = toString(param);
    if (ref.startsWith(prefix)) {
      consumer(prefix, CompCandidateKind::KEYWORD, ref);
    }
  });
}

static void completeParamName(const std::vector<std::string> &paramNames, const StringRef word,
                              CompCandidateConsumer &consumer) {
  const unsigned int size = paramNames.size();
  for (unsigned int i = 0; i < size; i++) {
    if (StringRef ref = paramNames[i]; ref.startsWith(word)) {
      const auto priority = static_cast<int>(9000000 + i);
      consumer(word, CompCandidateKind::PARAM, paramNames[i], priority);
    }
  }
}

static CmdArgCompStatus completeCLIArg(StringRef opt, const ArgEntry &entry,
                                       const CompPrefix &prefix,
                                       ObserverPtr<ForeignCompHandler> comp,
                                       CompCandidateConsumer &consumer) {
  if (entry.getCheckerKind() == ArgEntry::CheckerKind::CHOICE) {
    for (auto &e : entry.getChoice()) {
      if (StringRef ref = e; ref.startsWith(prefix.compWord)) {
        consumer(prefix, CompCandidateKind::COMMAND_ARG, ref);
      }
    }
    return CmdArgCompStatus::OK;
  }
  if (auto handle = entry.getCompHandle(); handle && comp) {
    return comp->callCLIComp(*handle, opt, prefix.compWord, consumer);
  }
  return CmdArgCompStatus::INVALID;
}

static CmdArgCompStatus completeCLIFlagOrOption(const CLIRecordType &type, const CompPrefix &prefix,
                                                ObserverPtr<ForeignCompHandler> comp,
                                                CompCandidateConsumer &consumer) {
  for (auto &e : type.getEntries()) {
    if (!e.isOption()) {
      continue;
    }

    // short option
    if (e.getShortName()) {
      std::string value;
      value += '-';
      value += e.getShortName();
      if (StringRef(value).startsWith(prefix.compWord)) {
        consumer(prefix, CompCandidateKind::COMMAND_ARG, value);
      } else if (e.op == OptParseOp::OPT_ARG && prefix.compWord.startsWith(value)) {
        CompPrefix remain = prefix;
        remain.removePrefix(value.size());
        return completeCLIArg(value, e, remain, comp, consumer);
      }
    }
    // long option
    if (e.getLongName().empty()) {
      continue;
    }
    std::string value = "--";
    value += e.getLongName();
    if (e.getParseOp() == OptParseOp::HAS_ARG) {
      value += '=';
    }
    if (StringRef(value).startsWith(prefix.compWord)) {
      if (value.size() == prefix.compWord.size() &&
          value.back() == '=') { // 'value == word' and '--long='
        return completeCLIArg(value, e, CompPrefix{}, comp, consumer);
      }
      CompCandidate candidate(prefix, CompCandidateKind::COMMAND_ARG, value);
      if (value.back() == '=') {
        candidate.overrideSuffixSpace(false);
      }
      consumer(std::move(candidate));
    } else if (e.op != OptParseOp::NO_ARG) {
      if (value.back() != '=') {
        value += '=';
      }
      if (prefix.compWord.startsWith(value)) {
        CompPrefix remain = prefix;
        remain.removePrefix(value.size());
        return completeCLIArg(value, e, remain, comp, consumer);
      }
    }
  }
  return CmdArgCompStatus::OK;
}

static const ArgEntry *resolveArgEntryNeedArg(const CLIRecordType &type, StringRef opt) {
  assert(opt.startsWith("-"));
  const bool longOpt = opt.startsWith("--");
  opt.removePrefix(longOpt ? 2 : 1);
  for (auto &e : type.getEntries()) {
    if (!e.isOption() || e.op != OptParseOp::HAS_ARG) {
      continue;
    }
    if (longOpt) {
      if (!e.getLongName().empty() && opt == e.getLongName()) { // --long
        return &e;
      }
    } else if (opt.size() == 1 && e.getShortName() == opt[0]) { // -s
      return &e;
    }
  }
  return nullptr;
}

static CmdArgCompStatus completeCLIOption(const TypePool &pool, const CLIRecordType *cliType,
                                          const CmdNode &cmdNode, unsigned int argOffset,
                                          const CompPrefix &prefix,
                                          ObserverPtr<ForeignCompHandler> comp,
                                          CompCandidateConsumer &consumer) {
  /**
   * cmd <word>
   * => cliType=cmd, lastOpt=null, argCount=0
   *
   * cmd sub1 sub2 -A <word>
   * => cliType=sub2, lastOpt=-A, argCount=0
   *
   * cmd -A arg1 arg2 -B <word>
   * => cliType=cmd, lastOpt=-B, argCount=2
   *
   * cmd -A arg1 <word>
   * => cliTYpe=cmd, lastOpt=null, argCount=1
   */
  assert(cliType);
  const std::string *lastOpt = nullptr;
  unsigned int argCount = 0;
  while (true) {
    auto [arg, cur] = cmdNode.findConstCmdArg(argOffset);
    if (!arg) {
      if (cur == cmdNode.getArgNodes().size()) { // reach end
        break;
      }
      return CmdArgCompStatus::INVALID;
    }
    if (!arg->empty() && (*arg)[0] == '-') { // option
      lastOpt = arg;
    } else if (lastOpt) { // cmd -A -B arg
      lastOpt = nullptr;
      argCount++;
    } else if (!argCount) {
      if (auto ret = cliType->findSubCmdInfo(pool, *arg); ret.first) { // cmd sub
        cliType = ret.first;
      } else { // cmd arg
        argCount++;
      }
    } else { // cmd arg1 arg2
      argCount++;
    }
    argOffset = cur + 1;
  }

  // complete sub-commands
  if (!lastOpt && !argCount && (prefix.compWord.empty() || prefix.compWord[0] != '-') &&
      hasFlag(cliType->getAttr(), CLIRecordType::Attr::HAS_SUBCMD)) {
    for (auto &e : cliType->getEntries()) {
      if (!e.isSubCmd()) {
        continue;
      }
      if (StringRef name(e.getArgName()); name.startsWith(prefix.compWord)) {
        consumer(prefix, CompCandidateKind::COMMAND_ARG, name);
      }
    }
    return CmdArgCompStatus::OK;
  }

  // complete Flag/Option
  if (!prefix.compWord.empty() && prefix.compWord[0] == '-') {
    return completeCLIFlagOrOption(*cliType, prefix, comp, consumer);
  }

  // complete optional arg
  if (lastOpt) {
    if (auto *entry = resolveArgEntryNeedArg(*cliType, *lastOpt)) {
      return completeCLIArg(*lastOpt, *entry, prefix, comp, consumer);
    }
  }
  return CmdArgCompStatus::INVALID; // TODO: positional arguments
}

static const CLIRecordType *resolveCLIType(const FunctionType &funcType) {
  if (funcType.getParamSize() == 1 && funcType.getParamTypeAt(0).isCLIRecordType()) {
    return cast<CLIRecordType>(&funcType.getParamTypeAt(0));
  }
  return nullptr;
}

static CmdArgCompStatus tryToCallUserDefinedComp(const CodeCompletionContext &ctx,
                                                 ObserverPtr<ForeignCompHandler> comp,
                                                 const unsigned int offset,
                                                 const ModType *cmdModType,
                                                 CompCandidateConsumer &consumer) {
  return comp ? comp->callUserDefinedComp(ctx, offset, cmdModType, consumer)
              : CmdArgCompStatus::INVALID;
}

static CmdArgCompStatus completeCmdArg(const TypePool &pool, ObserverPtr<ForeignCompHandler> comp,
                                       const CodeCompletionContext &ctx,
                                       CompCandidateConsumer &consumer) {
  const auto &cmdNode = *ctx.getCmdNode();
  auto handle = ctx.getScope().lookup(toCmdFullName(cmdNode.getNameNode().getValue()));
  if (!handle || !pool.get(handle.asOk()->getTypeId()).isModType()) { // call for non-module
    if (const auto s = tryToCallUserDefinedComp(ctx, comp, 0, nullptr, consumer);
        !handle || s != CmdArgCompStatus::INVALID) {
      return s;
    }
  }

  // sub-command
  const ModType *belongedModType = nullptr;
  const auto *curModType = checked_cast<ModType>(&pool.get(handle.asOk()->getTypeId()));
  const auto *curUdcType = checked_cast<FunctionType>(&pool.get(handle.asOk()->getTypeId()));
  unsigned int offset = 0;
  while (curModType) {
    auto [arg, index] = cmdNode.findConstCmdArg(offset);
    if (!arg) {
      if (index == cmdNode.getArgNodes().size()) { // reach end
        break;
      }
      return CmdArgCompStatus::INVALID;
    }
    auto hd = curModType->lookup(pool, toCmdFullName(*arg));
    if (!hd) {
      return CmdArgCompStatus::INVALID;
    }
    belongedModType = pool.getModTypeById(hd->getModId());
    curModType = checked_cast<ModType>(&pool.get(hd->getTypeId()));
    curUdcType = checked_cast<FunctionType>(&pool.get(hd->getTypeId()));
    offset = index + 1;
  }
  if (curUdcType) {
    if (belongedModType && offset <= cmdNode.getArgNodes().size() && offset > 0) {
      if (const auto s = tryToCallUserDefinedComp(ctx, comp, offset - 1, belongedModType, consumer);
          s != CmdArgCompStatus::INVALID) {
        return s;
      }
    }
    if (auto *cliType = resolveCLIType(*curUdcType)) {
      return completeCLIOption(pool, cliType, cmdNode, offset, ctx.toCompPrefix(), comp, consumer);
    }
  } else if (curModType) {
    curModType->walkField(pool, udcCandidateConsumer(ctx.toCompPrefix(), consumer, false));
    return CmdArgCompStatus::OK;
  }
  return CmdArgCompStatus::INVALID;
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
    TRY(completeCmdName(ctx.toCompPrefix(), ctx.getScope(), ctx.getCompOp(), this->consumer,
                        this->cancel));
  }
  if (ctx.has(CodeCompOp::DYNA_UDC) && this->foreignComp) {
    this->foreignComp->completeDynamicUdc(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::USER)) {
    completeUserName(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::GROUP)) {
    completeGroupName(ctx.getCompWord(), this->consumer);
  }
  if (ctx.has(CodeCompOp::FILE) || ctx.has(CodeCompOp::EXEC) || ctx.has(CodeCompOp::DIR)) {
    TRY(completeFileName(ctx.toCompPrefixByOffset(), this->logicalWorkingDir, ctx.getCompOp(),
                         this->consumer, this->cancel));
  }
  if (ctx.has(CodeCompOp::MODULE)) {
    TRY(completeModule(this->config, ctx.toCompPrefix(), ctx.getScriptDir(),
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
    assert(ctx.getRecvType());
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
    switch (completeCmdArg(this->pool, this->foreignComp, ctx, this->consumer)) {
    case CmdArgCompStatus::OK:
      return true;
    case CmdArgCompStatus::INVALID:
      break;
    case CmdArgCompStatus::CANCEL:
      return false;
    }
    if (const auto op = ctx.getFallbackOp(); hasFlag(op, CodeCompOp::FILE)) {
      TRY(completeFileName(ctx.toCompPrefixByOffset(), this->logicalWorkingDir, op, this->consumer,
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