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

#include "parser.h"
#include "scope.h"
#include "type_pool.h"

namespace ydsh {

enum class CodeCompOp : unsigned int {
  FILE = 1u << 0u,      /* complete file names (including directory) */
  DIR = 1u << 1u,       /* complete directory names (directory only) */
  EXEC = 1u << 2u,      /* complete executable file names (including directory) */
  TILDE = 1u << 3u,     /* perform tilde expansion before completions */
  EXTERNAL = 1u << 4u,  /* complete external command names */
  BUILTIN = 1u << 5u,   /* complete builtin command names */
  UDC = 1u << 6u,       /* complete user-defined command names */
  VAR = 1u << 7u,       /* complete global variable names (not start with $) */
  ENV = 1u << 8u,       /* complete environmental variable names */
  SIGNAL = 1u << 9u,    /* complete signal names (not start with SIG) */
  USER = 1u << 10u,     /* complete usernames */
  GROUP = 1u << 11u,    /* complete group names */
  MODULE = 1u << 12u,   /* complete module path */
  STMT_KW = 1u << 13u,  /* complete statement keyword */
  EXPR_KW = 1u << 14u,  /* complete expr keyword */
  NO_IDENT = 1u << 15u, /* ignore completion candicates starting wiht identifier */
  EXPECT = 1u << 16u,   /* complete expetced token */
  MEMBER = 1u << 17u,   /* complete member (field/method) */
  TYPE = 1u << 18u,     /* complete type name */
  HOOK = 1u << 19u,     /* for user-defined completion hook */
  COMMAND = EXTERNAL | BUILTIN | UDC,
};

template <>
struct allow_enum_bitop<CodeCompOp> : std::true_type {};

inline bool isKeyword(StringRef value) { return !value.startsWith("<") || !value.endsWith(">"); }

enum class CompEscapOp {
  NOP,
  COMMAND_NAME,
  COMMAND_NAME_PART,
  COMMAND_ARG,
};

class CompletionConsumer {
public:
  virtual ~CompletionConsumer() = default;

  void operator()(StringRef ref, CompEscapOp op);

private:
  virtual void consume(std::string &&) = 0;
};

using UserDefinedComp = std::function<bool(const Lexer &lex, const CmdNode &cmdNode,
                                           const std::string &word, CompletionConsumer &consumer)>;

class CodeCompletionHandler {
private:
  ObserverPtr<const UserDefinedComp> userDefinedComp;

  const TypePool &pool;

  const std::string &logicalWorkdir;

  ObserverPtr<const Lexer> lex;

  /**
   * if empty, use cwd
   */
  std::string scriptDir; // for module completion

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
   * for var name compeltion
   */
  NameScopePtr scope;

  /**
   * for member completion
   */
  const DSType *recvType{nullptr};

  CodeCompOp compOp{};

  /**
   * whem result of COMP_HOOK is empty, fallback to file name completion
   */
  CodeCompOp fallbackOp{};

public:
  CodeCompletionHandler(const TypePool &pool, const std::string &logicalWorkdir, NameScopePtr scope)
      : pool(pool), logicalWorkdir(logicalWorkdir), scope(std::move(scope)) {}

  void addCompRequest(CodeCompOp op, std::string &&word) {
    this->compOp = op;
    this->compWord = std::move(word);
  }

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

  void addVarNameRequest(std::string &&value, NameScopePtr curScope);

  void addTypeNameRequest(std::string &&value, const DSType *type, NameScopePtr curScope);

  void addMemberRequest(const DSType &type, std::string &&value) {
    this->compOp = CodeCompOp::MEMBER;
    this->recvType = &type;
    this->compWord = std::move(value);
  }

  enum class CMD_OR_KW_OP {
    STMT = 1u << 0u,
    TILDE = 1u << 1u,
    NO_IDENT = 1u << 2u,
  };

  void addCmdOrKeywordRequest(std::string &&value, CMD_OR_KW_OP op);

  void addCmdArgOrModRequest(std::string &&value, CmdArgParseOpt opt, bool tilde);

  void addCompHookRequest(const Lexer &lexer, std::unique_ptr<CmdNode> &&node) {
    this->lex.reset(&lexer);
    this->fallbackOp = this->compOp;
    this->compOp = CodeCompOp::HOOK;
    this->cmdNode = std::move(node);
  }

  bool hasCompRequest() const { return !empty(this->compOp); }

  CodeCompOp getCompOp() const { return this->compOp; }

  void setUserDefinedComp(const UserDefinedComp &comp) { this->userDefinedComp.reset(&comp); }

  void invoke(CompletionConsumer &results);
};

template <>
struct allow_enum_bitop<CodeCompletionHandler::CMD_OR_KW_OP> : std::true_type {};

class CodeCompleter {
private:
  CompletionConsumer &consumer;
  ModuleLoader &loader;
  TypePool &pool;
  NameScopePtr rootScope;
  const DiscardPoint discardPoint;
  const std::string &logicalWorkingDir;
  UserDefinedComp userDefinedComp;

public:
  CodeCompleter(CompletionConsumer &consumer, ModuleLoader &loader, TypePool &pool,
                NameScopePtr scope, const std::string &workDir)
      : consumer(consumer), loader(loader), pool(pool), rootScope(std::move(scope)),
        discardPoint({
            .mod = this->loader.getDiscardPoint(),
            .scope = this->rootScope->getDiscardPoint(),
            .type = this->pool.getDiscardPoint(),
        }),
        logicalWorkingDir(workDir) {}

  ~CodeCompleter() { discardAll(this->loader, *this->rootScope, this->pool, this->discardPoint); }

  void setUserDefinedComp(UserDefinedComp &&comp) { this->userDefinedComp = std::move(comp); }

  void operator()(const ModType *underlyingModType, StringRef ref, CodeCompOp option = {});
};

} // namespace ydsh

#endif // YDSH_COMPLETE_H
