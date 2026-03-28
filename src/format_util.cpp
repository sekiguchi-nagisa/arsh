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
#include "misc/format.hpp"
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

bool quoteAsCmdOrShellArg(const StringRef ref, std::string &out, const QuoteParam param) {
  const size_t oldSize = out.size();
  StringRef::size_type hexCount = 0;
  auto begin = ref.begin();
  const auto end = ref.end();
  if (param.asCmd && begin != end) {
    if (char ch = *begin; isDecimal(ch) || ch == '+' || ch == '-') {
      if (!param.carryBackslash || out.size() != oldSize) {
        out += '\\';
      }
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
        if (!param.carryBackslash || out.size() != oldSize) {
          out += '\\';
        }
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
    if (ch == '\\') {
      if (i + 1 == ref.size()) {
        continue; // skip trailing backslash
      }
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

static EscapeSeqResult okByte(unsigned char b, unsigned short size) {
  return {
      .kind = EscapeSeqResult::OK_BYTE,
      .consumedSize = size,
      .codePoint = b,
  };
}

static EscapeSeqResult ok(int code, unsigned short size) {
  return {
      .kind = EscapeSeqResult::OK_CODE,
      .consumedSize = size,
      .codePoint = code,
  };
}

static EscapeSeqResult ok(char ch) { return ok(ch, 2); }

static EscapeSeqResult err(EscapeSeqResult::Kind k, unsigned short size) {
  return {
      .kind = k,
      .consumedSize = size,
      .codePoint = -1,
  };
}

EscapeSeqResult parseEscapeSeq(const char *begin, const char *end, const EscapeSeqOption option) {
  if (begin == end || *begin != '\\' || (begin + 1) == end) {
    return err(EscapeSeqResult::END, 0);
  }
  const char *old = begin;
  begin++; // consume '\'
  switch (const char next = *(begin++); next) {
  case '\\':
    return ok('\\');
  case 'a':
    return ok('\a');
  case 'b':
    return ok('\b');
  case 'c':
    if (hasFlag(option, EscapeSeqOption::CONTROL_CHAR)) {
      if (begin == end || !isLetter(*begin)) {
        return err(EscapeSeqResult::NEED_CHARS, static_cast<unsigned short>(begin - old));
      }
      const unsigned char ch = static_cast<unsigned char>(*(begin++)) % 32;
      return okByte(ch, static_cast<unsigned short>(begin - old));
    }
    return err(EscapeSeqResult::UNKNOWN, static_cast<unsigned short>(begin - old));
  case 'e':
  case 'E':
    return ok('\033');
  case 'f':
    return ok('\f');
  case 'n':
    return ok('\n');
  case 'r':
    return ok('\r');
  case 't':
    return ok('\t');
  case 'v':
    return ok('\v');
  case 'x':
  case 'u':
  case 'U': {
    if (begin == end || !isHex(*begin)) {
      return err(EscapeSeqResult::NEED_CHARS, static_cast<unsigned short>(begin - old));
    }
    unsigned int limit = next == 'x' ? 2 : next == 'u' ? 4 : 8;
    unsigned int code = hexToNum(*(begin++));
    for (unsigned int i = 1; i < limit; i++) {
      if (begin != end && isHex(*begin)) {
        code *= 16;
        code += hexToNum(*(begin++));
      } else {
        break;
      }
    }
    if (limit == 2) { // byte
      assert(code <= UINT8_MAX);
      return okByte(static_cast<unsigned char>(code), static_cast<unsigned short>(begin - old));
    } else if (code <= 0x10FFFF) {
      return ok(static_cast<int>(code), static_cast<unsigned short>(begin - old));
    } else {
      return err(EscapeSeqResult::RANGE, static_cast<unsigned short>(begin - old));
    }
  }
  default:
    if (!isOctal(next) || (hasFlag(option, EscapeSeqOption::NEED_OCTAL_PREFIX) && next != '0')) {
      return err(EscapeSeqResult::UNKNOWN, static_cast<unsigned short>(begin - old));
    }
    unsigned int code = next - '0';
    for (unsigned int i = hasFlag(option, EscapeSeqOption::NEED_OCTAL_PREFIX) ? 0 : 1; i < 3; i++) {
      if (begin != end && isOctal(*begin)) {
        code *= 8;
        code += *(begin++) - '0';
      } else {
        break;
      }
    }
    return ok(static_cast<int>(code & 0xFF), static_cast<unsigned short>(begin - old));
  }
}

} // namespace arsh