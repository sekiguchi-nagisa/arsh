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
#include <misc/unicode.hpp>

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
    auto src = std::make_shared<Source>(ptr.get(), static_cast<unsigned short>(id), std::move(tmp),
                                        version);
    auto &pair = this->entries.emplace_back(std::move(ptr), std::move(src));
    this->indexMap.emplace(pair.first.get(), i);
    return this->entries[i].second;
  }
}

static size_t findLineStartPos(const std::string &content, unsigned int count) {
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

static size_t count(const std::string &content, unsigned int offset, unsigned int count) {
  size_t limit = offset;
  for (; limit < content.size() && content[limit] != '\n'; limit++)
    ;
  auto line = StringRef(content).slice(offset, limit + 1);

  const char *iter = line.begin();
  const char *end = line.end();
  for (unsigned int i = 0; i < count && iter != end;) {
    int codePoint;
    unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint);
    if (byteSize == 0) {
      byteSize++;
    }
    iter += byteSize;
    i += codePoint < 0 || UnicodeUtil::isBmpCodePoint(codePoint) ? 1 : 2;
  }
  return offset + (iter - line.begin());
}

Optional<unsigned int> toTokenPos(const std::string &content, const Position &position) {
  if (position.line < 0 || position.character < 0 || content.size() > UINT32_MAX) {
    return {};
  }
  size_t offset = findLineStartPos(content, position.line);
  size_t pos = count(content, offset, position.character);
  if (pos > UINT32_MAX) {
    return {};
  }
  return static_cast<unsigned int>(pos);
}

static unsigned int utf16Len(StringRef ref) {
  unsigned int count = 0;
  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    int codePoint;
    unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint);
    if (byteSize == 0) {
      byteSize++;
    }
    iter += byteSize;
    count += codePoint < 0 || UnicodeUtil::isBmpCodePoint(codePoint) ? 1 : 2;
  }
  return count;
}

Optional<Position> toPosition(const std::string &content, unsigned int pos) {
  if (content.size() > UINT32_MAX) {
    return {};
  }
  pos = static_cast<unsigned int>(std::min(content.size(), static_cast<size_t>(pos)));

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
  auto line = StringRef(content).slice(offset, pos);
  offset = utf16Len(line);
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