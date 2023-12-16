/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include <cstdarg>
#include <ctime>

#include <langinfo.h>

#include "cmd.h"
#include "misc/num_util.hpp"
#include "misc/time_util.hpp"
#include "ordered_map.h"
#include "vm.h"

namespace arsh {

template <typename T>
static constexpr bool interpret_consumer_requirement_v =
    std::is_same_v<bool, std::invoke_result_t<T, StringRef>>;

template <typename T, enable_when<interpret_consumer_requirement_v<T>> = nullptr>
static bool interpretEscapeSeq(const StringRef ref, T callback) {
  const auto size = ref.size();
  for (StringRef::size_type pos = 0; pos < size;) {
    const auto retPos = ref.find('\\', pos);
    const auto sub = ref.slice(pos, retPos);
    if (!callback(sub)) {
      return false;
    }
    if (retPos == StringRef::npos) {
      break;
    }
    pos = retPos;
    auto ret = parseEscapeSeq(ref.begin() + pos, ref.end(), true);
    switch (ret.kind) {
    case EscapeSeqResult::OK_CODE: {
      char buf[5];
      unsigned int byteSize = UnicodeUtil::codePointToUtf8(ret.codePoint, buf);
      if (!callback(StringRef(buf, byteSize))) {
        return false;
      }
      pos += ret.consumedSize;
      continue;
    }
    case EscapeSeqResult::OK_BYTE: {
      auto b = static_cast<unsigned int>(ret.codePoint);
      char buf[1];
      buf[0] = static_cast<char>(static_cast<unsigned char>(b));
      if (!callback(StringRef(buf, 1))) {
        return false;
      }
      pos += ret.consumedSize;
      continue;
    }
    case EscapeSeqResult::RANGE:
      pos += ret.consumedSize; // skip invalid code
      continue;
    case EscapeSeqResult::UNKNOWN:
      if (ref[pos + 1] == 'c') {
        return false; // stop further printing
      }
      break;
    default:
      break;
    }
    char buf[1];
    buf[0] = ref[pos];
    if (!callback(StringRef(buf, 1))) {
      return false;
    }
    pos++;
  }
  return true;
}

class StringBuf {
private:
  std::string value;
  size_t usedSize{0};

public:
  size_t getBufSize() const { return this->value.size(); }

  size_t getUsedSize() const { return this->usedSize; }

  size_t getRemainSize() const {
    assert(this->getBufSize() >= this->getUsedSize());
    return this->getBufSize() - this->getUsedSize();
  }

  void consume(size_t consumedSize) { this->usedSize += consumedSize; }

  char *getBuf() { return this->value.data() + this->getUsedSize(); }

  bool append(StringRef ref) {
    if (likely(ref.size() <= SYS_LIMIT_STRING_MAX &&
               this->usedSize <= SYS_LIMIT_STRING_MAX - ref.size())) {
      this->value.resize(this->usedSize);
      this->value += ref;
      this->usedSize = this->value.size();
      this->value.resize(this->usedSize + 32, '\0');
      return true;
    }
    errno = ENOMEM;
    return false;
  }

  bool expand(size_t additionalSize) {
    if (additionalSize <= SYS_LIMIT_STRING_MAX &&
        this->getBufSize() <= SYS_LIMIT_STRING_MAX - additionalSize) {
      size_t newSize = this->getBufSize() + additionalSize;
      this->value.resize(newSize, '\0');
      return true;
    }
    errno = ENOMEM;
    return false;
  }

  std::string take() {
    assert(this->getUsedSize() <= this->getBufSize());
    this->value.resize(this->getUsedSize());
    this->usedSize = 0;
    std::string tmp;
    std::swap(tmp, this->value);
    return tmp;
  }

  const std::string &getValue() const { return this->value; }

  void reset() { this->usedSize = 0; }
};

#define EACH_FORMAT_FLAG(OP)                                                                       \
  OP(ALTER_FORM, (1u << 0u), '#')                                                                  \
  OP(ZERO_PAD, (1u << 1u), '0')                                                                    \
  OP(LEFT_ADJUST, (1u << 2u), '-')                                                                 \
  OP(SPACE, (1u << 3u), ' ')                                                                       \
  OP(SIGN, (1u << 4u), '+')

enum class FormatFlag {
#define GEN_ENUM(E, F, C) E = (F),
  EACH_FORMAT_FLAG(GEN_ENUM)
#undef GEN_ENUM
};

template <>
struct allow_enum_bitop<FormatFlag> : std::true_type {};

/**
 * printf implementation
 */
class FormatPrinter {
private:
  const StringRef format;
  StringBuf strBuf;
  FILE *fp{nullptr}; // do not close it
  std::string error;
  const timestamp initTimestamp;
  const bool useBuf;
  bool restoreLocale{false};
  bool supportPlusTimeFormat{false};

public:
  FormatPrinter(StringRef format, timestamp init, bool useBuf)
      : format(format), initTimestamp(init), useBuf(useBuf) {
    this->setOutput(stdout);
    setlocale(LC_TIME, ""); // always sync LC_TIME
  }

  ~FormatPrinter() {
    if (this->restoreLocale) {
      setlocale(LC_NUMERIC, "C"); // reset locale
    }
  }

  void setOutput(FILE *f) { this->fp = f; }

  void setPlusFormat(bool set) { this->supportPlusTimeFormat = set; }

  std::string takeBuf() && { return std::move(this->strBuf).take(); }

  const auto &getError() const { return this->error; }

  /**
   * formant and print string
   * @param begin
   * @param end
   * @return
   */
  ArrayObject::IterType operator()(ArrayObject::IterType begin, ArrayObject::IterType end);

private:
  void syncLocale() {
    if (!this->restoreLocale) {
      this->restoreLocale = true;
      setlocale(LC_NUMERIC, ""); // printf should use current locale setting specified by env
    }
  }

  bool append(StringRef ref) {
    bool status;
    errno = 0;
    if (this->useBuf) {
      status = this->strBuf.append(ref);
    } else {
      status = fwrite(ref.data(), sizeof(char), ref.size(), this->fp) == ref.size();
    }
    if (unlikely(!status)) {
      this->formatError(errno);
    }
    return status;
  }

  /**
   *
   * @param ref
   * @return
   * if error, return 0
   * if success, return 1
   * if stop printing, return -1
   */
  bool appendAndInterpretEscape(StringRef ref) {
    return interpretEscapeSeq(ref, [this](StringRef sub) { return this->append(sub); });
  }

  void numberError(StringRef invalidNum, char conversion, StringRef message) {
    this->error = "`";
    this->error += invalidNum;
    this->error += "': `";
    this->error += conversion;
    this->error += "' specifier ";
    this->error += message;
  }

  void formatError(int errNum) {
    this->error = "format failed";
    if (errNum != 0) {
      this->error += ", caused by `";
      this->error += strerror(errNum);
      this->error += "'";
    }
  }

#define GEN_FLAG_CASE(E, F, C)                                                                     \
  case C:                                                                                          \
    setFlag(flags, FormatFlag::E);                                                                 \
    continue;

  FormatFlag parseFlags(StringRef::size_type &pos) const {
    FormatFlag flags{};
    for (const StringRef::size_type size = this->format.size(); pos < size; pos++) {
      switch (this->format[pos]) {
        // clang-format off
      EACH_FORMAT_FLAG(GEN_FLAG_CASE)
        // clang-format on
      default:
        break;
      }
      break;
    }
    return flags;
  }

  /**
   * ignore any length modifiers
   * @param pos
   */
  void consumeLengthModifier(StringRef::size_type &pos) const {
    for (const StringRef::size_type size = this->format.size(); pos < size; pos++) {
      switch (this->format[pos]) {
      case 'h':
      case 'l':
      case 'L':
      case 'j':
      case 'z':
      case 'Z':
      case 't':
        continue;
      default:
        return;
      }
    }
  }

  bool parseInt32(ArrayObject::IterType &begin, ArrayObject::IterType end, int &value) {
    if (begin != end) {
      auto ref = (*begin++).asStrRef();
      auto ret = convertToNum<int>(ref.begin(), ref.end(), 0);
      if (!ret) {
        this->error = "`";
        this->error += toPrintable(ref);
        this->error += "': invalid number, must be INT32";
        return false;
      }
      value = ret.value;
    }
    return true;
  }

  bool parseDecimal(StringRef ref, int &value) {
    auto ret = convertToDecimal<int>(ref.begin(), ref.end());
    if (!ret) {
      this->error = "`";
      this->error += toPrintable(ref);
      this->error += "': invalid number, must be decimal INT32";
      return false;
    }
    value = ret.value;
    return true;
  }

  Optional<int> parseWidth(StringRef::size_type &pos, ArrayObject::IterType &begin,
                           ArrayObject::IterType end);

  Optional<int> parsePrecision(StringRef::size_type &pos, ArrayObject::IterType &begin,
                               ArrayObject::IterType end);

  bool appendAsStr(FormatFlag flags, int width, int precision, char conversion,
                   ArrayObject::IterType &begin, ArrayObject::IterType end);

  bool appendAsInt(FormatFlag flags, int width, int precision, char conversion,
                   ArrayObject::IterType &begin, ArrayObject::IterType end);

  bool appendAsFloat(FormatFlag flags, int width, int precision, char conversion,
                     ArrayObject::IterType &begin, ArrayObject::IterType end);

  bool appendAsFormat(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  /**
   *
   * @param width
   * must not be negative number
   * @param ref
   * @param precision
   * @param leftAdjust
   * if true, add padding to right
   * @return
   */
  bool appendWithPadding(int width, StringRef ref, int precision, bool leftAdjust);

  bool putPadding(size_t size);

  bool appendAsTimeFormat(FormatFlag flags, int width, int precision, StringRef timeFormat,
                          ArrayObject::IterType &begin, ArrayObject::IterType endIter);
};

// ###########################
// ##     FormatPrinter     ##
// ###########################

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return end;                                                                                  \
    }                                                                                              \
  } while (false)

Optional<int> FormatPrinter::parseWidth(StringRef::size_type &pos, ArrayObject::IterType &begin,
                                        ArrayObject::IterType end) {
  int v = 0;
  if (pos == this->format.size()) {
    return 0;
  }
  if (this->format[pos] == '*') {
    pos++;
    if (!this->parseInt32(begin, end, v)) {
      return {};
    }
  } else {
    auto oldPos = pos;
    for (; pos < this->format.size(); pos++) {
      char ch = this->format[pos];
      if (ch >= '0' && ch <= '9') {
        continue;
      }
      break;
    }
    auto ref = this->format.slice(oldPos, pos);
    if (!ref.empty() && !this->parseDecimal(ref, v)) {
      return {};
    }
  }
  if (v == INT32_MIN) {
    this->error = "INT32_MIN is not allowed in width";
    return {};
  }
  return v;
}

Optional<int> FormatPrinter::parsePrecision(StringRef::size_type &pos, ArrayObject::IterType &begin,
                                            ArrayObject::IterType end) {
  const auto size = this->format.size();
  int v = -1;
  if (pos < size && this->format[pos] == '.') {
    pos++;
    v = 0;
    if (pos < size && this->format[pos] == '*') {
      pos++;
      if (!this->parseInt32(begin, end, v)) {
        return {};
      }
    } else {
      auto oldPos = pos;
      for (; pos < this->format.size(); pos++) {
        char ch = this->format[pos];
        if (ch >= '0' && ch <= '9') {
          continue;
        }
        break;
      }
      auto ref = this->format.slice(oldPos, pos);
      if (!ref.empty() && !this->parseDecimal(ref, v)) {
        return {};
      }
    }
  }
  return v;
}

bool FormatPrinter::appendAsStr(FormatFlag flags, int width, int precision, char conversion,
                                ArrayObject::IterType &begin, const ArrayObject::IterType end) {
  assert(StringRef("csbq").contains(conversion));
  StringRef ref;
  if (begin != end) {
    ref = (*begin++).asStrRef();
  }
  const bool leftAdjust = hasFlag(flags, FormatFlag::LEFT_ADJUST);
  switch (conversion) {
  case 'c':
    return this->appendWithPadding(width, ref, 1, leftAdjust);
  case 's':
    return this->appendWithPadding(width, ref, precision, leftAdjust);
  case 'b':
    if (width == 0 && precision < 0) { // fast path
      return this->appendAndInterpretEscape(ref);
    } else {
      std::string str;
      bool r = interpretEscapeSeq(ref, [&str](StringRef sub) {
        if (sub.size() <= SYS_LIMIT_STRING_MAX && str.size() <= SYS_LIMIT_STRING_MAX - sub.size()) {
          str += sub;
          return true;
        } else {
          errno = ENOMEM;
          return false;
        }
      });
      return r && this->appendWithPadding(width, str, precision, leftAdjust);
    }
  case 'q': {
    auto str = quoteAsShellArg(ref);
    return this->appendWithPadding(width, str, precision, leftAdjust);
  }
  default:
    return true; // normally unreachable
  }
}

#define GEN_IF(E, F, C)                                                                            \
  if (hasFlag(flags, FormatFlag::E)) {                                                             \
    fmt += (C);                                                                                    \
  }

bool FormatPrinter::appendAsInt(FormatFlag flags, int width, int precision, char conversion,
                                ArrayObject::IterType &begin, const ArrayObject::IterType end) {
  static_assert(sizeof(int64_t) == sizeof(intmax_t));

  assert(StringRef("diouxX").contains(conversion));
  this->syncLocale();

  std::string fmt = "%";

  EACH_FORMAT_FLAG(GEN_IF);

  fmt += "*.*j";
  fmt += conversion;

  int64_t v = 0;
  if (begin != end) {
    auto ref = (*begin++).asStrRef();
    auto pair = convertToNum<int64_t>(ref.begin(), ref.end(), 0);
    if (pair) {
      v = pair.value; // FIXME: error reporting
    } else {
      this->numberError(toPrintable(ref), conversion,
                        "needs valid INT64 (decimal, octal or hex number)");
      return false;
    }
  }
  return this->appendAsFormat(fmt.c_str(), width, precision, v);
}

bool FormatPrinter::appendAsFloat(FormatFlag flags, int width, int precision, char conversion,
                                  ArrayObject::IterType &begin, ArrayObject::IterType end) {
  assert(StringRef("eEfFgGaA").contains(conversion));
  this->syncLocale();

  double v = 0.0;
  if (begin != end) {
    auto ref = (*begin++).asStrRef();
    bool fail = true;
    if (!ref.hasNullChar()) {
      auto ret = convertToDouble(ref.data(), false);
      if (ret) {
        fail = false;
        v = ret.value;
      } // FIXME: error reporting
    }
    if (fail) {
      this->numberError(toPrintable(ref), conversion, "needs valid float number");
      return false;
    }
    if (std::isnan(v)) {
      static_assert(std::numeric_limits<double>::has_quiet_NaN);
      v = std::numeric_limits<double>::quiet_NaN();
      unsetFlag(flags, FormatFlag::SIGN);
      unsetFlag(flags, FormatFlag::SPACE);
    }
  }

  std::string fmt = "%";
  EACH_FORMAT_FLAG(GEN_IF);
  fmt += "*.*";
  fmt += conversion;
  return this->appendAsFormat(fmt.c_str(), width, precision, v);
}

bool FormatPrinter::appendAsFormat(const char *fmt, ...) {
  int errNum;
  int ret;
  if (this->useBuf) {
    while (true) {
      va_list arg;
      va_start(arg, fmt);
      errno = 0;
      ret = vsnprintf(this->strBuf.getBuf(), this->strBuf.getRemainSize(), fmt, arg);
      errNum = errno;
      va_end(arg);
      if (ret < 0) {
        break;
      }
      const auto retSize = static_cast<unsigned int>(ret);
      if (retSize < this->strBuf.getRemainSize()) {
        this->strBuf.consume(retSize);
        break;
      }
      if (!this->strBuf.expand(retSize + 64)) {
        errNum = errno;
        ret = -1;
        break;
      }
    }
  } else {
    va_list arg;
    va_start(arg, fmt);
    errno = 0;
    ret = vfprintf(this->fp, fmt, arg);
    errNum = errno;
    va_end(arg);
  }

  if (ret < 0) {
    this->formatError(errNum);
  }
  return ret >= 0;
}

bool FormatPrinter::appendWithPadding(const int width, StringRef ref, const int precision,
                                      const bool leftAdjust) {
  assert(width > -1);

  const size_t fieldWidth = static_cast<unsigned int>(width);
  auto endIter = ref.end();
  size_t count = ref.size();
  if (precision > -1 || fieldWidth != 0) {
    endIter = ref.begin();
    count = iterateGraphemeUntil(
        ref, static_cast<size_t>(precision),
        [&endIter](const GraphemeCluster &grapheme) { endIter = grapheme.getRef().end(); });
  }
  assert(endIter >= ref.begin());
  ref = ref.substr(0, endIter - ref.begin());

  constexpr bool end = false; // for TRY macro
  const bool needPadding = fieldWidth > count;

  if (needPadding && !leftAdjust) {
    TRY(this->putPadding(fieldWidth - count));
  }
  TRY(this->append(ref));
  if (needPadding && leftAdjust) {
    TRY(this->putPadding(fieldWidth - count));
  }
  return true;
}

bool FormatPrinter::putPadding(size_t size) {
  constexpr bool end = false;

  const size_t chunkSize = std::min(size, static_cast<size_t>(64));
  std::string spaces;
  spaces.resize(chunkSize, ' ');

  size_t limit = size / chunkSize;
  for (size_t i = 0; i < limit; i++) {
    TRY(this->append(spaces));
  }
  limit = size % chunkSize;
  for (size_t i = 0; i < limit; i++) {
    TRY(this->append(" "));
  }
  return true;
}

static bool checkAltSymbolsImpl(char conversion, const StringRef alt, char next,
                                std::string &error) {
  assert(conversion == 'E' || conversion == 'O');
  if (alt.contains(next)) {
    return true;
  }
  error = "need one of ";
  for (StringRef::size_type i = 0; i < alt.size(); i++) {
    if (i > 0) {
      error += ", ";
    }
    error += '`';
    error += alt[i];
    error += '\'';
  }
  error += " specifiers after `";
  error += conversion;
  error += "'";
  return false;
}

static bool checkAltSymbols(const StringRef format, StringRef::size_type pos, std::string &error) {
  char conversion = format[pos];
  char next = '\0';
  if (pos + 1 < format.size()) {
    next = format[pos + 1];
  }
  StringRef alt;
  if (conversion == 'E') {
    alt = "cCxXyY";
  } else if (conversion == 'O') {
    alt = "deHImMSuUVwWy";
  }
  return checkAltSymbolsImpl(conversion, alt, next, error);
}

static bool putTime(StringBuf &out, const char *fmt, const struct tm &tm) {
  char buf[256]; // FIXME: check actual required buffer size
  auto s = strftime(buf, std::size(buf), fmt, &tm);
  return out.append(StringRef(buf, s));
}

static bool interpretTimeFormat(StringBuf &out, const StringRef format, bool plusFormat,
                                const struct tm &tm, const long nanoSec, std::string &error) {
  constexpr bool end = false; // for TRY macro
  const auto size = format.size();
  for (StringRef::size_type pos = 0; pos < size; pos++) {
    const auto ret = format.find('%', pos);
    const auto sub = format.slice(pos, ret);
    TRY(out.append(sub));
    if (ret == StringRef::npos) {
      break;
    }
    pos = ret + 1;
    if (pos == size) {
      error = "require at-least one conversion specifier after %";
      return false;
    }

    const char conversion = format[pos];
    switch (conversion) {
    case 'a':
    case 'A':
    case 'b':
    case 'B':
    case 'c':
    case 'C':
    case 'd':
    case 'D':
    case 'e':
    case 'E':
    case 'F':
    case 'G':
    case 'g':
    case 'h':
    case 'H':
    case 'I':
    case 'j':
    case 'm':
    case 'M':
    case 'n':
    case 'O':
    case 'p':
    case 'r':
    case 'R':
    case 's':
    case 'S':
    case 't':
    case 'T':
    case 'u':
    case 'U':
    case 'V':
    case 'w':
    case 'W':
    case 'x':
    case 'X':
    case 'y':
    case 'Y':
    case 'z':
    case 'Z': {
      std::string fmt = "%";
      fmt += conversion;
      if (conversion == 'E' || conversion == 'O') {
        TRY(checkAltSymbols(format, pos, error));
        pos++;
        fmt += format[pos];
      }
      TRY(putTime(out, fmt.c_str(), tm));
      continue;
    }
    case 'k': {              // for musl (not implemented)
      int hour = tm.tm_hour; // 0-23 with blank if single digits
      char b[8];
      int s = snprintf(b, std::size(b), "%2d", hour);
      assert(s == 2);
      TRY(out.append(StringRef(b, static_cast<size_t>(s))));
      continue;
    }
    case 'l': {              // for musl (not implemented)
      int hour = tm.tm_hour; // 1-12 with blank if single digits
      if (hour == 0) {
        hour = 12;
      } else if (hour > 12) {
        hour -= 12;
      }
      char b[8];
      int s = snprintf(b, std::size(b), "%2d", hour);
      assert(s == 2);
      TRY(out.append(StringRef(b, static_cast<size_t>(s))));
      continue;
    }
    case 'N': {
      char b[16];
      int s = snprintf(b, std::size(b), "%09u", static_cast<unsigned int>(nanoSec));
      assert(s == 9);
      TRY(out.append(StringRef(b, static_cast<size_t>(s))));
      continue;
    }
    case '+': {
      if (plusFormat) {
        TRY(putTime(out, "%+", tm));
      } else {
        std::string fmt;
#ifdef _DATE_FMT // for glibc
        fmt = nl_langinfo(_DATE_FMT);
#endif
        if (fmt.empty()) {
          fmt = "%a %b %e %H:%M:%S %Z %Y";
        }
        TRY(interpretTimeFormat(out, fmt, plusFormat, tm, nanoSec, error));
      }
      continue;
    }
    case '%':
      TRY(out.append("%"));
      continue;
    default:
      error = "`";
      error += toPrintable(format.slice(pos, pos + 1));
      error += "': invalid time conversion specifier";
      return false;
    }
  }
  return true;
}

bool FormatPrinter::appendAsTimeFormat(FormatFlag flags, int width, int precision,
                                       StringRef timeFormat, ArrayObject::IterType &begin,
                                       ArrayObject::IterType endIter) {
  timespec targetTime{};
  if (begin != endIter) {
    auto ref = (*begin++).asStrRef();
    auto s = parseUnixTimeWithNanoSec(ref.begin(), ref.end(), targetTime);
    switch (s) {
    case ParseTimespecStatus::OK:
      break;
    case ParseTimespecStatus::INVALID_UNIX_TIME:
      this->numberError(toPrintable(ref), '(', "needs INT64 decimal number");
      return false;
    case ParseTimespecStatus::INVALID_NANO_SEC:
      this->numberError(toPrintable(ref), '(', "needs valid fractional part (0-999999999)");
      return false;
    }
    bool frac = ref.contains('.');
    if (targetTime.tv_sec == -1 && targetTime.tv_nsec == 0 && !frac) {
      targetTime = timestampToTimespec(getCurrentTimestamp()); // use current timestamp
    } else if (targetTime.tv_sec == -2 && targetTime.tv_nsec == 0 && !frac) {
      targetTime = timestampToTimespec(this->initTimestamp); // use startup timestamp
    }
  } else { // if no arg, get current timestamp
    targetTime = timestampToTimespec(getCurrentTimestamp());
  }

  struct tm tm {};
  tzset();
  errno = 0;
  if (!localtime_r(&targetTime.tv_sec, &tm)) {
    int e = errno;
    this->error = "localtime_r failed, caused by `";
    this->error += strerror(e);
    this->error += "'";
    return false;
  }

  StringBuf buf;
  if (timeFormat.empty()) {
    timeFormat = "%X";
  }
  if (!interpretTimeFormat(buf, timeFormat, this->supportPlusTimeFormat, tm, targetTime.tv_nsec,
                           this->error)) {
    if (this->error.empty()) {
      this->formatError(errno);
    }
    return false;
  }
  return this->appendWithPadding(width, buf.take(), precision,
                                 hasFlag(flags, FormatFlag::LEFT_ADJUST));
}

ArrayObject::IterType FormatPrinter::operator()(ArrayObject::IterType begin,
                                                const ArrayObject::IterType end) {
  this->error.clear();
  unsigned int directiveCount = 0;
  const size_t size = this->format.size();
  for (StringRef::size_type pos = 0; pos < size; pos++) {
    const auto ret = this->format.find('%', pos);
    const auto sub = this->format.slice(pos, ret);
    TRY(this->appendAndInterpretEscape(sub));
    if (ret == StringRef::npos) {
      break;
    }
    directiveCount++;
    pos = ret + 1;
    if (pos == size) {
      this->error = "require at-least one directive after %";
      return end;
    }
    if (this->format[pos] == '%') {
      directiveCount--;
      TRY(this->append("%"));
      continue;
    }

    auto flags = this->parseFlags(pos);
    const int width = ({
      auto r = this->parseWidth(pos, begin, end);
      if (!r.hasValue()) {
        return end;
      }
      auto v = r.unwrap();
      if (v < 0) {
        assert(v != INT32_MIN);
        setFlag(flags, FormatFlag::LEFT_ADJUST);
        v = -v;
      }
      v;
    });
    const int precision = ({
      auto r = this->parsePrecision(pos, begin, end);
      if (!r.hasValue()) {
        return end;
      }
      r.unwrap();
    });

    this->consumeLengthModifier(pos);

    if (pos == this->format.size()) {
      this->error = "`";
      this->error += this->format.substr(ret);
      this->error += "': missing conversion specifier";
      return end;
    }

    const char conversion = this->format[pos];
    switch (conversion) {
    case 'c':
    case 's':
    case 'b':
    case 'q':
      TRY(this->appendAsStr(flags, width, precision, conversion, begin, end));
      continue;
    case 'd':
    case 'i':
    case 'o':
    case 'u':
    case 'x':
    case 'X':
      TRY(this->appendAsInt(flags, width, precision, conversion, begin, end));
      continue;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
      TRY(this->appendAsFloat(flags, width, precision, conversion, begin, end));
      continue;
    case '(':
      if (auto r = this->format.find(")T", pos); r != StringRef::npos) {
        StringRef timeFormat = this->format.slice(pos + 1, r);
        TRY(this->appendAsTimeFormat(flags, width, precision, timeFormat, begin, end));
        pos = r + 1;
        continue;
      } else {
        this->error = "`(' specifier must end with `)T'";
        return end;
      }
    default:
      this->error = "`";
      this->error += toPrintable(this->format.slice(pos, pos + 1));
      this->error += "': invalid conversion specifier";
      return end;
    }
  }
  return directiveCount == 0 ? end : begin;
}

int builtin_echo(DSState &st, ArrayObject &argvObj) {
  bool newline = true;
  bool interpEscape = false;

  const unsigned int size = argvObj.getValues().size();
  unsigned int index = 1;
  for (; index < size; index++) {
    StringRef arg = argvObj.getValues()[index].asStrRef();

    if (arg.size() < 2 || arg[0] != '-') { // no options, such as 'a' '-' 'bb' ''
      goto DO_ECHO;
    }
    arg.removePrefix(1); // skip '-'

    // if invalid option, stop option parsing
    for (char ch : arg) {
      switch (ch) {
      case 'n':
      case 'e':
      case 'E':
        continue;
      default:
        goto DO_ECHO;
      }
    }

    // interpret options
    for (char ch : arg) {
      switch (ch) {
      case 'n':
        newline = false;
        break;
      case 'e':
        interpEscape = true;
        break;
      case 'E':
        interpEscape = false;
        break;
      default:
        break;
      }
    }
  }

DO_ECHO:
  bool firstArg = true;
  int errNum = 0;
  for (; index < size; index++) {
    errno = 0;
    if (firstArg) {
      firstArg = false;
    } else {
      if (fputc(' ', stdout) == EOF) {
        errNum = errno;
        goto END;
      }
    }

    auto arg = argvObj.getValues()[index].asStrRef();
    if (interpEscape) {
      bool r = interpretEscapeSeq(arg, [](StringRef sub) {
        return fwrite(sub.data(), sizeof(char), sub.size(), stdout) == sub.size();
      });
      if (!r) {
        errNum = errno;
        goto END;
      }
    } else {
      if (fwrite(arg.data(), sizeof(char), arg.size(), stdout) != arg.size()) {
        errNum = errno;
        goto END;
      }
    }
  }

  if (newline) {
    if (fputc('\n', stdout) == EOF) {
      errNum = errno;
      goto END;
    }
  }

END:
  CHECK_STDOUT_ERROR(st, argvObj, errNum);
  return 0;
}

int builtin_printf(DSState &state, ArrayObject &argvObj) {
  GetOptState optState(":v:h");
  bool setVar = false;
  StringRef target;
  for (int opt; (opt = optState(argvObj)) != -1;) {
    switch (opt) {
    case 'v':
      setVar = true;
      target = optState.optArg;
      break;
    case 'h':
      return showHelp(argvObj);
    case ':':
      ERROR(state, argvObj, "-%c: option requires argument", optState.optOpt);
      return 1;
    default:
      return invalidOptionError(state, argvObj, optState);
    }
  }

  if (setVar && target.empty()) {
    ERROR(state, argvObj, "must be valid identifier"); // FIXME: check var name format?
    return 1;
  }

  const unsigned int index = optState.index;
  if (index == argvObj.size()) {
    ERROR(state, argvObj, "need format string");
    return showUsage(argvObj);
  }

  FormatPrinter printer(argvObj.getValues()[index].asStrRef(), state.initTime, setVar);
  printer.setPlusFormat(state.support_strftime_plus);

#ifdef FUZZING_BUILD_MODE
  auto nullFIle = createFilePtr(fopen, "/dev/null", "w");
  if (getenv("ARSH_PRINTF_FUZZ")) {
    printer.setOutput(nullFIle.get());
  }
#endif

  if (setVar) {
    reassignReplyVar(state);
  }

  auto begin = argvObj.getValues().begin() + (index + 1);
  const auto end = argvObj.getValues().end();
  do {
    begin = printer(begin, end);
    if (!printer.getError().empty()) {
#ifdef FUZZING_BUILD_MODE
      if (getenv("ARSH_PRINTF_FUZZ")) {
        return 1; // ignore error message
      }
#endif

      ERROR(state, argvObj, "%s", printer.getError().c_str());
      return 1;
    }
  } while (begin != end);

  if (setVar && !state.hasError()) {
    auto &reply = typeAs<OrderedMapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR));
    auto old = reply.put(state, DSValue::createStr(target),
                         DSValue::createStr(std::move(printer).takeBuf()));
    if (unlikely(!old)) {
      return 1;
    }
  }
  if (fflush(stdout) == EOF) {
    PERROR(state, argvObj, "io error");
    return 1;
  }
  return 0;
}

} // namespace arsh