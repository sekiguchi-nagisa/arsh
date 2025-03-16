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

#ifndef ARSH_TOOLS_ANALYZER_REGISTRATION_H
#define ARSH_TOOLS_ANALYZER_REGISTRATION_H

#include <string>

#include "lsp.h"

namespace arsh::lsp {

#define EACH_REGISTRATION_CAPABILITY(OP) OP(SEMANTIC_TOKENS, "textDocument/semanticTokens")

class RegistrationMap {
public:
  enum class Capability : unsigned char {
#define GEN_ENUM(E, S) E,
    EACH_REGISTRATION_CAPABILITY(GEN_ENUM)
#undef GEN_ENUM
  };

  static constexpr unsigned int sizeOfCapability() {
    constexpr Capability table[] = {
#define GEN_TABLE(E, S) Capability::E,
        EACH_REGISTRATION_CAPABILITY(GEN_TABLE)
#undef GEN_TABLE
    };
    return std::size(table);
  }

private:
  std::vector<std::string> ids;

public:
  RegistrationMap() { this->ids.resize(sizeOfCapability()); }

  bool registerCapability(const SemanticTokensRegistrationOptions &options) {
    if (options.id.hasValue()) {
      return this->registerCapability(Capability::SEMANTIC_TOKENS, options.id.unwrap());
    }
    return false;
  }

  /**
   *
   * @param id
   * @param legend
   * @return if already registered, return empty struct (id is empty)
   */
  Registration registerSemanticTokensCapability(std::string &&id,
                                                const SemanticTokensLegend &legend);

  /**
   * @param capability
   * @return
   * if not found, return empty struct (id is empty)
   */
  Unregistration unregisterCapability(Capability capability);

private:
  bool registerCapability(Capability capability, const std::string &id);
};

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_REGISTRATION_H
