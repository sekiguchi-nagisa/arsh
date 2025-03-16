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

#include <misc/enum_util.hpp>

#include "../json/serialize.h"

#include "registration.h"

namespace arsh::lsp {

static const char *toString(RegistrationMap::Capability capability) {
  constexpr const char *table[] = {
#define GEN_TABLE(E, S) S,
      EACH_REGISTRATION_CAPABILITY(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[toUnderlying(capability)];
}

bool RegistrationMap::registerCapability(Capability capability, const std::string &id) {
  unsigned int index = toUnderlying(capability);
  if (index < this->ids.size()) {
    if (this->ids[index].empty()) {
      this->ids[index] = id;
      return true;
    }
  }
  return false;
}

Registration RegistrationMap::registerSemanticTokensCapability(std::string &&id,
                                                               const SemanticTokensLegend &legend) {
  if (!this->registerCapability(Capability::SEMANTIC_TOKENS, id)) {
    return {};
  }
  auto options = SemanticTokensRegistrationOptions::createDynamic(legend);
  JSONSerializer serializer;
  serializer(options);
  return {
      .id = std::move(id),
      .method = toString(Capability::SEMANTIC_TOKENS),
      .registerOptions = std::move(serializer).take(),
  };
}

Unregistration RegistrationMap::unregisterCapability(Capability capability) {
  Unregistration unregistration;
  unsigned int index = toUnderlying(capability);
  if (index < this->ids.size() && !this->ids[index].empty()) {
    std::string tmp;
    std::swap(tmp, this->ids[index]);
    unregistration.id = std::move(tmp);
    unregistration.method = toString(capability);
  }
  return unregistration;
}

} // namespace arsh::lsp