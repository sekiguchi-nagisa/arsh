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

#ifndef ARSH_TOOLS_ANALYZER_SOURCE_H
#define ARSH_TOOLS_ANALYZER_SOURCE_H

#include <string>
#include <unordered_map>

#include <constant.h>

#include "lsp.h"

namespace arsh::lsp {

enum class SourceAttr : unsigned char {
  FROM_DISK = 1u << 0u, // loaded from disk (not opened in editor)
};

}

template <>
struct arsh::allow_enum_bitop<arsh::lsp::SourceAttr> : std::true_type {};

namespace arsh::lsp {

class Source {
private:
  std::shared_ptr<const std::string> path;
  std::string content;
  LineNumTable lineNumTable;
  uint64_t hash{0};
  ModId srcId{0};
  SourceAttr attr{};
  int version{0};

public:
  Source() = default;

  Source(uint64_t seed, std::shared_ptr<const std::string> path, ModId srcId, std::string &&content,
         int version, SourceAttr attr);

  Source(uint64_t seed, const char *path, ModId srcId, std::string &&content, int version,
         SourceAttr attr)
      : Source(seed, std::make_shared<const std::string>(path), srcId, std::move(content), version,
               attr) {}

  const std::string &getPath() const { return *this->path; }

  /**
   *
   * @return
   * always ends with newline
   */
  const std::string &getContent() const { return this->content; }

  uint64_t getHash() const { return this->hash; }

  bool equalsDigest(const Source &other) const {
    return this->getHash() == other.getHash() &&
           this->getContent().size() == other.getContent().size();
  }

  Token stripAppliedNameSigil(Token token) const;

  StringRef toStrRef(Token token) const {
    StringRef ref = this->content;
    ref = ref.substr(token.pos, token.size);
    return ref;
  }

  /**
   * convert byte offset (token pos) to LSP Position
   * @param pos
   * @return
   */
  Optional<Position> toPosition(unsigned int pos) const;

  Optional<Range> toRange(Token token) const;

  const LineNumTable &getLineNumTable() const { return this->lineNumTable; }

  int getVersion() const { return this->version; }

  ModId getSrcId() const { return this->srcId; }

  SourceAttr getAttr() const { return this->attr; }

  bool has(SourceAttr a) const { return hasFlag(this->getAttr(), a); }

  std::shared_ptr<Source> copyAndUpdate(uint64_t seed, std::string &&c, int v) const {
    return std::make_shared<Source>(seed, this->path, this->srcId, std::move(c), v, this->attr);
  }
};

using SourcePtr = std::shared_ptr<Source>;

class SourceManager {
private:
  const uint64_t seed;
  std::shared_ptr<const std::string> testWorkDir; // for testing. normally null
  std::vector<SourcePtr> entries;
  StrRefMap<unsigned int> indexMap;                // full-path to index mapping
  std::unordered_set<unsigned int> unusedIndexSet; // for removed entries

public:
  explicit SourceManager(uint64_t seed = 42, const std::string &dir = "")
      : seed(seed), testWorkDir(dir.empty() ? nullptr : std::make_shared<const std::string>(dir)) {}

  const auto &getTestWorkDir() const { return this->testWorkDir; }

  /**
   *
   * @param id
   * id > 0
   * @return
   */
  SourcePtr findById(ModId id) const;

  /**
   * @param path
   * must be full-path
   * @return
   */
  SourcePtr find(StringRef path) const;

  /**
   *
   * @param path
   * must be full-path
   * @param version
   * @param content
   * @param attr
   * @return
   * if module id reaches limit, return null
   */
  SourcePtr update(StringRef path, int version, std::string &&content, SourceAttr attr = {});

  /**
   * add source from other source manager.
   * if path of other source is already assigned, replace with other source
   * @param other
   * @return
   * newly added source
   */
  SourcePtr add(SourcePtr other);

  /**
   *
   * @param id
   * @return
   * if correctly removed, return removed src
   */
  SourcePtr remove(ModId id);

  std::shared_ptr<SourceManager> copy() const { return std::make_shared<SourceManager>(*this); }

  std::string resolveURI(const uri::URI &uri) const;

  uri::URI toURI(const std::string &path) const;
};

/**
 * FIXME: use Source?
 * @param content
 * may not be terminated with newline
 * @param position
 * @return
 */
Optional<unsigned int> toTokenPos(StringRef content, const Position &position);

/**
 * FIXME: use Source?
 * @param content
 * may not be terminated with newline
 * @param range
 * @return
 */
Optional<Token> toToken(StringRef content, const Range &range);

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change);

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_SOURCE_H
