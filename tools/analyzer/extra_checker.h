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

#ifndef YDSH_TOOLS_ANALYZER_EXTRA_CHECKER_H
#define YDSH_TOOLS_ANALYZER_EXTRA_CHECKER_H

#include <tcerror.h>

#include "pass.h"

namespace ydsh::lsp {

class DiagnosticEmitter;

class ExtraChecker : public NodePass {
private:
  DiagnosticEmitter &emitter;
  std::vector<unsigned int> modIds;

public:
  explicit ExtraChecker(DiagnosticEmitter &emitter) : emitter(emitter) {}

  bool enterModule(unsigned short modId, int version,
                   const std::shared_ptr<TypePool> &pool) override;
  bool exitModule(const std::unique_ptr<Node> &node) override;

private:
  void warnImpl(Token token, const char *kind, const char *fmt, ...)
      __attribute__((format(printf, 4, 5)));

  template <typename T, typename... Arg,
            typename = base_of_t<T, TCErrorDetail<TypeCheckError::Type::WARN>>>
  void warn(Token token, Arg &&...arg) {
    this->warnImpl(token, T::kind, T::value, std::forward<Arg>(arg)...);
  }

protected:
  void visitVarDeclNode(VarDeclNode &node) override;
  void visitTypeDefNode(TypeDefNode &node) override;
};

DEFINE_TCWarn(UnusedLocal, "local variable `%s' is never used");
DEFINE_TCWarn(UnusedTypeAlias, "type alias `%s' is never used");

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_EXTRA_CHECKER_H
