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

#ifndef YDSH_TOOLS_ANALYZER_SOURCE_H
#define YDSH_TOOLS_ANALYZER_SOURCE_H

#include <string>
#include <unordered_map>

#include <constant.h>

#include "lsp.h"

namespace arsh::lsp {

struct TextDocumentContentChangeEvent;

class Source {
private:
  std::shared_ptr<const std::string> path;
  std::string content;
  LineNumTable lineNumTable;
  ModId srcId{0};
  int version{0};

public:
  Source() = default;

  Source(std::shared_ptr<const std::string> path, ModId srcId, std::string &&content, int version);

  Source(const char *path, ModId srcId, std::string &&content, int version)
      : Source(std::make_shared<const std::string>(path), srcId, std::move(content), version) {}

  const std::string &getPath() const { return *this->path; }

  /**
   *
   * @return
   * always ends with newline
   */
  const std::string &getContent() const { return this->content; }

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

  std::shared_ptr<Source> copyAndUpdate(int v, std::string &&c) const {
    return std::make_shared<Source>(this->path, this->srcId, std::move(c), v);
  }
};

using SourcePtr = std::shared_ptr<Source>;

class SourceManager {
private:
  std::shared_ptr<const std::string> testWorkDir; // for testing. normally null
  std::vector<SourcePtr> entries;
  StrRefMap<unsigned int> indexMap; // full-path to index mapping

public:
  void setTestWorkDir(std::string &&dir) {
    this->testWorkDir = std::make_shared<const std::string>(std::move(dir));
  }

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
   * @return
   * if module id reaches limit, return null
   */
  SourcePtr update(StringRef path, int version, std::string &&content);

  /**
   * add source from other source manager.
   * if path of other source is already assigned, replace with other source
   * @param other
   * @return
   * newly added source
   */
  SourcePtr add(SourcePtr other);

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
Optional<arsh::Token> toToken(StringRef content, const Range &range);

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change);

} // namespace arsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SOURCE_H
