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

#include <constant.h>

#include "lsp.h"
#include "source.h"

namespace ydsh::lsp {

const Source *SourceManager::find(const std::string &path) const {
  auto iter = this->srcMap.find(path);
  if (iter != this->srcMap.end()) {
    return &iter->second;
  }
  return nullptr;
}

const Source *SourceManager::update(const std::string &path, int version, std::string &&content) {
  auto iter = this->srcMap.find(path);
  if (iter != this->srcMap.end()) {
    auto id = iter->second.getSrcId();
    iter->second = Source(id, std::move(content), version);
    return &iter->second;
  }
  unsigned int id = this->srcMap.size() + 1;
  if (id == SYS_LIMIT_MOD_ID) {
    return nullptr;
  }
  return &(this->srcMap[path] =
               Source(static_cast<unsigned short>(id), std::move(content), version));
}

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change) {
  if (!change.range.hasValue()) {
    content = change.text;
    return true;
  } // FIXME: support incremental update
  return true;
}

} // namespace ydsh::lsp