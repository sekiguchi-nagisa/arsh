/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef ARSH_COMP_CONTEXT_H
#define ARSH_COMP_CONTEXT_H

#include "node.h"
#include "scope.h"

namespace arsh {

enum class CodeCompOp : unsigned int {
  FILE = 1u << 0u,        /* complete file names (including directory) */
  DIR = 1u << 1u,         /* complete directory names (directory only) */
  EXEC = 1u << 2u,        /* complete executable file names (including directory) */
  TILDE = 1u << 3u,       /* perform tilde expansion before completions */
  EXTERNAL = 1u << 4u,    /* complete external command names */
  DYNA_UDC = 1u << 5u,    /* complete dynamically registered command names */
  BUILTIN = 1u << 6u,     /* complete builtin command names */
  UDC = 1u << 7u,         /* complete user-defined command names */
  VAR = 1u << 8u,         /* complete global variable names (not start with $) */
  ENV = 1u << 9u,         /* complete environmental variable names */
  VALID_ENV = 1u << 10u,  /* complete environmental variable names (valid name only) */
  SIGNAL = 1u << 11u,     /* complete signal names (not start with SIG) */
  USER = 1u << 12u,       /* complete usernames */
  GROUP = 1u << 13u,      /* complete group names */
  MODULE = 1u << 14u,     /* complete module path */
  STMT_KW = 1u << 15u,    /* complete statement keyword */
  EXPR_KW = 1u << 16u,    /* complete expr keyword */
  EXPECT = 1u << 17u,     /* complete expected token */
  MEMBER = 1u << 18u,     /* complete member (field/method) */
  TYPE = 1u << 19u,       /* complete type name */
  CMD_ARG = 1u << 20u,    /* for command argument */
  HOOK = 1u << 21u,       /* for user-defined completion hook */
  ATTR = 1u << 22u,       /* complete attribute */
  ATTR_PARAM = 1u << 23u, /* complete attribute parameter */
  PARAM = 1u << 24u,      /* complete parameter name */
  COMMAND = EXTERNAL | DYNA_UDC | BUILTIN | UDC,
};

template <>
struct allow_enum_bitop<CodeCompOp> : std::true_type {};

inline bool willKickFrontEnd(CodeCompOp op) { return empty(op); }

inline bool isKeyword(StringRef value) { return !value.startsWith("<") || !value.endsWith(">"); }

class Lexer;

class CodeCompletionContext {
private:
  ObserverPtr<const Lexer> lex;

  const std::string &scriptDir; // for module completion

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

  /**
   * for attribute parameter completion
   */
  AttributeParamSet targetAttrParams;

public:
  CodeCompletionContext(NameScopePtr scope, const std::string &scriptDir)
      : scriptDir(scriptDir), scope(std::move(scope)) {}

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

  void addTypeNameRequest(std::string &&value, const DSType *type, NameScopePtr curScope) {
    this->scope = std::move(curScope);
    this->recvType = type;
    this->addCompRequest(CodeCompOp::TYPE, std::move(value));
  }

  void addMemberRequest(const DSType &type, std::string &&value) {
    this->compOp = CodeCompOp::MEMBER;
    this->recvType = &type;
    this->compWord = std::move(value);
  }

  void addAttrParamRequest(std::string &&value, AttributeParamSet paramSet) {
    this->targetAttrParams = paramSet;
    this->addCompRequest(CodeCompOp::ATTR_PARAM, std::move(value));
  }

  struct CmdOrKeywordParam {
    bool stmt{false};
    bool tilde{false};
  };

  void addCmdOrKeywordRequest(std::string &&value, const CmdOrKeywordParam param) {
    // add command request
    bool isDir = strchr(value.c_str(), '/') != nullptr;
    if (param.tilde || isDir) {
      CodeCompOp op = CodeCompOp::EXEC;
      if (param.tilde) {
        setFlag(op, CodeCompOp::TILDE);
      }
      this->addCompRequest(op, std::move(value));
    } else {
      this->addCompRequest(CodeCompOp::COMMAND, std::move(value));
    }

    // add keyword request
    setFlag(this->compOp, param.stmt ? CodeCompOp::STMT_KW : CodeCompOp::EXPR_KW);
  }

  void addCompHookRequest(const Lexer &lexer, std::unique_ptr<CmdNode> &&node) {
    this->lex = makeObserver(lexer);
    this->fallbackOp = this->compOp;
    this->compOp = CodeCompOp::HOOK;
    this->cmdNode = std::move(node);
  }

  bool hasCompRequest() const { return !empty(this->compOp); }

  CodeCompOp getCompOp() const { return this->compOp; }

  bool has(CodeCompOp op) const { return hasFlag(this->compOp, op); }

  const auto &getCompWord() const { return this->compWord; }

  unsigned int getCompWordOffset() const { return this->compWordOffset; }

  const auto &getScope() const { return *this->scope; }

  CodeCompOp getFallbackOp() const { return this->fallbackOp; }

  const auto &getCmdNode() const { return this->cmdNode; }

  AttributeParamSet getTargetAttrParams() const { return this->targetAttrParams; }

  const auto &getExtraWords() const { return this->extraWords; }

  const Lexer *getLexer() const { return this->lex.get(); }

  const DSType *getRecvType() const { return this->recvType; }

  const auto &getScriptDir() const { return this->scriptDir; }
};

} // namespace arsh

#endif // ARSH_COMP_CONTEXT_H
