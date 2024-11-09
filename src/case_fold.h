/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#ifndef ARSH_CASE_FOLD_H
#define ARSH_CASE_FOLD_H

#include <array>

#include "misc/flag_util.hpp"

namespace arsh {

enum class CaseFoldOp : unsigned char {
  FULL_FOLD = 1u << 0u,
  TURKIC = 1u << 1u,
};

template <>
struct allow_enum_bitop<CaseFoldOp> : std::true_type {};

class CaseFoldingResult {
public:
  static constexpr size_t FULL_FOLD_ENTRY_SIZE = 3;
  using FullFoldingEntry = std::array<uint16_t, FULL_FOLD_ENTRY_SIZE>;

private:
  bool full;
  union {
    FullFoldingEntry fullFolding;
    int simpleFolding;
  } data;

public:
  explicit CaseFoldingResult(int code) : full(false) { this->data.simpleFolding = code; }

  explicit CaseFoldingResult(const FullFoldingEntry &e) : full(true) { this->data.fullFolding = e; }

  bool isFullFolding() const { return this->full; }

  int getSimpleFolding() const { return this->data.simpleFolding; }

  const FullFoldingEntry &getFullFolding() const { return this->data.fullFolding; }

  bool equals(int codePoint) const {
    return !this->isFullFolding() && this->getSimpleFolding() == codePoint;
  }
};

CaseFoldingResult doCaseFolding(int codePoint, CaseFoldOp op);

} // namespace arsh

#endif // ARSH_CASE_FOLD_H
