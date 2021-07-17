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

#ifndef YDSH_TOOLS_ANALYZER_ARCHIVE_H
#define YDSH_TOOLS_ANALYZER_ARCHIVE_H

#include <string>

#include <misc/result.hpp>
#include <type.h>

namespace ydsh::lsp {

enum class ArchiveType : uint8_t {
  PREDEFINED,
  ARRAY,
  MAP,
  TUPLE,
  OPTION,
  FUNC,
  MOD,
};

class Archiver {
private:
  const TypePool &pool;
  const unsigned int builtinTypeIdCount;
  std::string data;

public:
  Archiver(const TypePool &pool, unsigned int idCount) : pool(pool), builtinTypeIdCount(idCount) {}

  void add(const DSType &type);

  void add(const FieldHandle &handle);

  std::string take() && { return std::move(this->data); }

  const std::string &get() const { return this->data; }

private:
  template <unsigned int N>
  void writeN(uint64_t b) {
    static_assert(N > 0 && N < 9, "out of range");
    for (unsigned int i = N; i > 0; --i) {
      const uint64_t shift = (i - 1) * 8;
      const uint64_t mask = static_cast<uint64_t>(0xFF) << shift;
      auto v = static_cast<uint8_t>((b & mask) >> shift);
      this->data += static_cast<char>(v);
    }
  }

  void write8(uint8_t b) { this->writeN<1>(b); }

  void write16(uint16_t b) { this->writeN<2>(b); }

  void write32(uint32_t b) { this->writeN<4>(b); }

  void writeT(ArchiveType t) {
    static_assert(sizeof(std::underlying_type_t<decltype(t)>) == sizeof(unsigned char));
    this->write8(static_cast<unsigned char>(t));
  }
};

class Unarchiver {
private:
  TypePool &pool;
  const std::string &data;
  unsigned int pos{0};

public:
  Unarchiver(TypePool &pool, const std::string &data) : pool(pool), data(data) {}

  Optional<FieldHandle> take() {
    this->pos = 0;
    return this->unpackHandle();
  }

private:
  const DSType *unpackType();

  Optional<FieldHandle> unpackHandle();

  template <unsigned int N>
  uint64_t readN() {
    static_assert(N > 0 && N < 9, "out of range");
    uint64_t v = 0;
    for (unsigned int i = N; i > 0; --i) {
      const uint64_t shift = (i - 1) * 8;
      v |= static_cast<uint64_t>(this->data[this->pos++]) << shift;
    }
    return v;
  }

  uint8_t read8() { return static_cast<uint8_t>(this->readN<1>()); }

  uint16_t read16() { return static_cast<uint16_t>(this->readN<2>()); }

  uint32_t read32() { return static_cast<uint32_t>(this->readN<4>()); }

  ArchiveType readT() {
    auto v = this->read8();
    return static_cast<ArchiveType>(v);
  }
};

class Archive {
private:
  std::string name;

  /**
   * uint32 commitID
   * uint32 index
   * uint16 attribute
   * uint16 modID
   *
   * DSType
   */
  std::string data;

  Archive(std::string &&name, std::string &&data) : name(std::move(name)), data(std::move(data)) {}

public:
  static Archive pack(const TypePool &pool, unsigned int builtinTypeIdCount,
                      const std::string &fieldName, const FieldHandle &handle) {
    Archiver archiver(pool, builtinTypeIdCount);
    archiver.add(handle);
    return Archive(std::string(fieldName), std::move(archiver).take());
  }

  const std::string &getName() const { return this->name; }

  const std::string &getData() const { return this->data; }

  /**
   *
   * @param pool
   * @return
   * if invalid type pool, return invalid
   */
  Optional<FieldHandle> unpack(TypePool &pool) const {
    Unarchiver unarchiver(pool, this->getData());
    return unarchiver.take();
  }
};

class ModuleArchive {
private:
  std::vector<Archive> handles;

public:
  explicit ModuleArchive(std::vector<Archive> &&handles) : handles(std::move(handles)) {}

  static ModuleArchive create(const TypePool &pool, const ModType &modType, unsigned int idCount);

  const auto &getHandles() const { return this->handles; }

  Optional<std::unordered_map<std::string, FieldHandle>> unpack(TypePool &pool) const;
};

class ModuleIndex;

/**
 * deserialize archives and load into foregin TypePool
 * @param pool
 * @param index
 * @return
 */
const ModType *loadFromModuleIndex(TypePool &pool, const ModuleIndex &index);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_ARCHIVE_H
