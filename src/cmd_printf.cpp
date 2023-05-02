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

#include "cmd.h"
#include "misc/num_util.hpp"
#include "vm.h"

namespace ydsh {

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

  size_t getRemainSize() const { return this->getBufSize() - this->getUsedSize(); }

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

  bool resize(size_t afterBufSize) {
    if (afterBufSize > this->getBufSize()) {
      if (likely(afterBufSize <= SYS_LIMIT_STRING_MAX)) {
        this->value.resize(afterBufSize, '\0');
      } else {
        errno = ENOMEM;
        return false;
      }
    }
    return true;
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
  FILE *fp; // do not close it
  std::string error;
  const bool useBuf;
  bool restoreLocale{false};

public:
  FormatPrinter(StringRef format, bool useBuf) : format(format), useBuf(useBuf) {
    this->setOutput(stdout);
  }

  ~FormatPrinter() {
    if (this->restoreLocale) {
      setlocale(LC_NUMERIC, "C"); // reset locale
    }
  }

  void setOutput(FILE *f) { this->fp = f; }

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
    int errNum = errno;
    if (unlikely(!status)) {
      this->error = "format failed";
      if (errNum != 0) {
        this->error += ", caused by `";
        this->error += strerror(errNum);
        this->error += "'";
      }
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

  void numFormatError(StringRef invalidNum, char conversion, StringRef message) {
    this->error = "`";
    this->error += invalidNum;
    this->error += "': `";
    this->error += conversion;
    this->error += "' specifier ";
    this->error += message;
  }

#define GEN_FLAG_CASE(E, F, C)                                                                     \
  case C:                                                                                          \
    setFlag(flags, FormatFlag::E);                                                                 \
    continue;

  FormatFlag parseFlags(StringRef::size_type &pos) {
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
  void consumeLengthModifier(StringRef::size_type &pos) {
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
      this->numFormatError(toPrintable(ref), conversion,
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
      this->numFormatError(toPrintable(ref), conversion, "needs valid float number");
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
      if (!this->strBuf.resize(this->strBuf.getBufSize() + retSize + 64)) {
        errNum = errno;
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
    this->error = "format failed";
    if (errNum != 0) { // snprintf may not set errno
      this->error += ", caused by `";
      this->error += strerror(errNum);
      this->error += "'";
    }
  }
  return ret >= 0;
}

bool FormatPrinter::appendWithPadding(const int width, StringRef ref, const int precision,
                                      const bool leftAdjust) {
  assert(width > -1);

  const size_t fieldWidth = static_cast<unsigned int>(width);
  auto endIter = ref.begin();
  const size_t count = iterateGraphemeUntil(
      ref, static_cast<size_t>(precision < 0 ? -1 : precision),
      [&endIter](const GraphemeScanner::Result &grapheme) { endIter = grapheme.ref.end(); });
  assert(endIter >= ref.begin());
  ref = ref.substr(0, endIter - ref.begin());

  static constexpr bool end = false; // for TRY macro
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

  for (size_t i = 0; i < size / chunkSize; i++) {
    TRY(this->append(spaces));
  }
  for (size_t i = 0; i < size % chunkSize; i++) {
    TRY(this->append(" "));
  }
  return true;
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
    default:
      this->error = "`";
      this->error += this->format.slice(pos, pos + 1);
      this->error += "': invalid conversion specifier";
      return end;
    }
  }
  return directiveCount == 0 ? end : begin;
}

int builtin_echo(DSState &, ArrayObject &argvObj) {
  bool newline = true;
  bool interpEscape = false;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "neE")) != -1;) {
    switch (opt) {
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
      goto DO_ECHO;
    }
  }

DO_ECHO:
  // print argument
  if (optState.index > 1 && argvObj.getValues()[optState.index - 1].asStrRef() == "--") {
    optState.index--;
  }

  unsigned int index = optState.index;
  const unsigned int argc = argvObj.getValues().size();
  bool firstArg = true;
  for (; index < argc; index++) {
    if (firstArg) {
      firstArg = false;
    } else {
      fputc(' ', stdout);
    }

    auto arg = argvObj.getValues()[index].asStrRef();
    if (interpEscape) {
      bool r = interpretEscapeSeq(arg, [](StringRef sub) {
        return fwrite(sub.data(), sizeof(char), sub.size(), stdout) == sub.size();
      });
      if (!r) {
        goto END;
      }
    } else {
      fwrite(arg.data(), sizeof(char), arg.size(), stdout);
    }
  }

  if (newline) {
    fputc('\n', stdout);
  }

END:
  return 0; // FIXME: io error check
}

int builtin_printf(DSState &state, ArrayObject &argvObj) {
  GetOptState optState;
  bool setVar = false;
  StringRef target;
  for (int opt; (opt = optState(argvObj, ":v:h")) != -1;) {
    switch (opt) {
    case 'v':
      setVar = true;
      target = optState.optArg;
      break;
    case 'h':
      return showHelp(argvObj);
    case ':':
      ERROR(argvObj, "-%c: option requires argument", optState.optOpt);
      return 1;
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  if (setVar && target.empty()) {
    ERROR(argvObj, "must be valid identifier"); // FIXME: check var name format?
    return 1;
  }

  const unsigned int index = optState.index;
  if (index == argvObj.size()) {
    ERROR(argvObj, "need format string");
    return showUsage(argvObj);
  }

  FormatPrinter printer(argvObj.getValues()[index].asStrRef(), setVar);
#ifdef FUZZING_BUILD_MODE
  auto nullFIle = createFilePtr(fopen, "/dev/null", "w");
  if (getenv("YDSH_PRINTF_FUZZ")) {
    printer.setOutput(nullFIle.get());
  }
#endif

  auto &reply = typeAs<MapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR));
  if (setVar) {
    if (unlikely(!reply.checkIteratorInvalidation(state, true))) {
      return 1;
    }
  }

  auto begin = argvObj.getValues().begin() + (index + 1);
  const auto end = argvObj.getValues().end();
  do {
    begin = printer(begin, end);
    if (!printer.getError().empty()) {
#ifdef FUZZING_BUILD_MODE
      if (getenv("YDSH_PRINTF_FUZZ")) {
        return 1; // ignore error message
      }
#endif

      ERROR(argvObj, "%s", printer.getError().c_str());
      return 1;
    }
  } while (begin != end);

  if (setVar && !state.hasError()) {
    reply.set(DSValue::createStr(target), DSValue::createStr(std::move(printer).takeBuf()));
  }
  return 0;
}

} // namespace ydsh