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

#ifndef ARSH_REGEX_CAPTURE_H
#define ARSH_REGEX_CAPTURE_H

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "misc/array_ref.hpp"
#include "misc/buffer.hpp"
#include "misc/noncopyable.h"
#include "misc/string_ref.hpp"

namespace arsh::regex {

class NamedCaptureEntry {
private:
  bool multiple{false};
  unsigned int sizeOrIndex{0};    // 1-based-index
  unsigned int *indices{nullptr}; // 1-based-index

public:
  static constexpr unsigned int MAX_INDEX = UINT16_MAX;

  NON_COPYABLE(NamedCaptureEntry);

  NamedCaptureEntry() = default;

  explicit NamedCaptureEntry(FlexBuffer<unsigned int> &&indices)
      : multiple(true), sizeOrIndex(indices.size()), indices(indices.take()) {}

  explicit NamedCaptureEntry(unsigned int index) : sizeOrIndex(index) {}

  NamedCaptureEntry(NamedCaptureEntry &&o) noexcept
      : multiple(o.multiple), sizeOrIndex(o.sizeOrIndex), indices(o.indices) {
    o.multiple = false;
    o.indices = nullptr;
  }

  ~NamedCaptureEntry() {
    if (this->multiple) {
      free(this->indices);
    }
  }

  NamedCaptureEntry &operator=(NamedCaptureEntry &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~NamedCaptureEntry();
      new (this) NamedCaptureEntry(std::move(o));
    }
    return *this;
  }

  bool hasMultipleIndices() const { return this->multiple; }

  unsigned int getIndex() const { return this->sizeOrIndex; }

  unsigned int getSize() const { return this->hasMultipleIndices() ? this->sizeOrIndex : 0; }

  unsigned int operator[](const unsigned int i) const { return this->indices[i]; }
};

class NamedCaptureGroups {
public:
  using Entry = std::pair<std::string, NamedCaptureEntry>;

private:
  StrRefMap<unsigned short> offsetMap; // name to entry offset
  std::vector<Entry> entries;

public:
  NamedCaptureGroups(StrRefMap<unsigned short> &&offsetMap, std::vector<Entry> &&entries)
      : offsetMap(std::move(offsetMap)), entries(std::move(entries)) {}

  const auto &getEntries() const { return this->entries; }

  const auto &operator[](unsigned int offset) const { return this->entries[offset]; }

  const NamedCaptureEntry *find(StringRef name) const {
    if (auto iter = this->offsetMap.find(name); iter != this->offsetMap.end()) {
      return &this->entries[iter->second].second;
    }
    return nullptr;
  }

  ArrayRef<Entry> toArrayRef() const { return {this->entries.data(), this->entries.size()}; }
};

struct Capture {
  uint32_t offset{UINT32_MAX};
  uint32_t size{UINT32_MAX};

  explicit operator bool() const { return this->offset != UINT32_MAX && this->size != UINT32_MAX; }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_CAPTURE_H
