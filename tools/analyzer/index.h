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
#include "source.h"
#include "symbol.h"

namespace ydsh::lsp {

class ModuleIndex;

using ModuleIndexPtr = std::shared_ptr<ModuleIndex>;

class ModuleIndex {
private:
  const unsigned short modId;
  const int version;
  ModuleArchive archive;
  std::vector<std::pair<bool, ModuleIndexPtr>> imported;

public:
  static const ModuleIndexPtr NULL_INDEX; // dummy object for null entry

  ModuleIndex(unsigned short modId, int version, ModuleArchive &&archive,
              std::vector<std::pair<bool, ModuleIndexPtr>> &&dependencies)
      : modId(modId), version(version), archive(std::move(archive)),
        imported(std::move(dependencies)) {}

  template <typename... Args>
  static ModuleIndexPtr create(Args &&...args) {
    return std::make_shared<ModuleIndex>(std::forward<Args>(args)...);
  }

  int getVersion() const { return this->version; }

  const ModuleArchive &getArchive() const { return this->archive; }

  const auto &getImportedIndexes() const { return this->imported; }

  unsigned short getModId() const { return this->modId; }

  bool isNullIndex() const { return this->getModId() == 0; }

  std::vector<ModuleIndexPtr> getDepsByTopologicalOrder() const;
};

class IndexMap {
private:
  StrRefMap<ModuleIndexPtr> map;

public:
  ModuleIndexPtr find(const Source &src) const {
    auto iter = this->map.find(src.getPath());
    return iter != this->map.end() ? iter->second : nullptr;
  }

  void add(const Source &src, ModuleIndexPtr index) {
    assert(index);
    assert(index->isNullIndex() || src.getSrcId() == index->getModId());
    this->map[src.getPath()] = std::move(index);
  }

  size_t size() const { return this->map.size(); }

  void revert(std::unordered_set<unsigned short> &&revertingModIdSet);

  /**
   * revert sepcified index if unused (not imported from other indexes)
   * @param id
   * @return
   * if unused, return true
   */
  bool revertIfUnused(unsigned short id);
};

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_INDEX_H
