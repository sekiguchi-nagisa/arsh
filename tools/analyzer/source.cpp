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

#include <constant.h>

#include "lsp.h"
#include "source.h"

namespace ydsh::lsp {

SourcePtr SourceManager::findById(unsigned int id) const {
  std::lock_guard<std::mutex> lockGuard(this->mutex);

  if (id > 0 && --id < this->entries.size()) {
    return this->entries[id].second;
  }
  return nullptr;
}

SourcePtr SourceManager::find(StringRef path) const {
  std::lock_guard<std::mutex> lockGuard(this->mutex);

  auto iter = this->indexMap.find(path);
  if (iter != this->indexMap.end()) {
    return this->entries[iter->second].second;
  }
  return nullptr;
}

SourcePtr SourceManager::update(StringRef path, int version, std::string &&content) {
  std::lock_guard<std::mutex> lockGuard(this->mutex);

  std::string tmp = std::move(content);
  if (tmp.empty() || tmp.back() != '\n') {
    tmp += '\n';
  }

  auto iter = this->indexMap.find(path);
  if (iter != this->indexMap.end()) {
    unsigned int i = iter->second;
    this->entries[i].second->update(std::move(tmp), version);
    return this->entries[i].second;
  } else {
    unsigned int id = this->entries.size() + 1;
    if (id == SYS_LIMIT_MOD_ID) {
      return nullptr;
    }
    unsigned int i = this->entries.size();
    auto ptr = CStrPtr(strdup(path.data()));
    path = ptr.get();
    this->entries.emplace_back(
        std::move(ptr), std::make_shared<Source>(path.data(), static_cast<unsigned short>(id),
                                                 std::move(tmp), version));
    this->indexMap.emplace(path, i);
    return this->entries[i].second;
  }
}

static unsigned int findLineStartPos(const std::string &content, unsigned int count) {
  const char *str = content.c_str();
  for (unsigned int i = 0; i < count; i++) {
    const char *ptr = strchr(str, '\n');
    if (!ptr) {
      break;
    }
    str = ++ptr;
  }
  return str - content.c_str();
}

static bool checkPos(const std::string &content, unsigned int pos) {
  if (pos < content.size()) {
    return true;
  } else if (pos > content.size()) {
    return false;
  } else {
    bool endWithNL = !content.empty() && content.back() == '\n';
    return !endWithNL;
  }
}

Optional<unsigned int> toTokenPos(const std::string &content, const Position &position) {
  if (position.line < 0 || position.character < 0) {
    return {};
  }
  unsigned int pos = findLineStartPos(content, position.line);
  pos += position.character;
  if (checkPos(content, pos)) {
    return pos;
  }
  return {};
}

Optional<Position> toPosition(const std::string &content, unsigned int pos) {
  if (!checkPos(content, pos)) {
    return {};
  }

  unsigned int c = 0;
  unsigned int offset = 0;
  char prev = '\0';
  for (unsigned int i = 0; i <= pos && i < content.size(); i++) {
    if (prev == '\n') {
      c++;
      offset = i;
    }
    prev = content[i];
  }
  offset = pos - offset;
  if (c > INT32_MAX || offset > INT32_MAX) {
    return {};
  }
  return Position{
      .line = static_cast<int>(c),
      .character = static_cast<int>(offset),
  };
}

Optional<Token> toToken(const std::string &content, const Range &range) {
  auto r = toTokenPos(content, range.start);
  if (!r.hasValue()) {
    return {};
  }
  unsigned int start = r.unwrap();
  r = toTokenPos(content, range.end);
  if (!r.hasValue()) {
    return {};
  }
  unsigned int end = r.unwrap();
  return Token{
      .pos = start,
      .size = end - start,
  };
}

Optional<Range> toRange(const std::string &content, Token token) {
  auto start = toPosition(content, token.pos);
  auto end = toPosition(content, token.endPos());
  if (start.hasValue() && end.hasValue()) {
    return Range{
        .start = start.unwrap(),
        .end = end.unwrap(),
    };
  }
  return {};
}

bool applyChange(std::string &content, const TextDocumentContentChangeEvent &change) {
  if (!change.range.hasValue()) {
    content = change.text;
    return true;
  } // FIXME: support incremental update
  return true;
}

} // namespace ydsh::lsp