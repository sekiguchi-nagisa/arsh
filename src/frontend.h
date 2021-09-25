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

#ifndef YDSH_FRONTEND_H
#define YDSH_FRONTEND_H

#include "parser.h"
#include "type_checker.h"

namespace ydsh {

enum class FrontEndOption {
  PARSE_ONLY = 1 << 0,
  TOPLEVEL = 1 << 1,
  ERROR_RECOVERY = 1 << 2,
};

template <>
struct allow_enum_bitop<FrontEndOption> : std::true_type {};

struct FrontEndResult {
  enum Kind {
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
    Lexer lexer;
    Parser parser;
    TypeChecker checker;
    IntrusivePtr<NameScope> scope;
    std::unique_ptr<SourceListNode> srcListNode;

    Context(TypePool &pool, Lexer &&lexer, IntrusivePtr<NameScope> scope, FrontEndOption option,
            ObserverPtr<CodeCompletionHandler> ccHandler = nullptr)
        : lexer(std::move(lexer)), parser(this->lexer, ccHandler),
          checker(pool, hasFlag(option, FrontEndOption::TOPLEVEL), &this->lexer),
          scope(std::move(scope)) {
      this->checker.setCodeCompletionHandler(ccHandler);
    }
  };

  struct ModuleProvider {
    virtual ~ModuleProvider() = default;

    virtual std::unique_ptr<Context> newContext(Lexer &&lexer, FrontEndOption option,
                                                ObserverPtr<CodeCompletionHandler> ccHandler) = 0;

    virtual const ModType &
    newModTypeFromCurContext(const std::vector<std::unique_ptr<Context>> &ctx) = 0;

    using Ret = Union<const ModType *, std::unique_ptr<Context>, ModLoadingError>;

    virtual Ret load(const char *scriptDir, const char *modPath, FrontEndOption option) = 0;
  };

  struct ErrorListener {
    virtual ~ErrorListener() = default;

    virtual bool handleParseError(const std::vector<std::unique_ptr<Context>> &ctx,
                                  const ParseError &parseError) = 0;

    virtual bool handleTypeError(const std::vector<std::unique_ptr<Context>> &ctx,
                                 const TypeCheckError &checkError) = 0;
  };

private:
  std::vector<std::unique_ptr<Context>> contexts;
  ModuleProvider &provider;
  const FrontEndOption option;
  const DSType *prevType{nullptr};
  ObserverPtr<ErrorListener> listener;
  ObserverPtr<NodeDumper> uastDumper;
  ObserverPtr<NodeDumper> astDumper;

public:
  FrontEnd(ModuleProvider &provider, Lexer &&lexer, FrontEndOption option = {},
           ObserverPtr<CodeCompletionHandler> ccHandler = nullptr);

  void setErrorListener(ErrorListener &r) { this->listener.reset(&r); }

  void setUASTDumper(NodeDumper &dumper) {
    assert(dumper);
    this->uastDumper.reset(&dumper);
  }

  void setASTDumper(NodeDumper &dumper) {
    assert(dumper);
    this->astDumper.reset(&dumper);
  }

  unsigned int getMaxLocalVarIndex() const { return this->curScope()->getMaxLocalVarIndex(); }

  TypePool &getTypePool() { return this->checker().getTypePool(); }

  const Lexer &getCurrentLexer() const { return this->contexts.back()->lexer; }

  const std::vector<std::unique_ptr<Context>> &getContext() const { return this->contexts; }

  unsigned int getRootLineNum() const { return this->contexts[0]->lexer.getMaxLineNum(); }

  const std::unique_ptr<SourceListNode> &getCurSrcListNode() const {
    return this->contexts.back()->srcListNode;
  }

  unsigned short getCurModId() const { return this->curScope()->modId; }

  bool hasUnconsumedPath() const {
    auto &e = this->getCurSrcListNode();
    return e && e->hasUnconsumedPath();
  }

  explicit operator bool() const {
    return static_cast<bool>(this->parser()) || this->contexts.size() > 1 ||
           this->hasUnconsumedPath();
  }

  FrontEndResult operator()();

  void setupASTDump();

  void teardownASTDump();

private:
  Parser &parser() { return this->contexts.back()->parser; }

  const Parser &parser() const { return this->contexts.back()->parser; }

  TypeChecker &checker() { return this->contexts.back()->checker; }

  /**
   *
   * @return
   */
  const char *getCurScriptDir() const { return this->contexts.back()->lexer.getScriptDir(); }

  std::unique_ptr<SourceListNode> &getCurSrcListNode() {
    return this->contexts.back()->srcListNode;
  }

  const IntrusivePtr<NameScope> &curScope() const { return this->contexts.back()->scope; }

  std::unique_ptr<Node> tryToParse();

  bool tryToCheckType(std::unique_ptr<Node> &node);

  FrontEndResult enterModule();

  std::unique_ptr<SourceNode> exitModule();
};

class DefaultModuleProvider : public FrontEnd::ModuleProvider {
private:
  ModuleLoader &loader;
  TypePool &pool;
  IntrusivePtr<NameScope> scope;

public:
  DefaultModuleProvider(ModuleLoader &loader, TypePool &pool, IntrusivePtr<NameScope> scope)
      : loader(loader), pool(pool), scope(std::move(scope)) {}

  ~DefaultModuleProvider() override = default;

  std::unique_ptr<FrontEnd::Context>
  newContext(Lexer &&lexer, FrontEndOption option,
             ObserverPtr<CodeCompletionHandler> ccHandler) override;

  const ModType &
  newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) override;

  Ret load(const char *scriptDir, const char *modPath, FrontEndOption option) override;

  void discard(const DiscardPoint &discardPoint) {
    discardAll(this->loader, *this->scope, this->pool, discardPoint);
  }
};

} // namespace ydsh

#endif // YDSH_FRONTEND_H
