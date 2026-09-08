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

#include "match_context.h"
#include "misc/format.hpp"

namespace arsh::regex {

const char *toString(const MatchStatus s) {
  switch (s) {
  case MatchStatus::OK:
    break;
  case MatchStatus::FAIL:
    return "failed";
  case MatchStatus::INVALID_UTF8:
    return "input string is invalid UTF-8";
  case MatchStatus::INPUT_LIMIT:
    return "size of input string is too large";
  case MatchStatus::CANCEL:
    return "canceled";
  case MatchStatus::TIMEOUT:
    return "match timeout";
  case MatchStatus::STACK_LIMIT:
    return "stack depth reaches limit";
  case MatchStatus::INVALID_REPLACE_PATTERN:
  case MatchStatus::REPLACED_LIMIT:
    break;
  }
  return "";
}

MatchStatus match(const Regex &regex, const StringRef text, const unsigned int codePointOffset,
                  std::vector<Capture> &captures, const ObserverPtr<Timer> timer) {
  auto ctx = tryToCreateMatchContext(regex, text, codePointOffset, captures);
  if (!ctx) {
    return ctx.asErr();
  }
  return match(ctx.asOk(), timer);
}

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return MatchStatus::REPLACED_LIMIT;                                                          \
    }                                                                                              \
  } while (false)

static MatchStatus interpretReplacePattern(const MatchContext &ctx, const ReplaceParam &param) {
  for (size_t pos = 0;;) {
    auto retPos = param.replacement.find('$', pos);
    const auto sub = param.replacement.slice(pos, retPos);
    TRY(!param.consumer || param.consumer(sub));
    if (retPos == StringRef::npos) {
      break;
    }
    retPos++;
    if (retPos == param.replacement.size()) { // end with '$'
      if (param.err) {
        *param.err += "invalid replace pattern: `$'";
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    StringRef inserting;
    switch (param.replacement[retPos]) {
    case '$':
      inserting = "$";
      retPos++;
      break;
    case '&':
      inserting = param.text.substr(ctx.getCaptures()[0].offset, ctx.getCaptures()[0].size);
      retPos++;
      break;
    case '`':
      inserting = param.text.substr(0, ctx.getCaptures()[0].offset);
      retPos++;
      break;
    case '\'':
      inserting = param.text.substr(ctx.getCaptures()[0].endOffset());
      retPos++;
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
      const auto startPos = retPos++;
      while (retPos < param.replacement.size() && isDecimal(param.replacement[retPos])) {
        retPos++;
      }
      StringRef num = param.replacement.slice(startPos, retPos);
      if (auto ret = convertToNum10<unsigned int>(num.begin(), num.end());
          ret && ret.value <= ctx.getRegex().getCaptureGroupCount() && ret.value > 0) {
        if (auto cap = ctx.getCaptures()[ret.value]) {
          inserting = param.text.substr(cap.offset, cap.size);
        }
        break;
      }
      if (param.err) {
        *param.err += "undefined capture group index: `";
        *param.err += num;
        *param.err += '\'';
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    case '<': {
      retPos++;
      auto p = param.replacement.find('>', retPos);
      if (p == StringRef::npos) {
        if (param.err) {
          *param.err += "replace pattern `$<' must end with `>'";
        }
        return MatchStatus::INVALID_REPLACE_PATTERN;
      }
      auto name = param.replacement.slice(retPos, p);
      if (auto *e = ctx.getRegex().getNamedCaptureGroups().find(name)) {
        if (auto cap = ctx.resolveNamedBackRef(*e)) {
          inserting = param.text.substr(cap.offset, cap.size);
        }
        retPos = p + 1;
        break;
      }
      if (param.err) {
        *param.err += "undefined capture group name: `";
        *param.err += name;
        *param.err += '\'';
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    default:
      if (param.err) {
        *param.err += "invalid replace pattern: `$'";
      }
      return MatchStatus::INVALID_REPLACE_PATTERN;
    }
    TRY(!param.consumer || param.consumer(inserting));
    pos = retPos;
  }
  return MatchStatus::OK;
}

MatchStatus replace(const Regex &regex, const ReplaceParam &param, const ObserverPtr<Timer> timer) {
  std::vector<Capture> captures;
  auto ctx = tryToCreateMatchContext(regex, param.text, 0, captures);
  if (!ctx) {
    return ctx.asErr();
  }
  unsigned int matchStartOffset = 0;
  do {
    matchStartOffset = ctx.asOk().getInput().getOffset();
    const auto s = match(ctx.asOk(), timer);
    auto &input = ctx.asOk().refInput(); // input is updated after call match
    if (s == MatchStatus::FAIL) {
      input.setIter(input.getBegin() + matchStartOffset);
      break;
    }
    if (s != MatchStatus::OK) {
      return s;
    }
    TRY(!param.consumer || param.consumer(param.text.slice(matchStartOffset, captures[0].offset)));
    if (auto s2 = interpretReplacePattern(ctx.asOk(), param); s2 != MatchStatus::OK) {
      return s2;
    }
    if (input.available() && matchStartOffset == input.getOffset()) { // not consume input
      input.consumeForward();
      TRY(!param.consumer || param.consumer(StringRef(input.getBegin() + matchStartOffset,
                                                      input.getOffset() - matchStartOffset)));
    }
  } while (param.global && ctx.asOk().getInput().getOffset() != matchStartOffset);
  TRY(!param.consumer || param.consumer(ctx.asOk().refInput().remainForward()));
  return MatchStatus::OK;
}

MatchStatus split(const Regex &regex, const StringRef text, const unsigned int limit,
                  const std::function<bool(StringRef)> &consumer, const ObserverPtr<Timer> timer) {
  if (!limit) {
    return MatchStatus::OK;
  }
  if (limit == 1) {
    TRY(!consumer || consumer(text));
    return MatchStatus::OK;
  }
  std::vector<Capture> captures;
  auto ctx = tryToCreateMatchContext(regex, text, 0, captures);
  if (!ctx) {
    return ctx.asErr();
  }
  bool ignoreRemain = false;
  for (unsigned int count = 1; count < limit; count++) {
    const unsigned int matchStartOffset = ctx.asOk().getInput().getOffset();
    const auto s = match(ctx.asOk(), timer);
    auto &input = ctx.asOk().refInput(); // input is updated after call match
    if (s == MatchStatus::FAIL) {
      input.setIter(input.getBegin() + matchStartOffset);
      break;
    }
    if (s != MatchStatus::OK) {
      return s;
    }
    if (matchStartOffset != input.getOffset()) {
      TRY(!consumer || consumer(text.slice(matchStartOffset, captures[0].offset)));
    } else if (input.available()) {
      input.consumeForward();
      TRY(!consumer || consumer(StringRef(input.getBegin() + matchStartOffset,
                                          input.getOffset() - matchStartOffset)));
    } else {
      ignoreRemain = true;
      break;
    }
  }
  if (!ignoreRemain) {
    TRY(!consumer || consumer(ctx.asOk().refInput().remainForward()));
  }
  return MatchStatus::OK;
}

#undef TRY
#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool escape(const StringRef ref, const size_t maxSize, std::string &out) {
  const char *const end = ref.end();
  const char *iter = ref.begin();
  out.reserve(ref.size() + out.size());
  if (iter != end && isLetterOrDigit(*iter)) {
    char ch = *(iter++);
    char data[16];
    snprintf(data, std::size(data), "\\x%02x", ch);
    TRY(checkedAppend(data, maxSize, out));
  }
  for (; iter != end; ++iter) {
    char buf[16];
    unsigned int len = 0;
    switch (*iter) {
    case '^':
    case '$':
    case '\\':
    case '.':
    case '*':
    case '+':
    case '?':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '|':
    case '/':
      buf[0] = '\\';
      buf[1] = *iter;
      len = 2;
      break;
    case ',':
    case '-':
    case '=':
    case '<':
    case '>':
    case '#':
    case '&':
    case '!':
    case '%':
    case ':':
    case ';':
    case '@':
    case '~':
    case '\'':
    case '`':
    case '"': {
      const char *table = "0123456789abcdef";
      const auto ch = static_cast<unsigned char>(*iter);
      buf[0] = '\\';
      buf[1] = 'x';
      buf[2] = table[ch / 16];
      buf[3] = table[ch % 16];
      len = 4;
      break;
    }
    case '\f':
      buf[0] = '\\';
      buf[1] = 'f';
      len = 2;
      break;
    case '\n':
      buf[0] = '\\';
      buf[1] = 'n';
      len = 2;
      break;
    case '\r':
      buf[0] = '\\';
      buf[1] = 'r';
      len = 2;
      break;
    case '\t':
      buf[0] = '\\';
      buf[1] = 't';
      len = 2;
      break;
    case '\v':
      buf[0] = '\\';
      buf[1] = 'v';
      len = 2;
      break;
    case ' ':
      buf[0] = '\\';
      buf[1] = 'x';
      buf[2] = '2';
      buf[3] = '0';
      len = 4;
      break;
    default:
      int codePoint;
      if (unsigned int byteSize = UnicodeUtil::wtf8ToCodePoint(iter, end, codePoint)) {
        if (ucp::hasPrimeLoneProperty(codePoint, ucp::Lone::ESRegexClassSpace) ||
            UnicodeUtil::isSurrogate(codePoint)) {
          if (codePoint <= 0xFF) {
            len = snprintf(buf, std::size(buf), "\\x%02x", codePoint);
          } else {
            len = snprintf(buf, std::size(buf), "\\u%04x", codePoint);
          }
        } else {
          memcpy(buf, iter, byteSize);
          len = byteSize;
        }
        iter += byteSize - 1;
      } else {
        buf[0] = *iter;
        len = 1;
      }
      break;
    }
    TRY(checkedAppend(StringRef(buf, len), maxSize, out));
  }
  return true;
}

} // namespace arsh::regex