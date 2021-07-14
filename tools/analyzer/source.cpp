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

const Source *SourceManager::find(StringRef path) const {
  auto iter = this->indexMap.find(path);
  if (iter != this->indexMap.end()) {
    return &this->sources[iter->second];
  }
  return nullptr;
}

const Source *SourceManager::update(StringRef path, int version, std::string &&content) {
  auto iter = this->indexMap.find(path);
  if (iter != this->indexMap.end()) {
    unsigned int i = iter->second;
    this->sources[i].update(std::move(content), version);
    return &this->sources[i];
  } else {
    unsigned int id = this->sources.size() + 1;
    if (id == SYS_LIMIT_MOD_ID) {
      return nullptr;
    }
    unsigned int i = this->sources.size();
    auto ptr = CStrPtr(strdup(path.data()));
    this->sources.emplace_back(std::move(ptr), static_cast<unsigned short>(id), std::move(content),
                               version);
    path = this->sources[i].getPath();
    this->indexMap.emplace(path, i);
    return &this->sources[i];
  }
}

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change) {
  if (!change.range.hasValue()) {
    content = change.text;
    return true;
  } // FIXME: support incremental update
  return true;
}

} // namespace ydsh::lsp