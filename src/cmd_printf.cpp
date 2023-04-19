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

#include "cmd.h"
#include "vm.h"

namespace ydsh {

enum class FormatFlag {
  ALTER_FORM = 1u << 0u,  // #
  ZERO_PAD = 1u << 1u,    // 0
  LEFT_ADJUST = 1u << 2u, // -
  SPACE = 1u << 3u,       // ' ' (a space)
  SIGN = 1u << 4u,        // +
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
  std::string error;

public:
  explicit FormatPrinter(StringRef format) : format(format) {
    setlocale(LC_NUMERIC, ""); // printf should use current locale setting specified by env
  }

  ~FormatPrinter() {
    setlocale(LC_NUMERIC, "C"); // reset locale
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

  FormatFlag parseFlags(StringRef::size_type &pos) {
    FormatFlag flags{};
    for (const StringRef::size_type size = this->format.size(); pos < size; pos++) {
      switch (this->format[pos]) {
      case '#':
        setFlag(flags, FormatFlag::ALTER_FORM);
        continue;
      case '0':
        setFlag(flags, FormatFlag::ZERO_PAD);
        continue;
      case '-':
        setFlag(flags, FormatFlag::LEFT_ADJUST);
        continue;
      case ' ':
        setFlag(flags, FormatFlag::SPACE);
        continue;
      case '+':
        setFlag(flags, FormatFlag::SIGN);
        continue;
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
      if (this->format[pos + 1] == 'c') {
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

    char next = this->format[pos];
    switch (next) {
    case 's': {
      StringRef ref;
      if (begin != end) {
        ref = (*begin++).asStrRef();
      }
      TRY(this->append(ref));
      pos++;
      continue;
    }
    case 'b': {
      StringRef ref;
      if (begin != end) {
        ref = (*begin++).asStrRef();
      }
      TRY(this->appendAndInterpretEscape(ref));
      pos++;
      continue;
    }
    case 'q': {
      StringRef ref;
      if (begin != end) {
        ref = (*begin++).asStrRef();
      }
      auto str = quoteAsShellArg(ref);
      TRY(this->append(str));
      pos++;
      continue;
    }
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