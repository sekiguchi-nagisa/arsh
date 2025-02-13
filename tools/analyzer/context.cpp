/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#include "context.h"

namespace arsh::lsp {

// ############################
// ##     ContextManager     ##
// ############################

std::shared_ptr<Context> ContextManager::addNew(JSON id) {
  auto ctx = std::make_shared<Context>(std::move(id));
  this->ring.emplace_back(ctx);
  return ctx;
}

static bool equals(const JSON &x, const JSON &y) {
  if (x.isLong() && y.isLong()) {
    return x.asLong() == y.asLong();
  }
  if (x.isString() && y.isString()) {
    return x.asString() == y.asString();
  }
  return false;
}

void ContextManager::cancel(const JSON &id) const {
  const unsigned int size = this->ring.size();
  for (unsigned int i = 0; i < size; i++) {
    auto &e = this->ring[i];
    if (equals(e->getId(), id)) {
      e->getCancelPoint()->cancel();
    }
  }
}

std::string toStringCancelId(const JSON &id) {
  std::string value;
  if (id.isString()) {
    value = id.asString();
  } else {
    assert(id.isLong());
    value = std::to_string(id.asLong());
  }
  return value;
}

JSON cancelIdToJSON(const Union<int, std::string> &id) {
  JSON json;
  if (is<int>(id)) {
    json = get<int>(id);
  } else {
    assert(is<std::string>(id));
    json = get<std::string>(id);
  }
  return json;
}

} // namespace arsh::lsp