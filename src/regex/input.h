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

#include "misc/result.hpp"
#include "misc/string_ref.hpp"
#include "misc/unicode.hpp"

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

namespace arsh::regex {

/**
 *
 * @param iter must be valid utf8
 * @return
 */
[[gnu::always_inline]] inline int unsafeNextUtf8(const char *&iter) {
  const unsigned char b = *iter;
  if (likely(b < 128)) {
    ++iter;
    return b;
  }
  const auto *tmp = reinterpret_cast<const unsigned char *>(iter);
  const unsigned int len = UnicodeUtil::utf8ByteSize(b);
  int codePoint = 0;
  if (len == 2) {
    codePoint = static_cast<int>(((tmp[0] & 0x1Fu) << 6u) | (tmp[1] & 0x3Fu));
  } else if (len == 3) {
    codePoint =
        static_cast<int>(((tmp[0] & 0x0Fu) << 12u) | ((tmp[1] & 0x3Fu) << 6u) | (tmp[2] & 0x3Fu));
  } else { // len == 4
    codePoint = static_cast<int>(((tmp[0] & 0x07u) << 18u) | ((tmp[1] & 0x3Fu) << 12u) |
                                 ((tmp[2] & 0x3Fu) << 6u) | (tmp[3] & 0x3Fu));
  }
  iter += len;
  return codePoint;
}

inline void unsafeNextUtf8Noreturn(const char *&iter) { iter += UnicodeUtil::utf8ByteSize(*iter); }

/**
 *
 * @param iter must be valid utf8
 * @return
 */
[[gnu::always_inline]] inline int unsafePrevUtf8(const char *&iter) {
  constexpr unsigned char masks[] = {0xFF, 0x1F, 0x0F, 0x07};
  unsigned int codePoint = 0;
  unsigned int len;
  unsigned int shift = 0;
  while ((len = UnicodeUtil::utf8ByteSize(*--iter)) == 0) {
    codePoint |= (static_cast<unsigned char>(*iter) & 0x3Fu) << shift;
    shift += 6;
  }
  codePoint |= (static_cast<unsigned char>(*iter) & masks[len - 1]) << shift;
  return static_cast<int>(codePoint);
}

inline void unsafeRemovePrefixUtf8(StringRef &ref) {
  ref.removePrefix(UnicodeUtil::utf8ByteSize(ref[0]));
}

inline void unsafeRemoveSuffixUtf8(StringRef &ref) {
  const char *iter = ref.end();
  unsafePrevUtf8(iter);
  ref.removeSuffix(ref.end() - iter);
}

class Input {
public:
  static constexpr size_t INPUT_MAX = UINT32_MAX;

  enum class Error : unsigned char {
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

  static Result<Input, Error> create(const StringRef text, const unsigned int codePointOffset = 0) {
    if (text.size() > INPUT_MAX) {
      return Err(Error::TOO_LARGE);
    }
    auto iter = text.begin();
    unsigned int offset = 0;
    const auto end = text.end();
    for (unsigned int count = 0; iter != end; count++) {
      if (const auto len = UnicodeUtil::wtf8ValidateChar(iter, end)) {
        iter += len;
        if (count < codePointOffset) {
          offset += len;
        }
        continue;
      }
      return Err(Error::INVALID_UTF8);
    }
    Input input(text);
    if (offset) {
      input.iter += offset;
    }
    return Ok(input);
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

  [[gnu::always_inline]] int cur() const {
    auto i = this->iter;
    return unsafeNextUtf8(i);
  }

  [[gnu::always_inline]] int consumeForward() { return unsafeNextUtf8(this->iter); }

  [[gnu::always_inline]] int prev() const {
    auto i = this->iter;
    return unsafePrevUtf8(i);
  }

  /**
   * for look-behind.
   * @return
   */
  [[gnu::always_inline]] int consumeBackward() { return unsafePrevUtf8(this->iter); }

  StringRef remainForward() const {
    return {this->iter, static_cast<size_t>(this->end - this->iter)};
  }

  StringRef remainForwardOfCodePoints(unsigned int codePointCount) const {
    unsigned int count = 0;
    auto cur = this->iter;
    while (count < codePointCount && cur < this->end) {
      cur += UnicodeUtil::utf8ByteSize(*cur);
      count++;
    }
    return {this->iter, static_cast<size_t>(cur - this->iter)};
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

  StringRef remainBackwardOfCodePoints(unsigned int codePointCount) const {
    unsigned int count = 0;
    auto cur = this->iter;
    while (count < codePointCount && cur > this->begin) {
      unsafePrevUtf8(cur);
      count++;
    }
    return {cur, static_cast<size_t>(this->iter - cur)};
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
