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

#ifndef YDSH_TOOLS_ANALYZER_INDEX_H
#define YDSH_TOOLS_ANALYZER_INDEX_H

#include <node.h>
#include <scope.h>
#include <type_pool.h>

#include "archive.h"

namespace ydsh::lsp {

class ModuleIndex;

using ModuleIndexPtr = std::shared_ptr<ModuleIndex>;

class ModuleIndex { // FIXME: indexed symbols
private:
  const unsigned short modId;
  const int version;
  std::unique_ptr<TypePool> pool;
  std::vector<std::unique_ptr<Node>> nodes;
  ModuleArchive archive;
  std::vector<std::pair<bool, ModuleIndexPtr>> imported;

public:
  static const ModuleIndexPtr NULL_INDEX; // dummy object for null entry

  ModuleIndex(unsigned short modId, int version, std::unique_ptr<TypePool> &&pool,
              std::vector<std::unique_ptr<Node>> &&nodes, ModuleArchive &&archive,
              std::vector<std::pair<bool, ModuleIndexPtr>> &&dependencies)
      : modId(modId), version(version), pool(std::move(pool)), nodes(std::move(nodes)),
        archive(std::move(archive)), imported(std::move(dependencies)) {}

  template <typename... Args>
  static ModuleIndexPtr create(Args &&...args) {
    return std::make_shared<ModuleIndex>(std::forward<Args>(args)...);
  }

  int getVersion() const { return this->version; }

  const TypePool &getPool() const { return *this->pool; }

  const auto &getNodes() const { return this->nodes; }

  const ModuleArchive &getArchive() const { return this->archive; }

  const auto &getImportedIndexes() const { return this->imported; }

  unsigned short getModId() const { return this->modId; }

  bool isNullIndex() const { return this->getModId() == 0; }

  std::vector<ModuleIndexPtr> getDepsByTopologicalOrder() const;
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
