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

#ifndef YDSH_COMPLETE_H
#define YDSH_COMPLETE_H

#include <vector>

#include "misc/enum_util.hpp"
#include "misc/resource.hpp"
#include "misc/string_ref.hpp"

#include "frontend.h"

namespace ydsh {

enum class CodeCompOp : unsigned int {
  FILE = 1u << 0u,       /* complete file names (including directory) */
  DIR = 1u << 1u,        /* complete directory names (directory only) */
  EXEC = 1u << 2u,       /* complete executable file names (including directory) */
  TILDE = 1u << 3u,      /* perform tilde expansion before completions */
  EXTERNAL = 1u << 4u,   /* complete external command names */
  DYNA_UDC = 1u << 5u,   /* complete dynamically registered command names */
  BUILTIN = 1u << 6u,    /* complete builtin command names */
  UDC = 1u << 7u,        /* complete user-defined command names */
  VAR = 1u << 8u,        /* complete global variable names (not start with $) */
  ENV = 1u << 9u,        /* complete environmental variable names */
  VALID_ENV = 1u << 10u, /* complete environmental variable names (valid name only) */
  SIGNAL = 1u << 11u,    /* complete signal names (not start with SIG) */
  USER = 1u << 12u,      /* complete usernames */
  GROUP = 1u << 13u,     /* complete group names */
  MODULE = 1u << 14u,    /* complete module path */
  STMT_KW = 1u << 15u,   /* complete statement keyword */
  EXPR_KW = 1u << 16u,   /* complete expr keyword */
  EXPECT = 1u << 17u,    /* complete expected token */
  MEMBER = 1u << 18u,    /* complete member (field/method) */
  TYPE = 1u << 19u,      /* complete type name */
  CMD_ARG = 1u << 20u,   /* for command argument */
  HOOK = 1u << 21u,      /* for user-defined completion hook */
  COMMAND = EXTERNAL | DYNA_UDC | BUILTIN | UDC,
};

template <>
struct allow_enum_bitop<CodeCompOp> : std::true_type {};

inline bool willKickFrontEnd(CodeCompOp op) { return empty(op); }

inline bool isKeyword(StringRef value) { return !value.startsWith("<") || !value.endsWith(">"); }

enum class CompCandidateKind {
  COMMAND_NAME,
  COMMAND_NAME_PART,
  COMMAND_ARG,
  COMMAND_TILDE,
  COMMAND_ARG_NO_QUOTE,
  ENV_NAME,
  VALID_ENV_NAME,
  USER,
  GROUP,
  VAR,
  VAR_IN_CMD_ARG,
  SIGNAL,
  FIELD,
  METHOD,
  KEYWORD,
  TYPE,
};

inline bool mayBeQuoted(CompCandidateKind kind) {
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

struct CompCandidate {
  const StringRef value;
  const CompCandidateKind kind;
  const int priority;

  CompCandidate(StringRef v, CompCandidateKind k, int p) : value(v), kind(k), priority(p) {}

  /**
   * quote as shell arg
   * @return
   */
  std::string quote() const;
};

class CompCandidateConsumer {
public:
  virtual ~CompCandidateConsumer() = default;

  void operator()(StringRef ref, CompCandidateKind kind) { (*this)(ref, kind, 0); }

  void operator()(StringRef ref, CompCandidateKind kind, int priority) {
    CompCandidate candidate(ref, kind, priority);
    (*this)(candidate);
  }

  virtual void operator()(const CompCandidate &candidate) = 0;
};

/**
 * if failed (cannot call user-defined comp or error), return -1
 * otherwise, return number of consumed completion candidates
 */
using UserDefinedComp =
    std::function<int(const Lexer &lex, const CmdNode &cmdNode, const std::string &word, bool tilde,
                      CompCandidateConsumer &consumer)>;

using DynaUdcComp = std::function<void(const std::string &word, CompCandidateConsumer &consumer)>;

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

void completeMember(const TypePool &pool, const NameScope &scope, const DSType &recvType,
                    StringRef word, CompCandidateConsumer &consumer);

void completeType(const TypePool &pool, const NameScope &scope, const DSType *recvType,
                  StringRef word, CompCandidateConsumer &consumer);

class CodeCompletionHandler {
private:
  UserDefinedComp userDefinedComp;

  DynaUdcComp dynaUdcComp;

  const SysConfig &config;

  const TypePool &pool;

  const std::string &logicalWorkdir;

  ObserverPtr<const Lexer> lex;

  const std::string &scriptDir; // for module completion

  ObserverPtr<CancelToken> cancel;

  /**
   * current completion word
   */
  std::string compWord;

  /**
   * for expected tokens
   */
  std::vector<std::string> extraWords;

  /**
   * for COMP_HOOK
   */
  std::unique_ptr<CmdNode> cmdNode;

  /**
   * for var name completion
   */
  NameScopePtr scope;

  /**
   * for member completion
   */
  const DSType *recvType{nullptr};

  CodeCompOp compOp{};

  /**
   * when result of COMP_HOOK is empty, fallback to file name completion
   */
  CodeCompOp fallbackOp{};

  /**
   * for file name completion with tilde expansion like the following case
   *  `dd if=~/'
   */
  unsigned int compWordOffset{0};

public:
  CodeCompletionHandler(const SysConfig &config, const TypePool &pool,
                        const std::string &logicalWorkdir, NameScopePtr scope,
                        const std::string &scriptDir)
      : config(config), pool(pool), logicalWorkdir(logicalWorkdir), scriptDir(scriptDir),
        scope(std::move(scope)) {}

  void addCompRequest(CodeCompOp op, std::string &&word) {
    this->compOp = op;
    this->compWord = std::move(word);
  }

  void setCompWordOffset(unsigned int offset) { this->compWordOffset = offset; }

  void ignore(CodeCompOp ignored) { unsetFlag(this->compOp, ignored); }

  void addExpectedTokenRequest(std::string &&prefix, TokenKind kind) {
    TokenKind kinds[] = {kind};
    this->addExpectedTokenRequests(std::move(prefix), 1, kinds);
  }

  template <unsigned int N>
  void addExpectedTokenRequests(std::string &&prefix, const TokenKind (&kinds)[N]) {
    this->addExpectedTokenRequests(std::move(prefix), N, kinds);
  }

  void addExpectedTokenRequests(std::string &&prefix, unsigned int size, const TokenKind *kinds) {
    unsigned count = 0;
    for (unsigned int i = 0; i < size; i++) {
      const char *value = toString(kinds[i]);
      if (isKeyword(value)) {
        this->compOp = CodeCompOp::EXPECT;
        this->extraWords.emplace_back(value);
        count++;
      }
    }
    if (count > 0) {
      this->compWord = std::move(prefix);
    }
  }

  void addVarNameRequest(std::string &&value, bool inCmdArg, NameScopePtr curScope) {
    this->scope = std::move(curScope);
    auto op = CodeCompOp::VAR;
    if (inCmdArg) {
      setFlag(op, CodeCompOp::CMD_ARG);
    }
    this->addCompRequest(op, std::move(value));
  }

  void addTypeNameRequest(std::string &&value, const DSType *type, NameScopePtr curScope);

  void addMemberRequest(const DSType &type, std::string &&value) {
    this->compOp = CodeCompOp::MEMBER;
    this->recvType = &type;
    this->compWord = std::move(value);
  }

  enum class CMD_OR_KW_OP {
    STMT = 1u << 0u,
    TILDE = 1u << 1u,
  };

  void addCmdOrKeywordRequest(std::string &&value, CMD_OR_KW_OP op);

  void addCompHookRequest(const Lexer &lexer, std::unique_ptr<CmdNode> &&node) {
    this->lex = makeObserver(lexer);
    this->fallbackOp = this->compOp;
    this->compOp = CodeCompOp::HOOK;
    this->cmdNode = std::move(node);
  }

  bool hasCompRequest() const { return !empty(this->compOp); }

  CodeCompOp getCompOp() const { return this->compOp; }

  void setUserDefinedComp(const UserDefinedComp &comp) { this->userDefinedComp = comp; }

  void setDynaUdcComp(const DynaUdcComp &comp) { this->dynaUdcComp = comp; }

  void setCancel(ObserverPtr<CancelToken> c) { this->cancel = c; }

  /**
   *
   * @param consumer
   * @return
   * if cancelled, return false
   */
  bool invoke(CompCandidateConsumer &consumer);
};

template <>
struct allow_enum_bitop<CodeCompletionHandler::CMD_OR_KW_OP> : std::true_type {};

class CodeCompleter {
private:
  CompCandidateConsumer &consumer;
  ObserverPtr<FrontEnd::ModuleProvider> provider;
  const SysConfig &config;
  TypePool &pool;
  const std::string &logicalWorkingDir;
  UserDefinedComp userDefinedComp;
  DynaUdcComp dynaUdcComp;
  ObserverPtr<CancelToken> cancel;

public:
  CodeCompleter(CompCandidateConsumer &consumer, ObserverPtr<FrontEnd::ModuleProvider> provider,
                const SysConfig &config, TypePool &pool, const std::string &workDir)
      : consumer(consumer), provider(provider), config(config), pool(pool),
        logicalWorkingDir(workDir) {}

  void setUserDefinedComp(UserDefinedComp &&comp) { this->userDefinedComp = std::move(comp); }

  void setDynaUdcComp(DynaUdcComp &&comp) { this->dynaUdcComp = std::move(comp); }

  void setCancel(CancelToken &c) { this->cancel = makeObserver(c); }

  /**
   * if module provider is specified, parse 'ref' and complete candidates (except for 'option')
   * otherwise complete candidates corresponding to 'option'
   * @param scope
   * @param scriptName
   * @param ref
   * @param option
   * @return
   * if cancelled (interrupted by signal or has error), return false
   */
  bool operator()(NameScopePtr scope, const std::string &scriptName, StringRef ref,
                  CodeCompOp option);
};

// for error suggestion

/**
 * get similar var name from scope
 * @param name
 * @param scope
 * @param threshold
 * @return
 * if suggestion score (edit distance) is greater than threshold, return empty string
 */
StringRef suggestSimilarVarName(StringRef name, const NameScope &scope, unsigned int threshold = 3);

/**
 * get similar type name from scope and type pool
 * @param name
 * @param pool
 * @param scope
 * @param recvType
 * may be null
 * @param threshold
 * @return
 * if suggestion score (edit distance) is greater than threshold, return empty string
 */
StringRef suggestSimilarType(StringRef name, const TypePool &pool, const NameScope &scope,
                             const DSType *recvType, unsigned int threshold = 3);

enum class SuggestMemberType {
  FIELD = 1u << 0u,
  METHOD = 1u << 1u,
};

template <>
struct allow_enum_bitop<SuggestMemberType> : std::true_type {};

/**
 * get similar member (field/method)
 * @param name
 * @param pool
 * @param scope
 * @param recvType
 * @param targetType
 * @param threshold
 * @return
 * if suggestion score (edit distance) is greater than threshold, return empty string
 */
StringRef suggestSimilarMember(StringRef name, const TypePool &pool, const NameScope &scope,
                               const DSType &recvType, SuggestMemberType targetType,
                               unsigned int threshold = 3);

} // namespace ydsh

#endif // YDSH_COMPLETE_H
