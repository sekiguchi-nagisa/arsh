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

#include "js_lexer.h"
#include "misc/num_util.hpp"
#include "misc/unicode.hpp"

namespace arsh::re262 {

const char *toString(JSTokenKind kind) {
  constexpr const char *table[] = {
#define GEN_STR(T, S) S,
      EACH_JS_TOKEN_KIND(GEN_STR)
#undef GEN_STR
  };
  return table[static_cast<unsigned int>(kind)];
}

static int nextCodePoint(const char *&iter, const char *end) {
  int codePoint;
  if (unsigned int len = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint)) {
    iter += len;
  }
  return codePoint;
}

static int parseHex(const char *&iter, const char *end, unsigned int size) {
  int codePoint = 0;
  for (unsigned int i = 0; i < size; i++) {
    if (iter == end || !isHex(*iter)) {
      return -1;
    }
    codePoint *= 16;
    codePoint += static_cast<int>(hexToNum(*iter));
    iter++;
  }
  return codePoint;
}

std::optional<std::u16string> JSLexer::unquoteString(StringRef ref, std::string *err) {
  if (ref.size() < 2 || ref.front() != ref.back() || (ref[0] != '\'' && ref[0] != '"')) {
    if (err) {
      *err += "must be valid string literal";
    }
    return {};
  }
  ref.removePrefix(1);
  ref.removeSuffix(1);

  std::u16string ret;
  const char *iter = ref.begin();
  for (const char *const end = ref.end(); iter != end;) {
    const auto old = iter;
    int codePoint = nextCodePoint(iter, end);
    if (codePoint == '\\') {
      int next = nextCodePoint(iter, end);
      switch (next) {
      case '0':
        if (iter == end || !isDecimal(*iter)) {
          codePoint = '\0';
          break;
        } // fall-thru
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        if (err) {
          *err += "octal escape sequence is not allowed: ";
          *err += StringRef(old, iter - old);
        }
        return {};
      case '8':
      case '9':
        if (err) {
          *err += "\\8 and \\9 escape sequences are not allowed";
        }
        return {};
      case '\'':
      case '"':
      case '\\':
        codePoint = next;
        break;
      case 'n':
        codePoint = '\n';
        break;
      case 'r':
        codePoint = '\r';
        break;
      case 'v':
        codePoint = '\v';
        break;
      case 't':
        codePoint = '\t';
        break;
      case 'b':
        codePoint = '\b';
        break;
      case 'f':
        codePoint = '\f';
        break;
      case '\r':
      case '\n':
      case 0x2028:
      case 0x2029:
        continue;
      case 'x':
        codePoint = parseHex(iter, end, 2);
        if (codePoint < 0) {
          if (err) {
            *err += "broken hex escape sequence: ";
            *err += StringRef(old, iter - old);
          }
          return {};
        }
        break;
      case 'u':
        if (iter != end && *iter == '{') {
          iter++;
          const auto start = iter;
          codePoint = 0;
          for (; iter != end && *iter != '}'; ++iter) {
            if (!isHex(*iter)) {
              if (err) {
                *err += "broken unicode escape sequence: ";
                *err += StringRef(old, iter - old);
              }
              return {};
            }
            codePoint *= 16;
            codePoint += static_cast<int>(hexToNum(*iter));
            if (codePoint > UnicodeUtil::CODE_POINT_MAX) {
              if (err) {
                *err += "out-of-range code point: \\U+";
                char data[16];
                snprintf(data, std::size(data), "%04X", codePoint);
                *err += data;
              }
              return {};
            }
          }
          if (iter == end || *iter != '}') {
            if (err) {
              *err += "unclosed unicode escape sequence: ";
              *err += StringRef(old, iter - old);
            }
            return {};
          }
          iter++;
          if (start + 1 == iter) {
            if (err) {
              *err += "empty unicode escape sequence: ";
              *err += StringRef(old, iter - old);
            }
            return {};
          }
        } else {
          codePoint = parseHex(iter, end, 4);
          if (codePoint < 0) {
            if (err) {
              *err += "broken unicode escape sequence: ";
              *err += StringRef(old, iter - old);
            }
            return {};
          }
        }
        break;
      default:
        if (next < 0) {
          if (err) {
            *err += "broken escape sequence: ";
            *err += StringRef(old, iter - old);
          }
          return {};
        }
        codePoint = next;
        break;
      }
    }
    if (UnicodeUtil::isBmpCodePoint(codePoint)) {
      ret += static_cast<char16_t>(codePoint);
    } else {
      assert(UnicodeUtil::isValidCodePoint(codePoint));
      const auto code = static_cast<unsigned int>(codePoint) - 0x10000;
      const auto high = static_cast<char16_t>((code >> 10) + 0xD800);
      const auto low = static_cast<char16_t>((code & 0x3FF) + 0xDC00);
      ret += high;
      ret += low;
    }
  }
  return ret;
}

} // namespace arsh::re262