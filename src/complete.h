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

#ifndef ARSH_COMPLETE_H
#define ARSH_COMPLETE_H

#include "misc/enum_util.hpp"
#include "misc/resource.hpp"
#include "misc/string_ref.hpp"

#include "comp_context.h"
#include "frontend.h"

namespace arsh {

enum class CompCandidateKind : unsigned char {
  COMMAND_NAME,
  COMMAND_NAME_PART,
  COMMAND_ARG,
  COMMAND_TILDE,
  USER_SPECIFIED,
  ENV_NAME,
  VALID_ENV_NAME,
  USER,
  GROUP,
  VAR,
  VAR_IN_CMD_ARG,
  PARAM,
  SIGNAL,
  FIELD,
  METHOD,
  NATIVE_METHOD,
  KEYWORD,
  TYPE,
};

class CompCandidate {
public:
  enum class CmdNameType : unsigned char {
    MOD,
    UDC,
    BUILTIN,
    DYNA_UDC,
    EXTERNAL,
  };

  struct QuoteParam {
    StringRef compWordToken;
    StringRef compWord;
  };

  std::string value;
  const CompCandidateKind kind;
  bool suffixSpace;
  const int priority;

private:
  union {
    const Handle *handle;
    struct {
      unsigned int recvTypeId;
      unsigned int typeId;
    } fieldInfo;
    struct {
      unsigned int typeId;
      unsigned int methodIndex;
    } nativeMethodHandleInfo;
    CmdNameType cmdNameType;
  } meta{};

  CompCandidate(const QuoteParam *param, StringRef v, CompCandidateKind kind, int priority);

public:
  CompCandidate(StringRef v, CompCandidateKind k, int p = 0) : CompCandidate(nullptr, v, k, p) {}

  CompCandidate(const QuoteParam &param, StringRef v, CompCandidateKind kind)
      : CompCandidate(&param, v, kind, 0) {}

  void setHandle(const Handle &handle) { this->meta.handle = &handle; }

  const Handle *getHandle() const { return this->meta.handle; }

  void setFieldInfo(const Type &recvType, const Handle &field) {
    this->meta.fieldInfo = {
        .recvTypeId = recvType.typeId(),
        .typeId = field.getTypeId(),
    };
  }

  const auto &getFieldInfo() const { return this->meta.fieldInfo; }

  void setNativeMethodInfo(const Type &type, unsigned int methodIndex) {
    this->meta.nativeMethodHandleInfo = {
        .typeId = type.typeId(),
        .methodIndex = methodIndex,
    };
  }

  const auto &getNativeMethodInfo() const { return this->meta.nativeMethodHandleInfo; }

  void setCmdNameType(CmdNameType t) { this->meta.cmdNameType = t; }

  CmdNameType getCmdNameType() const { return this->meta.cmdNameType; }

  void overrideSuffixSpace(bool space) { this->suffixSpace = space; }

  std::string formatTypeSignature(const TypePool &pool) const;

  bool needSuffixSpace() const { return this->suffixSpace; }

  static bool needSuffixSpace(StringRef value, CompCandidateKind kind);
};

class CompCandidateConsumer {
public:
  virtual ~CompCandidateConsumer() = default;

  void operator()(StringRef ref, CompCandidateKind kind, int priority = 0) {
    (*this)(CompCandidate(ref, kind, priority));
  }

  virtual void operator()(CompCandidate &&candidate) = 0;
};

/**
 *
 * @param scope
 * @param prefix
 * not start with '$'
 * @param inCmdArg
 * @param consumer
 */
void completeVarName(const NameScope &scope, StringRef prefix, bool inCmdArg,
                     CompCandidateConsumer &consumer);

void completeMember(const TypePool &pool, const NameScope &scope, const Type &recvType,
                    StringRef word, CompCandidateConsumer &consumer);

void completeType(const TypePool &pool, const NameScope &scope, const Type *recvType,
                  StringRef word, CompCandidateConsumer &consumer);

/**
 * if failed (cannot call user-defined comp or error), return -1
 * otherwise, return the number of consumed completion candidates
 */
using UserDefinedComp = std::function<int(const CodeCompletionContext &, unsigned int,
                                          const ModType *, CompCandidateConsumer &consumer)>;

using DynaUdcComp = std::function<void(const std::string &word, CompCandidateConsumer &consumer)>;

class CodeCompleter {
private:
  CompCandidateConsumer &consumer;
  ObserverPtr<FrontEnd::ModuleProvider> provider;
  const SysConfig &config;
  const TypePool &pool;
  const std::string &logicalWorkingDir;
  UserDefinedComp userDefinedComp;
  DynaUdcComp dynaUdcComp;
  ObserverPtr<const CancelToken> cancel;

public:
  CodeCompleter(CompCandidateConsumer &consumer, ObserverPtr<FrontEnd::ModuleProvider> provider,
                const SysConfig &config, const TypePool &pool, const std::string &workDir)
      : consumer(consumer), provider(provider), config(config), pool(pool),
        logicalWorkingDir(workDir) {}

  void setUserDefinedComp(UserDefinedComp &&comp) { this->userDefinedComp = std::move(comp); }

  void setDynaUdcComp(DynaUdcComp &&comp) { this->dynaUdcComp = std::move(comp); }

  void setCancel(const CancelToken &c) { this->cancel = makeObserver(c); }

  /**
   * if a module provider is specified, parse 'ref' and complete candidates (except for `option`)
   * otherwise complete candidates corresponding to 'option'
   * @param scope
   * @param scriptName
   * @param ref
   * @param option
   * @return
   * if canceled (interrupted by signal or has error), return false
   */
  bool operator()(NameScopePtr scope, const std::string &scriptName, StringRef ref,
                  CodeCompOp option);

private:
  /**
   * @param ctx
   * @return
   * if canceled (interrupted by signal or has error), return false
   */
  bool invoke(const CodeCompletionContext &ctx);
};

// for error suggestion

/**
 * get the similar var name from scope
 * @param name
 * @param scope
 * @param threshold
 * @return
 * if the suggestion score (edit distance) is greater than the threshold, return the empty string
 */
std::string suggestSimilarVarName(StringRef name, const NameScope &scope,
                                  unsigned int threshold = 3);

/**
 * get the similar type name from scope and type pool
 * @param name
 * @param pool
 * @param scope
 * @param recvType
 * maybe null
 * @param threshold
 * @return
 * if the suggestion score (edit distance) is greater than the threshold, return the empty string
 */
std::string suggestSimilarType(StringRef name, const TypePool &pool, const NameScope &scope,
                               const Type *recvType, unsigned int threshold = 3);

enum class SuggestMemberType : unsigned char {
  FIELD = 1u << 0u,
  METHOD = 1u << 1u,
};

template <>
struct allow_enum_bitop<SuggestMemberType> : std::true_type {};

/**
 * get the similar member (field/method)
 * @param name
 * @param pool
 * @param scope
 * @param recvType
 * @param targetType
 * @param threshold
 * @return
 * if the suggestion score (edit distance) is greater than the threshold, return the empty string
 */
std::string suggestSimilarMember(StringRef name, const TypePool &pool, const NameScope &scope,
                                 const Type &recvType, SuggestMemberType targetType,
                                 unsigned int threshold = 3);

std::string suggestSimilarParamName(StringRef name, const std::vector<std::string> &paramNames,
                                    unsigned int threshold = 3);

} // namespace arsh

#endif // ARSH_COMPLETE_H
