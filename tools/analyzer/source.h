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

namespace ydsh::lsp {

struct TextDocumentContentChangeEvent;

class Source {
private:
  std::string content;
  unsigned short srcId;
  int version{0};

public:
  Source() = default;

  Source(unsigned short srcId, std::string &&content, int version)
      : content(std::move(content)), srcId(srcId), version(version) {}

  const std::string &getContent() const { return this->content; }

  int getVersion() const { return this->version; }

  unsigned short getSrcId() const { return this->srcId; }
};

class SourceManager {
private:
  std::unordered_map<std::string, Source> srcMap;

public:
  const Source *find(const std::string &path) const;

  /**
   *
   * @param path
   * @param version
   * @param content
   * @return
   * if module id reaches limit, return null
   */
  const Source *update(const std::string &path, int version, std::string &&content);
};

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SOURCE_H
