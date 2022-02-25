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

#include "lsp.h"

namespace ydsh::lsp {

struct TextDocumentContentChangeEvent;

class Source {
private:
  std::shared_ptr<const std::string> path;
  std::string content;
  unsigned short srcId{0};
  int version{0};

public:
  Source() = default;

  Source(const char *path, unsigned short srcId, std::string &&content, int version)
      : path(std::make_shared<const std::string>(path)), content(std::move(content)), srcId(srcId),
        version(version) {}

  const std::string &getPath() const { return *this->path; }

  const std::string &getContent() const { return this->content; }

  int getVersion() const { return this->version; }

  unsigned short getSrcId() const { return this->srcId; }

  std::shared_ptr<Source> copyAndUpdate(int v, std::string &&c) const;
};

using SourcePtr = std::shared_ptr<Source>;

class SourceManager {
private:
  std::vector<SourcePtr> entries;
  StrRefMap<unsigned int> indexMap; // fullpath to index mapping

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
};

/**
 *
 * @param content
 * may not be terminated with newline
 * @param position
 * @return
 */
Optional<unsigned int> toTokenPos(StringRef content, const Position &position);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param pos
 * @return
 */
Optional<Position> toPosition(StringRef content, unsigned int pos);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param range
 * @return
 */
Optional<ydsh::Token> toToken(StringRef content, const Range &range);

/**
 *
 * @param content
 * may not be terminated with newline
 * @param token
 * @return
 */
Optional<Range> toRange(StringRef content, Token token);

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SOURCE_H
