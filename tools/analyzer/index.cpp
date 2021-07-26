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

#include "index.h"

namespace ydsh::lsp {

// #########################
// ##     ModuleIndex     ##
// #########################

static ModuleIndexPtr createNull() {
  return ModuleIndex::create(0, 0, nullptr, std::vector<std::unique_ptr<Node>>(),
                             ModuleArchive({}), std::vector<std::pair<bool, ModuleIndexPtr>>());
}

const ModuleIndexPtr ModuleIndex::NULL_INDEX = createNull();

static void tryInsertByAscendingOrder(std::vector<ModuleIndexPtr> &targets,
                                      const ModuleIndexPtr &index) {
  assert(index);
  auto iter = std::lower_bound(targets.begin(), targets.end(), index,
                               [](const ModuleIndexPtr &x, const ModuleIndexPtr &y) {
                                 return x->getModId() < y->getModId();
                               });
  if (iter == targets.end() || (*iter)->getModId() != index->getModId()) {
    targets.insert(iter, index);
  }
}

static void resolveTargets(std::vector<ModuleIndexPtr> &targets, const ModuleIndexPtr &index) {
  tryInsertByAscendingOrder(targets, index);
  for (auto &e : index->getImportedIndexes()) {
    resolveTargets(targets, e.second);
  }
}

static void visit(std::vector<ModuleIndexPtr> &ret, std::vector<bool> &used,
                  const ModuleIndexPtr &index) {
  if (used[index->getModId()]) {
    return;
  }
  used[index->getModId()] = true;
  for (auto &e : index->getImportedIndexes()) {
    visit(ret, used, e.second);
  }
  ret.push_back(index);
}

static std::vector<ModuleIndexPtr> topologcalSort(const std::vector<ModuleIndexPtr> &targets) {
  std::vector<ModuleIndexPtr> ret;
  if (targets.empty()) {
    return ret;
  }
  std::vector<bool> used(targets.back()->getModId() + 1, false);
  for (auto &e : targets) {
    visit(ret, used, e);
  }
  return ret;
}

std::vector<ModuleIndexPtr> ModuleIndex::getDepsByTopologicalOrder() const {
  std::vector<ModuleIndexPtr> targets;
  for (auto &e : this->imported) {
    resolveTargets(targets, e.second);
  }
  return topologcalSort(targets);
}

} // namespace ydsh::lsp