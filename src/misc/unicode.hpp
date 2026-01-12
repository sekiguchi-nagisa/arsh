/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef MISC_LIB_UNICODE_HPP
#define MISC_LIB_UNICODE_HPP

#include <clocale>
#include <cstring>

#include "codepoint_set.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

/**
 * for east asian ambiguous character.
 */
enum class AmbiguousCharWidth : unsigned char {
  HALF,
  FULL,
};

namespace detail_unicode {

template <bool T>
struct UnicodeUtil {
  static_assert(T, "not allowed instantiation");

  static constexpr int CODE_POINT_MAX = 0x10FFFF;

  static constexpr int REPLACEMENT_CHAR_CODE = 0xFFFD;

  static constexpr const char *REPLACEMENT_CHAR_UTF8 = "\xEF\xBF\xBD";

  /**
   * if b is illegal start byte of UTF-8, skip it.
   */
  static size_t utf8NextPos(size_t pos, unsigned char b) {
    unsigned int size = utf8ByteSize(b);
    return pos + (size > 0 ? size : 1);
  }

  /**
   * if b is illegal start byte of UTF-8, return always 0.
   */
  static unsigned int utf8ByteSize(unsigned char b);

  /**
   *
   * @param begin0
   * @param end0
   * exclusive
   * @return
   * if first character is invalid UTF-8, return 0.
   * Otherwise, return byte size.
   */
  static unsigned int utf8ValidateChar(const char *begin0, const char *end0);

  /**
   * if illegal UTF-8 code, return -1.
   * otherwise, return converted code.
   */
  static int utf8ToCodePoint(const char *buf, std::size_t bufSize) {
    return utf8ToCodePoint(buf, buf + bufSize);
  }

  static int utf8ToCodePoint(const char *begin, const char *end) {
    int codePoint = 0;
    utf8ToCodePoint(begin, end, codePoint);
    return codePoint;
  }

  /**
   * write converted value to codePoint.
   * if illegal UTF-8 code, write -1 and return 0.
   * otherwise, return byte size of UTF-8.
   */
  static unsigned int utf8ToCodePoint(const char *buf, std::size_t bufSize, int &codePoint) {
    return utf8ToCodePoint(buf, buf + bufSize, codePoint);
  }

  static unsigned int utf8ToCodePoint(const char *begin, const char *end, int &codePoint);

  /**
   *
   * @param codePoint
   * must be Unicode code point
   * @param buf
   * at least 4byte
   * @return
   * size of written byte.
   * if invalid code point, return 0 and do nothing
   */
  static unsigned int codePointToUtf8(int codePoint, char *buf);

  static bool isValidCodePoint(int codePoint) {
    return codePoint >= 0x0000 && codePoint <= CODE_POINT_MAX;
  }

  static bool isBmpCodePoint(int codePoint) { return codePoint >= 0x0000 && codePoint <= 0xFFFF; }

  static bool isSupplementaryCodePoint(int codePoint) {
    return codePoint > 0xFFFF && codePoint <= CODE_POINT_MAX;
  }

  static bool isHighSurrogate(unsigned short v) { return v >= 0xD800 && v <= 0xDBFF; }

  static bool isLowSurrogate(unsigned short v) { return v >= 0xDC00 && v <= 0xDFFF; }

  static bool isSurrogate(unsigned short v) { return v >= 0xD800 && v <= 0xDFFF; }

  /**
   * if illegal surrogate pair, return -1.
   * @param high
   * @param low
   * @return
   */
  static int utf16ToCodePoint(unsigned short high, unsigned short low) {
    if (isHighSurrogate(high) && isLowSurrogate(low)) {
      return static_cast<int>((static_cast<unsigned int>(high - 0xD800) << 10) +
                              static_cast<unsigned int>(low - 0xDC00) + 0x10000);
    }
    return -1;
  }

  /**
   * get width of unicode code point.
   * return -3, if codePoint is negate.
   * return -2, if combining character
   * return -1, if control character.
   * return 0, if null character.
   * return 2, if wide width character.
   * return 1, otherwise.
   *
   * codePoint must be unicode code point.
   */
  static int width(int codePoint, AmbiguousCharWidth ambiguousCharWidth = AmbiguousCharWidth::HALF);

  static bool isCJKLocale() {
    const char *ctype = setlocale(LC_CTYPE, nullptr);
    if (ctype != nullptr) {
      const char *cjk[] = {"ja", "zh", "ko"};
      for (const auto &l : cjk) {
        if (strstr(ctype, l) != nullptr) {
          return true;
        }
      }
    }
    return false;
  }

  /**
   * if LC_CTYPE is CJK, call width(codePoint, TWO_WIDTH)
   */
  static int localeAwareWidth(int codePoint) {
    return width(codePoint, isCJKLocale() ? AmbiguousCharWidth::FULL : AmbiguousCharWidth::HALF);
  }
};

// #########################
// ##     UnicodeUtil     ##
// #########################

template <bool T>
unsigned int UnicodeUtil<T>::utf8ByteSize(unsigned char b) {
  constexpr unsigned char table[256] = {
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

      2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
      2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
      4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  return table[b];
}

template <bool T>
unsigned int UnicodeUtil<T>::utf8ValidateChar(const char *begin0, const char *end0) {
  auto begin = reinterpret_cast<const unsigned char *>(begin0);
  auto end = reinterpret_cast<const unsigned char *>(end0);

  if (begin == end) {
    return 0;
  }

  /**
   * This code has been taken from utf8_check.c which was developed by
   * arkus Kuhn <http://www.cl.cam.ac.uk/~mgk25/>.
   *
   * For original code / licensing please refer to
   * https://www.cl.cam.ac.uk/%7Emgk25/ucs/utf8_check.c
   */
  if (*begin < 0x80) { // 0xxxxxxx
    return 1;
  } else if ((begin[0] & 0xE0) == 0xC0) { // 110xxxxx 10xxxxxx
    if (begin + 1 == end || (begin[1] & 0xC0) != 0x80 || (begin[0] & 0xFE) == 0xC0) { // overlong
      return 0;
    }
    return 2;
  } else if ((begin[0] & 0xF0) == 0xE0) { // 1110xxxx 10xxxxxx 10xxxxxx
    if (begin + 2 >= end || (begin[1] & 0xC0) != 0x80 || (begin[2] & 0xC0) != 0x80 ||
        (begin[0] == 0xE0 && (begin[1] & 0xE0) == 0x80) ||
        (begin[0] == 0xED && (begin[1] & 0xE0) == 0xA0)) {
      return 0;
    }
    return 3;
  } else if ((begin[0] & 0xF8) == 0xF0) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (begin + 3 >= end || (begin[1] & 0xC0) != 0x80 || (begin[2] & 0xC0) != 0x80 ||
        (begin[3] & 0xC0) != 0x80 || (begin[0] == 0xF0 && (begin[1] & 0xF0) == 0x80) || // overlong
        (begin[0] == 0xF4 && begin[1] > 0x8F) || begin[0] > 0xF4) { // > U+10FFFF
      return 0;
    }
    return 4;
  }
  return 0;
}

template <bool T>
unsigned int UnicodeUtil<T>::utf8ToCodePoint(const char *begin0, const char *end0, int &codePoint) {
  const unsigned int size = utf8ValidateChar(begin0, end0);
  auto begin = reinterpret_cast<const unsigned char *>(begin0);
  switch (size) {
  case 1:
    codePoint = static_cast<unsigned char>(begin[0]);
    break;
  case 2:
    codePoint = static_cast<int>((static_cast<unsigned int>(begin[0] & 0x1F) << 6) |
                                 static_cast<unsigned int>(begin[1] & 0x3F));
    break;
  case 3:
    codePoint = static_cast<int>((static_cast<unsigned int>(begin[0] & 0x0F) << 12) |
                                 (static_cast<unsigned int>(begin[1] & 0x3F) << 6) |
                                 static_cast<unsigned int>(begin[2] & 0x3F));
    break;
  case 4:
    codePoint = static_cast<int>((static_cast<unsigned int>(begin[0] & 0x07) << 18) |
                                 (static_cast<unsigned int>(begin[1] & 0x3F) << 12) |
                                 (static_cast<unsigned int>(begin[2] & 0x3F) << 6) |
                                 static_cast<unsigned int>(begin[3] & 0x3F));
    break;
  default:
    codePoint = -1;
    break;
  }
  return size;
}

template <bool T>
unsigned int UnicodeUtil<T>::codePointToUtf8(int codePoint, char *const buf) {
  if (!isValidCodePoint(codePoint)) {
    return 0;
  }

  if (codePoint <= 0x7F) { // 0xxxxxxx
    buf[0] = static_cast<char>(codePoint);
    return 1;
  } else if (codePoint <= 0x7FF) { // 110xxxxx 10xxxxxx
    buf[0] = static_cast<char>(0xC0 | (codePoint >> 6));
    buf[1] = static_cast<char>(0x80 | (codePoint & 0x3F));
    return 2;
  } else if (codePoint <= 0xFFFF) { // 1110xxxx 10xxxxxx 10xxxxxx
    buf[0] = static_cast<char>(0xE0 | (codePoint >> 12));
    buf[1] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (codePoint & 0x3F));
    return 3;
  } else if (codePoint <= 0x10FFFF) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    buf[0] = static_cast<char>(0xF0 | (codePoint >> 18));
    buf[1] = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (codePoint & 0x3F));
    return 4;
  }
  return 0;
}

template <bool T>
int UnicodeUtil<T>::width(int codePoint, AmbiguousCharWidth ambiguousCharWidth) {
#define PROPERTY_SET_RANGE BMPCodePointRange
#define PROPERTY_SET_RANGE_TABLE CodePointSetRef
#define PACKED(F, L) PackedNonBMPCodePointRange::pack(F, L)

#include "unicode_width.in"

#undef PROPERTY_SET_RANGE
#undef PROPERTY_SET_RANGE_TABLE
#undef PACKED

  if (codePoint < 0) {
    return -3;
  }
  if (codePoint == 0) {
    return 0; // null character width is 0
  }

  if (codePoint >= 32 && codePoint < 127) { // ascii printable character
    return 1;
  }

  if (codePoint < 32 || (codePoint >= 0x7F && codePoint < 0xA0)) { // control character
    return -1;
  }

  // search zero-width (combining) character
  if (zero_width_table.contains(codePoint)) {
    return -2;
  }

  // search ambiguous width character
  if (ambiguousCharWidth == AmbiguousCharWidth::FULL && ambiguous_width_table.contains(codePoint)) {
    return 2;
  }

  // search two width character
  if (codePoint < 0x1100) {
    return 1;
  }

  if (two_width_table.contains(codePoint)) {
    return 2;
  }
  return 1;
}

} // namespace detail_unicode

using UnicodeUtil = detail_unicode::UnicodeUtil<true>;

struct Utf8Stream {
  const char *iter{nullptr};
  const char *end{nullptr};

  Utf8Stream(const char *begin, const char *end) : iter(begin), end(end) {}

  explicit operator bool() const { return this->iter != this->end; }

  const char *saveState() const { return this->iter; }

  void restoreState(const char *ptr) { this->iter = ptr; }

  int nextCodePoint() {
    int codePoint = 0;
    unsigned int size = UnicodeUtil::utf8ToCodePoint(this->iter, this->end, codePoint);
    if (size < 1) {
      codePoint = -1;
      if (this->iter != this->end) {
        this->iter++;
      }
    } else {
      this->iter += size;
    }
    return codePoint;
  }
};

class CodePointWithMeta {
private:
  /**
   * | 8bit (meta) | 24bit (code point) |
   */
  unsigned int value{0};

  explicit CodePointWithMeta(unsigned int v) : value(v) {}

public:
  CodePointWithMeta() = default;

  static CodePointWithMeta from(unsigned int v) { return CodePointWithMeta(v); }

  constexpr CodePointWithMeta(int codePoint, unsigned char meta) : value(meta << 24) {
    this->value |= codePoint < 0 ? 0xFFFFFF : static_cast<unsigned int>(codePoint) & 0xFFFFFF;
  }

  unsigned int getValue() const { return this->value; }

  unsigned char getMeta() const { return this->value >> 24; }

  int codePoint() const {
    unsigned int cc = this->value & 0xFFFFFF;
    return cc == 0xFFFFFF ? -1 : static_cast<int>(cc);
  }

  struct Comp {
    bool operator()(CodePointWithMeta l, int r) const { return l.codePoint() < r; }

    bool operator()(int l, CodePointWithMeta r) const { return l < r.codePoint(); }
  };
};

template <typename T>
class CodePointPropertyInterval {
private:
  static_assert(sizeof(T) <= sizeof(uint16_t));

  /**
   * | property (16bit) | inclusive first (24bit) | inclusive last (24bit) |
   */
  uint64_t data;

public:
  constexpr CodePointPropertyInterval(int first, int last, T p)
      : data(static_cast<uint64_t>(p) << 48 | static_cast<uint64_t>(first) << 24 |
             static_cast<uint64_t>(last)) {}

  constexpr int first() const { return static_cast<int>(this->data >> 24 & 0xFFFFFF); }

  constexpr int last() const { return static_cast<int>(this->data & 0xFFFFFF); }

  constexpr T property() const { return static_cast<T>(static_cast<uint16_t>(this->data >> 48)); }

  bool contains(int codePoint) const {
    return codePoint >= this->first() && codePoint <= this->last();
  }

  struct Comp {
    bool operator()(CodePointPropertyInterval l, int r) const { return l.last() < r; }

    bool operator()(int l, CodePointPropertyInterval r) const { return l < r.first(); }
  };
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_UNICODE_HPP
