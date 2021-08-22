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

#include <mutex>
#include <string>
#include <unordered_map>

#include "lsp.h"

namespace ydsh::lsp {

struct TextDocumentContentChangeEvent;

class Source {
private:
  const char *path;
  std::string content;
  unsigned short srcId{0};
  int version{0};

public:
  Source(const char *path, unsigned short srcId, std::string &&content, int version)
      : path(path), content(std::move(content)), srcId(srcId), version(version) {}

  const char *getPath() const { return this->path; }

  const std::string &getContent() const { return this->content; }

  int getVersion() const { return this->version; }

  unsigned short getSrcId() const { return this->srcId; }

  void update(std::string &&c, int v) {
    this->content = std::move(c);
    this->version = v;
  }
};

using SourcePtr = std::shared_ptr<Source>;

class SourceManager {
private:
  std::vector<std::pair<CStrPtr, SourcePtr>> entries;
  StrRefMap<unsigned int> indexMap; // fullpath to index mapping
  mutable std::mutex mutex;

public:
  /**
   *
   * @param id
   * id > 0
   * @return
   */
  SourcePtr findById(unsigned int id) const;

  /**
   * @param path
   * must be fullpath
   * @return
   */
  SourcePtr find(StringRef path) const;

  /**
   *
   * @param path
   * must be fullpath
   * @param version
   * @param content
   * @return
   * if module id reaches limit, return null
   */
  SourcePtr update(StringRef path, int version, std::string &&content); // FIXME:
};

/**
 *
 * @param content
 * may not be terminated with newline
 * @param position
 * @return
 */
Optional<unsigned int> toTokenPos(const std::string &content, const Position &position);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param pos
 * @return
 */
Optional<Position> toPosition(const std::string &content, unsigned int pos);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param range
 * @return
 */
Optional<ydsh::Token> toToken(const std::string &content, const Range &range);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param token
 * @return
 */
Optional<Range> toRange(const std::string &content, Token token);

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SOURCE_H
