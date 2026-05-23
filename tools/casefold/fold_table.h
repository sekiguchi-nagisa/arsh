/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_CASEFOLD_FOLD_TABLE_H
#define ARSH_TOOLS_CASEFOLD_FOLD_TABLE_H

#include <array>
#include <vector>

#include <misc/buffer.hpp>

namespace arsh::fold {

// for simple case folding
class FoldTable {
public:
  static constexpr unsigned int SHIFT = 5;
  static constexpr unsigned int BLOCK_SIZE = 1 << SHIFT;

  struct Block {
    std::array<uint32_t, BLOCK_SIZE> values{};

    Block() = default;

    bool operator==(const Block &o) const {
      return this->values.size() == o.values.size() &&
             std::equal(this->values.begin(), this->values.end(), o.values.begin());
    }
  };

private:
  std::vector<uint8_t> blockIndexes;
  std::vector<Block> blocks;
  int maxFoldCodePoint;

  FoldTable(std::vector<uint8_t> &&blockIndexes, std::vector<Block> &&blocks, int maxFoldCodePoint)
      : blockIndexes(std::move(blockIndexes)), blocks(std::move(blocks)),
        maxFoldCodePoint(maxFoldCodePoint) {}

public:
  static FoldTable create();

  const auto &getBlockIndexes() const { return this->blockIndexes; }

  const auto &getBlocks() const { return this->blocks; }

  int getMaxFoldCodePoint() const { return this->maxFoldCodePoint; }

  std::pair<int, char> fold(int codePoint) const;
};

} // namespace arsh::fold

#endif // ARSH_TOOLS_CASEFOLD_FOLD_TABLE_H
