/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_ANALYZER_ANALYZER_H
#define YDSH_TOOLS_ANALYZER_ANALYZER_H

#include <frontend.h>

#include "ast.h"

namespace ydsh::lsp {

class ASTContextProvider : public FrontEnd::ModuleProvider, public ModuleLoaderBase {
private:
  StrRefMap<ASTContextPtr> ctxMap; // fullpath to ASTContext mapping

public:
  ~ASTContextProvider() override = default;

  std::unique_ptr<FrontEnd::Context>
  newContext(Lexer &&lexer, FrontEndOption option,
             ObserverPtr<CodeCompletionHandler> ccHandler) override;

  const ModType &
  newModTypeFromCurContext(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx) override;

  Ret load(const char *scriptDir, const char *modPath, FrontEndOption option) override;

  ASTContextPtr find(StringRef ref) const;

  ASTContextPtr find(const uri::URI &uri) const { return this->find(uri.getPath()); }

  ASTContextPtr addNew(const uri::URI &uri, std::string &&content, int version);

private:
  ModResult addNewModEntry(CStrPtr &&ptr) override;
};

class DiagnosticEmitter : public FrontEnd::ErrorListener {
public:
  ~DiagnosticEmitter() override = default;

  bool handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                        const ParseError &parseError) override;
  bool handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                       const TypeCheckError &checkError) override;
};

ASTContextPtr buildAST(ASTContextProvider &provider, DiagnosticEmitter &emitter, ASTContextPtr ctx);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_ANALYZER_H
