/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#include "format_util.h"
#include "constant.h"
#include "misc/num_util.hpp"
#include "misc/unicode.hpp"

namespace arsh {

int vformatTo(std::string &out, const char *fmt, va_list arg) {
  char data[32];
  va_list arg2;
  va_copy(arg2, arg);
  int s = vsnprintf(data, std::size(data), fmt, arg);
  if (s > 0) {
    if (static_cast<unsigned int>(s) < std::size(data)) { // fast path
      out.append(data, s);
      return s;
    }
    // reallocate
    const auto size = out.size();
    const std::string::size_type newSize = size + s;
    out.resize(newSize + 1); // reserve sentinel
    s = vsnprintf(out.data() + size, s + 1, fmt, arg2);
    if (s > 0) {
      out.resize(newSize);
    }
  }
  return s;
}

int formatTo(std::string &out, const char *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  const int s = vformatTo(out, fmt, arg);
  va_end(arg);
  return s;
}

bool quoteAsCmdOrShellArg(const StringRef ref, std::string &out, bool asCmd) {
  StringRef::size_type hexCount = 0;
  auto begin = ref.begin();
  const auto end = ref.end();
  if (asCmd && begin != end) {
    if (char ch = *begin; isDecimal(ch) || ch == '+' || ch == '-') {
      out += '\\';
      out += ch;
      begin++;
    }
  }
  while (begin != end) {
    int codePoint = 0;
    const unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(begin, end, codePoint);
    if (byteSize == 0) { // invalid utf-8 byte sequence
      char d[32];
      int size = snprintf(d, std::size(d), "$'\\x%02x'", static_cast<unsigned char>(*begin));
      assert(size > 0);
      out.append(d, static_cast<unsigned int>(size));
      hexCount++;
      begin++;
    } else {
      switch (codePoint) {
      case ' ':
      case ';':
      case '\'':
      case '"':
      case '`':
      case '\\':
      case '|':
      case '&':
      case '<':
      case '>':
      case '(':
      case ')':
      case '$':
      case '#':
      case '~':
      case '{':
      case '}':
      case '[':
      case ']':
      case '*':
      case '?':
      case '!':
        assert(byteSize == 1);
        out += '\\';
        out.append(begin, byteSize);
        break;
      default:
        if ((codePoint >= 0 && codePoint < 32) || codePoint == 127) {
          char d[32];
          int size = snprintf(d, std::size(d), "$'\\x%02x'", codePoint);
          assert(size > 0);
          out.append(d, static_cast<unsigned int>(size));
          hexCount++;
        } else {
          out.append(begin, byteSize);
        }
        break;
      }
      begin += byteSize;
    }
  }
  return hexCount == 0;
}

std::string toPrintable(const StringRef ref) {
  std::string out;
  appendAsPrintable(ref, SYS_LIMIT_PRINTABLE_MAX, out);
  return out;
}

bool appendAsPrintable(StringRef ref, size_t maxSize, std::string &out) {
  const auto old = errno;
  bool status = true;
  for (int ch : ref) { // for arm32/arm64
    if (unlikely(out.size() >= maxSize)) {
      if (maxSize >= 3) {
        out.resize(maxSize - 3);
        out += "...";
      }
      status = false;
      goto END;
    }
    if ((ch >= 0 && ch < 32) || ch == 127) {
      char d[16];
      snprintf(d, std::size(d), "\\x%02x", ch);
      out += d;
    } else {
      out += static_cast<char>(ch);
    }
  }
END:
  errno = old;
  return status;
}

std::string unquoteCmdArgLiteral(const StringRef ref, bool unescape) {
  std::string str;
  str.reserve(ref.size());

  for (StringRef::size_type i = 0; i < ref.size(); i++) {
    char ch = ref[i];
    if (ch == '\\' && i + 1 < ref.size()) {
      const char next = ref[++i];
      if (next == '\n') {
        continue;
      }
      ch = next;
      if (!unescape) { // for glob bracket expression
        str += '\\';
      }
    }
    str += ch;
  }
  return str;
}

} // namespace arsh