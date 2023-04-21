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

class FormatBuf {
private:
  static constexpr unsigned int STATIC_BUF_SIZE = 32;

  unsigned int size{STATIC_BUF_SIZE};
  union {
    char data[STATIC_BUF_SIZE];
    char *ptr;
  };

public:
  FormatBuf() = default;

  ~FormatBuf() {
    if (this->heapUsed()) {
      free(this->ptr);
    }
  }

  bool heapUsed() const { return this->getBufSize() > STATIC_BUF_SIZE; }

  unsigned int getBufSize() const { return this->size; }

  char *getBuf() { return this->heapUsed() ? this->ptr : this->data; }

  /**
   * reserve at-least afterBufSize
   * @param afterBufSize
   */
  void resize(unsigned int afterBufSize) {
    if (afterBufSize > this->getBufSize() && afterBufSize > STATIC_BUF_SIZE) {
      if (!this->heapUsed()) {
        this->ptr = nullptr;
      }
      auto *r = realloc(this->ptr, sizeof(char) * afterBufSize);
      if (!r) {
        fatal_perror("alloc failed");
      }
      this->ptr = static_cast<char *>(r);
      this->size = afterBufSize;
    }
  }

  StringRef format(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list arg;

    va_start(arg, fmt);
    StringRef ref;
    while (true) {
      errno = 0;
      int ret = vsnprintf(this->getBuf(), this->getBufSize(), fmt, arg);
      if (ret < 0) {
        ref = nullptr;
        break;
      }
      const auto retSize = static_cast<unsigned int>(ret);
      if (retSize < this->getBufSize()) {
        ref = StringRef(this->getBuf(), retSize);
        break;
      }
      this->resize(retSize + 1);
    }
    va_end(arg);

    return ref;
  }
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
  std::function<bool(StringRef)> consumer;
  FormatBuf formatBuf;
  std::string error;
  bool restoreLocale{false};

public:
  explicit FormatPrinter(StringRef format) : format(format) {}

  ~FormatPrinter() {
    if (this->restoreLocale) {
      setlocale(LC_NUMERIC, "C"); // reset locale
    }
  }

  void setConsumer(std::function<bool(StringRef)> &&f) { this->consumer = std::move(f); }

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
    if (likely(this->consumer)) {
      if (!ref.empty()) {
        return this->consumer(ref);
      }
    }
    return true;
  }

  /**
   *
   * @param ref
   * @return
   * if error, return 0
   * if success, return 1
   * if stop printing, return -1
   */
  bool appendAndInterpretEscape(StringRef ref);

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

  bool appendAsStr(char conversion, ArrayObject::IterType &begin, ArrayObject::IterType end);

  bool appendAsInt(FormatFlag flags, char conversion, ArrayObject::IterType &begin,
                   ArrayObject::IterType end);

  bool appendAsFloat(FormatFlag flags, char conversion, ArrayObject::IterType &begin,
                     ArrayObject::IterType end);
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

bool FormatPrinter::appendAndInterpretEscape(const StringRef ref) {
  constexpr auto end = false; // dummy for TRY macro
  const auto size = ref.size();
  for (StringRef::size_type pos = 0; pos < size;) {
    const auto retPos = ref.find('\\', pos);
    const auto sub = ref.slice(pos, retPos);
    TRY(this->append(sub));
    if (retPos == StringRef::npos) {
      break;
    }
    pos = retPos;
    auto ret = parseEscapeSeq(ref.begin() + pos, ref.end(), true);
    switch (ret.kind) {
    case EscapeSeqResult::OK_CODE: {
      char buf[5];
      unsigned int byteSize = UnicodeUtil::codePointToUtf8(ret.codePoint, buf);
      TRY(this->append(StringRef(buf, byteSize)));
      pos += ret.consumedSize;
      continue;
    }
    case EscapeSeqResult::OK_BYTE: {
      auto b = static_cast<unsigned int>(ret.codePoint);
      char buf[1];
      buf[0] = static_cast<char>(static_cast<unsigned char>(b));
      TRY(this->append(StringRef(buf, 1)));
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
    TRY(this->append(StringRef(buf, 1)));
    pos++;
  }
  return true;
}

bool FormatPrinter::appendAsStr(char conversion, ArrayObject::IterType &begin,
                                const ArrayObject::IterType end) {
  assert(conversion == 'c' || conversion == 's' || conversion == 'b' || conversion == 'q');
  StringRef ref;
  if (begin != end) {
    ref = (*begin++).asStrRef();
  }
  switch (conversion) {
  case 'c': {
    StringRef c;
    iterateGraphemeUntil(ref, 1, [&c](const auto &grapheme) { c = grapheme.ref; });
    return this->append(c);
  }
  case 's':
    return this->append(ref);
  case 'b':
    return this->appendAndInterpretEscape(ref);
  case 'q': {
    auto str = quoteAsShellArg(ref);
    return this->append(str);
  }
  default:
    return true; // normally unreachable
  }
}

#define GEN_IF(E, F, C)                                                                            \
  if (hasFlag(flags, FormatFlag::E)) {                                                             \
    fmt += (C);                                                                                    \
  }

bool FormatPrinter::appendAsInt(FormatFlag flags, char conversion, ArrayObject::IterType &begin,
                                const ArrayObject::IterType end) {
  assert(StringRef("diouxX").contains(conversion));
  this->syncLocale();

  std::string fmt = "%";

  EACH_FORMAT_FLAG(GEN_IF);

  fmt += "j";
  fmt += conversion;

  int64_t v = 0;
  if (begin != end) {
    auto ref = (*begin++).asStrRef();
    auto pair = convertToNum<int64_t>(ref.begin(), ref.end(), 0);
    if (pair.second) {
      v = pair.first;
    } else {
      this->error = "`";
      this->error += toPrintable(ref);
      this->error += "': invalid number, must be octal, hex, decimal";
      return false;
    }
  }

  auto ret = this->formatBuf.format(fmt.c_str(), v);
  if (!ret.data()) {
    return false;
  }
  return this->append(ret);
}

bool FormatPrinter::appendAsFloat(FormatFlag flags, char conversion, ArrayObject::IterType &begin,
                                  ArrayObject::IterType end) {
  assert(StringRef("eEfFgGaA").contains(conversion));
  this->syncLocale();

  std::string fmt = "%";

  EACH_FORMAT_FLAG(GEN_IF);

  fmt += conversion;

  double v = 0.0;
  if (begin != end) {
    auto ref = (*begin++).asStrRef();
    bool fail = true;
    if (!ref.hasNullChar()) {
      auto pair = convertToDouble(ref.data(), false);
      if (pair.second == 0) {
        fail = false;
        v = pair.first;
      }
    }
    if (fail) {
      this->error = "`";
      this->error += toPrintable(ref);
      this->error += "': invalid float number";
      return false;
    }
  }

  auto ret = this->formatBuf.format(fmt.c_str(), v);
  if (!ret.data()) {
    return false;
  }
  return this->append(ret);
}

ArrayObject::IterType FormatPrinter::operator()(ArrayObject::IterType begin,
                                                const ArrayObject::IterType end) {
  this->error.clear();
  unsigned int directiveCount = 0;
  const size_t size = this->format.size();
  for (StringRef::size_type pos = 0; pos < size;) {
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
      pos++;
      continue;
    }

    auto flags = this->parseFlags(pos);
    (void)flags;

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
      TRY(this->appendAsStr(conversion, begin, end));
      pos++;
      continue;
    case 'd':
    case 'i':
    case 'o':
    case 'u':
    case 'x':
    case 'X':
      TRY(this->appendAsInt(flags, conversion, begin, end));
      pos++;
      continue;
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
      TRY(this->appendAsFloat(flags, conversion, begin, end));
      pos++;
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

  auto &reply = typeAs<MapObject>(state.getGlobal(BuiltinVarOffset::REPLY_VAR));
  auto out = DSValue::createStr(); // for -v option
  FormatPrinter printer(argvObj.getValues()[index].asStrRef());
  if (setVar) {
    if (unlikely(!reply.checkIteratorInvalidation(state, true))) {
      return 1;
    }
    reply.clear();
    printer.setConsumer(
        [&state, &out](StringRef ref) -> bool { return out.appendAsStr(state, ref); });
  } else {
    printer.setConsumer([](StringRef ref) -> bool {
      return fwrite(ref.data(), sizeof(char), ref.size(), stdout) == ref.size();
    });
  }

  auto begin = argvObj.getValues().begin() + (index + 1);
  const auto end = argvObj.getValues().end();
  do {
    begin = printer(begin, end);
    if (!printer.getError().empty()) {
      ERROR(argvObj, "%s", printer.getError().c_str());
      return 1;
    }
  } while (begin != end);

  if (setVar && !state.hasError()) {
    reply.set(DSValue::createStr(target), std::move(out));
  }
  return 0;
}

} // namespace ydsh