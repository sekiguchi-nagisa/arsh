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

#include "source.h"

namespace ydsh::lsp {

Source::Source(std::shared_ptr<const std::string> path, ModId srcId, std::string &&content,
               int version)
    : path(std::move(path)), content(std::move(content)), srcId(srcId), version(version) {
  if (this->content.empty() || this->content.back() != '\n') {
    this->content += '\n';
  }

  this->lineNumTable.setOffset(0);
  StringRef ref = this->content;
  for (StringRef::size_type pos = 0; (pos = ref.find('\n', pos)) != StringRef::npos; pos++) {
    this->lineNumTable.addNewlinePos(pos);
  }
}

SourcePtr SourceManager::findById(ModId id) const {
  auto v = toValue(id);
  if (v > 0 && --v < this->entries.size()) {
    return this->entries[v];
  }
  return nullptr;
}

SourcePtr SourceManager::find(StringRef path) const {
  auto iter = this->indexMap.find(path);
  if (iter != this->indexMap.end()) {
    return this->entries[iter->second];
  }
  return nullptr;
}

SourcePtr SourceManager::update(StringRef path, int version, std::string &&content) {
  auto iter = this->indexMap.find(path);
  if (iter != this->indexMap.end()) {
    unsigned int i = iter->second;
    this->entries[i] = this->entries[i]->copyAndUpdate(version, std::move(content));
    return this->entries[i];
  } else {
    unsigned int id = this->entries.size() + 1;
    if (id == SYS_LIMIT_MOD_ID) {
      return nullptr;
    }
    unsigned int i = this->entries.size();
    auto src = std::make_shared<Source>(path.data(), ModId{static_cast<unsigned short>(id)},
                                        std::move(content), version);
    auto &ret = this->entries.emplace_back(std::move(src));
    this->indexMap.emplace(ret->getPath(), i);
    return this->entries[i];
  }
}

SourcePtr SourceManager::add(SourcePtr other) {
  if (!other) {
    return nullptr;
  }
  if (auto src = this->find(other->getPath()); src == other) {
    return other;
  }
  return this->update(other->getPath(), other->getVersion(), std::string(other->getContent()));
}

static size_t findLineStartPos(StringRef content, unsigned int count) {
  StringRef::size_type pos = 0;
  for (unsigned int i = 0; i < count; i++) {
    auto ret = content.find('\n', pos);
    if (ret == StringRef::npos) {
      break;
    }
    pos = ++ret;
  }
  return pos;
}

static size_t count(StringRef content, unsigned int offset, unsigned int count) {
  size_t limit = offset;
  for (; limit < content.size() && content[limit] != '\n'; limit++)
    ;
  auto line = content.slice(offset, limit + 1);

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

Optional<unsigned int> toTokenPos(StringRef content, const Position &position) {
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

Optional<Position> toPosition(const Source &src, unsigned int pos) {
  if (src.getContent().size() > UINT32_MAX) {
    return {};
  }
  pos = static_cast<unsigned int>(std::min(src.getContent().size() - 1, static_cast<size_t>(pos)));

  unsigned int c = src.getLineNumTable().lookup(pos);
  unsigned int offset = 0;
  if (c > 0) {
    offset = src.getLineNumTable().getNewlinePos(c - 1) + 1;
  }

  auto line = StringRef(src.getContent()).slice(offset, pos);
  offset = utf16Len(line);
  if (c > INT32_MAX || offset > INT32_MAX) {
    return {};
  }
  return Position{
      .line = static_cast<int>(c),
      .character = static_cast<int>(offset),
  };
}

Optional<Token> toToken(StringRef content, const Range &range) {
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

Optional<Range> toRange(const Source &src, Token token) {
  auto start = toPosition(src, token.pos);
  auto end = toPosition(src, token.endPos());
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
  }

  auto ret = toToken(content, change.range.unwrap());
  if (!ret.hasValue()) {
    return false;
  }
  Token token = ret.unwrap();
  if (change.rangeLength.hasValue()) {
    StringRef ref = content;
    ref = ref.substr(token.pos, token.size);
    unsigned int len = utf16Len(ref);
    if (len != change.rangeLength.unwrap()) {
      return false;
    }
  }
  content.replace(token.pos, token.size, change.text);
  return true;
}

std::string resolveURI(const SourceManager &srcMan, const uri::URI &uri) {
  std::string path;
  if (uri.getScheme() == "file") {
    path = uri.getPath();
  } else if (srcMan.getTestWorkDir() && uri.getScheme() == "test") {
    path += *srcMan.getTestWorkDir();
    if (path.back() != '/' && !uri.getPath().empty() && uri.getPath()[0] != '/') {
      path += "/";
    }
    path += uri.getPath();
  }
  return path;
}

uri::URI toURI(const SourceManager &srcMan, const std::string &path) {
  StringRef ref = path;
  const char *scheme = "file";
  if (srcMan.getTestWorkDir() && ref.startsWith(*srcMan.getTestWorkDir())) {
    ref.removePrefix((*srcMan.getTestWorkDir()).size());
    scheme = "test";
  }
  return uri::URI::fromPath(scheme, ref.toString());
}

} // namespace ydsh::lsp