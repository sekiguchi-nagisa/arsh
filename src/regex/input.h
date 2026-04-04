/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_REGEX_INPUT_H
#define ARSH_REGEX_INPUT_H

#include "misc/string_ref.hpp"
#include "misc/unicode.hpp"

namespace arsh::regex {

/**
 *
 * @param iter must be valid utf8
 * @return
 */
inline int unsafePrevUtf8(const char *&iter) {
  constexpr unsigned char masks[] = {0xFF, 0x1F, 0x0F, 0x07};
  unsigned int codePoint = 0;
  unsigned int len;
  unsigned int shift = 0;
  while ((len = UnicodeUtil::utf8ByteSize(*--iter)) == 0) {
    codePoint |= (*iter & 0x3F) << shift;
    shift += 6;
  }
  codePoint |= (*iter & masks[len - 1]) << shift;
  return static_cast<int>(codePoint);
}

class Input {
public:
  static constexpr size_t INPUT_MAX = UINT32_MAX;

  enum class Status : unsigned char {
    OK,
    TOO_LARGE,
    INVALID_UTF8,
  };

private:
  const char *begin{nullptr};
  const char *end{nullptr};
  const char *iter{nullptr};

  explicit Input(StringRef text) : begin(text.begin()), end(text.end()), iter(text.begin()) {}

public:
  Input() = default;

  static Status create(StringRef text, Input &input) {
    if (text.size() > INPUT_MAX) {
      return Status::TOO_LARGE;
    }
    auto iter = text.begin();
    const auto end = text.end();
    while (iter != end) {
      if (const auto len = UnicodeUtil::utf8ValidateChar(iter, end)) {
        iter += len;
        continue;
      }
      return Status::INVALID_UTF8;
    }
    input = Input(text);
    return Status::OK;
  }

  explicit operator bool() const {
    return this->begin != nullptr && this->end != nullptr && this->iter != nullptr;
  }

  bool available() const { return this->iter != this->end; }

  bool availableBackward() const { return this->iter != this->begin; }

  bool isBegin() const { return this->iter == this->begin; }

  bool isEnd() const { return this->iter == this->end; }

  const char *getIter() const { return this->iter; }

  void setIter(const char *old) { this->iter = old; }

  unsigned int getOffset() const { return this->iter - this->begin; }

  const char *getBegin() const { return this->begin; }

  const char *getEnd() const { return this->end; }

  int cur() const {
    const unsigned int len = UnicodeUtil::utf8ByteSize(*this->iter);
    int codePoint = 0;
    switch (len) {
    case 1:
      codePoint = static_cast<unsigned char>(this->iter[0]);
      break;
    case 2:
      codePoint = static_cast<int>((static_cast<unsigned int>(this->iter[0] & 0x1F) << 6) |
                                   static_cast<unsigned int>(this->iter[1] & 0x3F));
      break;
    case 3:
      codePoint = static_cast<int>((static_cast<unsigned int>(this->iter[0] & 0x0F) << 12) |
                                   (static_cast<unsigned int>(this->iter[1] & 0x3F) << 6) |
                                   static_cast<unsigned int>(this->iter[2] & 0x3F));
      break;
    case 4:
      codePoint = static_cast<int>((static_cast<unsigned int>(this->iter[0] & 0x07) << 18) |
                                   (static_cast<unsigned int>(this->iter[1] & 0x3F) << 12) |
                                   (static_cast<unsigned int>(this->iter[2] & 0x3F) << 6) |
                                   static_cast<unsigned int>(this->iter[3] & 0x3F));
    default:
      break;
    }
    return codePoint;
  }

  int consumeForward() {
    const unsigned int len = UnicodeUtil::utf8ByteSize(*this->iter);
    int codePoint = 0;
    switch (len) {
    case 1:
      codePoint = static_cast<unsigned char>(this->iter[0]);
      break;
    case 2:
      codePoint = static_cast<int>((static_cast<unsigned int>(this->iter[0] & 0x1F) << 6) |
                                   static_cast<unsigned int>(this->iter[1] & 0x3F));
      break;
    case 3:
      codePoint = static_cast<int>((static_cast<unsigned int>(this->iter[0] & 0x0F) << 12) |
                                   (static_cast<unsigned int>(this->iter[1] & 0x3F) << 6) |
                                   static_cast<unsigned int>(this->iter[2] & 0x3F));
      break;
    case 4:
      codePoint = static_cast<int>((static_cast<unsigned int>(this->iter[0] & 0x07) << 18) |
                                   (static_cast<unsigned int>(this->iter[1] & 0x3F) << 12) |
                                   (static_cast<unsigned int>(this->iter[2] & 0x3F) << 6) |
                                   static_cast<unsigned int>(this->iter[3] & 0x3F));
    default:
      break;
    }
    this->iter += len;
    return codePoint;
  }

  int prev() const {
    auto i = this->iter;
    return unsafePrevUtf8(i);
  }

  /**
   * for look-behind.
   * @return
   */
  int consumeBackward() { return unsafePrevUtf8(this->iter); }

  StringRef remainForward() const {
    return {this->iter, static_cast<size_t>(this->end - this->iter)};
  }

  bool expectForward(StringRef needle) {
    if (this->remainForward().startsWith(needle)) {
      this->iter += needle.size();
      return true;
    }
    return false;
  }

  StringRef remainBackward() const {
    return {this->begin, static_cast<size_t>(this->iter - this->begin)};
  }

  bool expectBackward(StringRef needle) {
    if (this->remainBackward().endsWith(needle)) {
      this->iter -= needle.size();
      return true;
    }
    return false;
  }
};

} // namespace arsh::regex

#endif // ARSH_REGEX_INPUT_H
