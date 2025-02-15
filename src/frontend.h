/*
 * Copyright (C) 2018-2021 Nagisa Sekiguchi
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

#ifndef ARSH_FRONTEND_H
#define ARSH_FRONTEND_H

#include "parser.h"
#include "type_checker.h"

namespace arsh {

enum class FrontEndOption : unsigned char {
  PARSE_ONLY = 1 << 0,
  TOPLEVEL = 1 << 1,
  ERROR_RECOVERY = 1 << 2,
  SINGLE_EXPR = 1 << 3,
  REPORT_WARN = 1 << 4,
  COLLECT_SIGNATURE = 1u << 5,
  IMPLICIT_BLOCK = 1 << 6,
};

template <>
struct allow_enum_bitop<FrontEndOption> : std::true_type {};

struct FrontEndResult {
  enum Kind : unsigned char {
    IN_MODULE,
    ENTER_MODULE,
    EXIT_MODULE,
    FAILED,
  };

  std::unique_ptr<Node> node;
  Kind kind;

  static FrontEndResult inModule(std::unique_ptr<Node> &&node) {
    return {
        .node = std::move(node),
        .kind = IN_MODULE,
    };
  }

  static FrontEndResult enterModule() {
    return {
        .node = nullptr,
        .kind = ENTER_MODULE,
    };
  }

  static FrontEndResult failed() {
    return {
        .node = nullptr,
        .kind = FAILED,
    };
  }

  explicit operator bool() const { return this->kind != FAILED; }
};

class FrontEnd {
public:
  struct Context {
    LexerPtr lexer;
    std::reference_wrapper<TypePool> pool;
    NameScopePtr scope;
    std::unique_ptr<SourceListNode> srcListNode;
    std::vector<std::unique_ptr<Node>> nodes;
    unsigned int nodeIndex{0};

    Context(TypePool &pool, LexerPtr lexer, NameScopePtr scope)
        : lexer(std::move(lexer)), pool(pool), scope(std::move(scope)) {}
  };

  struct ErrorListener {
    virtual ~ErrorListener() = default;

    virtual bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                  const ParseError &parseError) = 0;

    virtual bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                 const TypeCheckError &checkError, bool firstAppear) = 0;
  };

  struct ModuleProvider {
    virtual ~ModuleProvider() = default;

    virtual std::unique_ptr<Context> newContext(LexerPtr lexer) = 0;

    virtual const ModType &
    newModTypeFromCurContext(const std::vector<std::unique_ptr<Context>> &ctx) = 0;

    using Ret = Union<const ModType *, std::unique_ptr<Context>, ModLoadingError>;

    virtual Ret load(const char *scriptDir, const char *modPath) = 0;

    virtual const SysConfig &getSysConfig() const = 0;

    virtual std::reference_wrapper<const CancelToken> getCancelToken() const = 0;
  };

private:
  TypeChecker checker;
  std::vector<std::unique_ptr<Context>> contexts;
  ModuleProvider &provider;
  const FrontEndOption option;
  bool prevIsNothing{false};
  ObserverPtr<ErrorListener> listener;
  ObserverPtr<NodeDumper> uastDumper;
  ObserverPtr<NodeDumper> astDumper;

public:
  FrontEnd(ModuleProvider &provider, LexerPtr lexer, FrontEndOption option = {},
           ObserverPtr<CodeCompletionContext> compCtx = nullptr)
      : FrontEnd(provider, provider.newContext(std::move(lexer)), option, compCtx) {}

  FrontEnd(ModuleProvider &provider, std::unique_ptr<Context> &&ctx, FrontEndOption option,
           ObserverPtr<CodeCompletionContext> compCtx);

  void setErrorListener(ErrorListener &r) { this->listener.reset(&r); }

  void setUASTDumper(NodeDumper &dumper) {
    assert(dumper);
    this->uastDumper.reset(&dumper);
  }

  void setASTDumper(NodeDumper &dumper) {
    assert(dumper);
    this->astDumper.reset(&dumper);
  }

  void setSignatureHandler(SignatureHandler &&handler) {
    this->checker.setSignatureHandler(std::move(handler));
  }

  unsigned int getMaxLocalVarIndex() const { return this->curScope()->getMaxLocalVarIndex(); }

  ModuleProvider &getModuleProvider() { return this->provider; }

  TypePool &getTypePool() { return this->contexts.back()->pool; }

  const LexerPtr &getCurrentLexer() const { return this->contexts.back()->lexer; }

  const std::vector<std::unique_ptr<Context>> &getContext() const { return this->contexts; }

  unsigned int getRootLineNum() const { return this->contexts[0]->lexer->getMaxLineNum(); }

  const std::unique_ptr<SourceListNode> &getCurSrcListNode() const {
    return this->contexts.back()->srcListNode;
  }

  ModId getCurModId() const { return this->curScope()->modId; }

  bool isPrevTypeNothing() const { return this->prevIsNothing; }

  bool hasUnconsumedPath() const {
    auto &e = this->getCurSrcListNode();
    return e && e->hasUnconsumedPath();
  }

  explicit operator bool() const {
    auto &ctx = this->contexts.back();
    return ctx->nodes.empty() || ctx->nodeIndex < ctx->nodes.size() || this->contexts.size() > 1 ||
           this->hasUnconsumedPath();
  }

  FrontEndResult operator()();

  void setupASTDump();

  void teardownASTDump();

private:
  /**
   *
   * @return
   */
  const char *getCurScriptDir() const { return this->contexts.back()->lexer->getScriptDir(); }

  std::unique_ptr<SourceListNode> &getCurSrcListNode() {
    return this->contexts.back()->srcListNode;
  }

  const NameScopePtr &curScope() const { return this->contexts.back()->scope; }

  /**
   * @return
   * if has error, return false
   */
  bool tryToParse();

  /**
   *
   * @return
   * if reach end, return null
   */
  std::unique_ptr<Node> takeNode();

  bool tryToCheckType(std::unique_ptr<Node> &node);

  FrontEndResult enterModule();

  std::unique_ptr<SourceNode> exitModule();
};

inline void consumeAllInput(FrontEnd &frontEnd) {
  while (frontEnd) {
    if (!frontEnd()) {
      break;
    }
  }
}

class DefaultModuleProvider : public FrontEnd::ModuleProvider {
private:
  ModuleLoader &loader;
  TypePool &pool;
  NameScopePtr scope;
  std::unique_ptr<CancelToken> cancelToken;

public:
  DefaultModuleProvider(ModuleLoader &loader, TypePool &pool, NameScopePtr scope,
                        std::unique_ptr<CancelToken> &&cancelToken)
      : loader(loader), pool(pool), scope(std::move(scope)), cancelToken(std::move(cancelToken)) {}

  ~DefaultModuleProvider() override = default;

  std::unique_ptr<FrontEnd::Context> newContext(LexerPtr lexer) override;

  const ModType &
  newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) override;

  const ModType &newModType(const NameScope &s) {
    return this->loader.createModType(this->pool, s);
  }

  Ret load(const char *scriptDir, const char *modPath) override;

  Ret load(const char *scriptDir, const char *modPath, ModLoadOption loadOption);

  const SysConfig &getSysConfig() const override;

  std::reference_wrapper<const CancelToken> getCancelToken() const override;

  TypePool &getPool() { return this->pool; }

  const NameScopePtr &getScope() const { return this->scope; }

  DiscardPoint getCurrentDiscardPoint() const {
    return DiscardPoint{
        .mod = this->loader.getDiscardPoint(),
        .scope = this->scope->getDiscardPoint(),
        .type = this->pool.getDiscardPoint(),
    };
  }

  void discard(const DiscardPoint &discardPoint) {
    discardAll(this->loader, *this->scope, this->pool, discardPoint);
  }
};

} // namespace arsh

#endif // ARSH_FRONTEND_H
