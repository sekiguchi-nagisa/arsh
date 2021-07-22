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
  CStrPtr path;
  std::string content;
  unsigned short srcId{0};
  int version{0};

public:
  Source(CStrPtr &&path, unsigned short srcId, std::string &&content, int version)
      : path(std::move(path)), content(std::move(content)), srcId(srcId), version(version) {}

  const char *getPath() const { return this->path.get(); }

  const std::string &getContent() const { return this->content; }

  int getVersion() const { return this->version; }

  unsigned short getSrcId() const { return this->srcId; }

  void update(std::string &&c, int v) {
    this->content = std::move(c);
    this->version = v;
  }
};

class SourceManager {
private:
  std::vector<Source> sources;
  StrRefMap<unsigned int> indexMap; // fullpath to index mapping

public:
  unsigned int size() const {
    return this->sources.size();
  }

  /**
   *
   * @param id
   * id > 0
   * @return
   */
  const Source *findById(unsigned int id) const {
    if (id > 0 && --id < this->sources.size()) {
      return &this->sources[id];
    }
    return nullptr;
  }

  /**
   * @param path
   * must be fullpath
   * @return
   */
  const Source *find(StringRef path) const;

  /**
   *
   * @param path
   * must be fullpath
   * @param version
   * @param content
   * @return
   * if module id reaches limit, return null
   */
  const Source *update(StringRef path, int version, std::string &&content);
};

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change);

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_SOURCE_H
