/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#include "lexer.h"
#include "misc/fatal.h"
#include "misc/hash.hpp"
#include "misc/num_util.hpp"

namespace ydsh {

const char *toString(TokenKind kind) {
  const char *table[] = {
#define GEN_NAME(ENUM, STR) STR,
      EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
  };
  return table[static_cast<unsigned int>(kind)];
}

OperatorInfo getOpInfo(TokenKind kind) {
#define INFIX OperatorAttr::INFIX
#define PREFIX OperatorAttr::PREFIX
#define RASSOC OperatorAttr::RASSOC

  switch (kind) {
#define GEN_CASE(T, P, A)                                                                          \
  case TokenKind::T:                                                                               \
    return {P, A};
    EACH_OPERATOR(GEN_CASE)
#undef GEN_CASE
  default:
    return {};
  }

#undef INFIX
#undef PREFIX
#undef RASSOC
}

bool isAssignOp(TokenKind kind) {
  switch (kind) {
#define GEN_OP(K, P, A)                                                                            \
  case TokenKind::K:                                                                               \
    return true;
    EACH_ASSIGN_OPERATOR(GEN_OP)
#undef GEN_OP
  default:
    return false;
  }
}

std::string LexerMode::toString() const {
  const char *mode = "(";
  switch (this->cond()) {
  case yycSTMT:
    mode = "STMT(";
    break;
  case yycEXPR:
    mode = "EXPR(";
    break;
  case yycNAME:
    mode = "NAME(";
    break;
  case yycTYPE:
    mode = "TYPE(";
    break;
  case yycCMD:
    mode = "CMD(";
    break;
  case yycDSTRING:
    mode = "DSTRING(";
    break;
  }
  std::string value = mode;
  if (this->skipNL()) {
    value += "skipNL";
  }
  value += ")";
  return value;
}

// ###################
// ##     Lexer     ##
// ###################

Lexer Lexer::fromFullPath(const char *fullpath, ByteBuffer &&buf) {
  char *path = strdup(fullpath);
  const char *ptr = strrchr(path, '/');
  path[ptr == path ? 1 : ptr - path] = '\0';
  return Lexer(fullpath, std::move(buf), CStrPtr(path));
}

SrcPos Lexer::getSrcPos(Token token) const {
  token = this->shiftEOS(token);
  unsigned int lineNum = this->getLineNumByPos(token.pos);
  Token line = this->getLineToken(token);
  Token marker{
      .pos = line.pos,
      .size = token.pos - line.pos,
  };
  unsigned int chars = marker.size == 0 ? 1 : this->formatLineMarker(line, marker).size() + 1;

  return SrcPos{
      .lineNum = lineNum,
      .chars = chars,
  };
}

bool Lexer::singleToString(Token token, std::string &out) const {
  if (this->startsWith(token, '$')) {
    return this->escapedSingleToString(token, out);
  }

  Token trimed = token;
  trimed.pos++;
  trimed.size -= 2;

  out = this->toTokenText(trimed);
  return true;
}

bool Lexer::escapedSingleToString(Token token, std::string &out) const {
  assert(this->withinRange(token));

  out.clear();
  out.reserve(token.size - 3);

  StringRef ref = this->toStrRef(token);
  ref.removePrefix(2); // prefix "$'"
  ref.removeSuffix(1); // suffix "'"

  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    if (*iter == '\\') {
      auto ret = parseEscapeSeq(iter, end, false);
      switch (ret.kind) {
      case EscapeSeqResult::OK: {
        char buf[4];
        unsigned int size = UnicodeUtil::codePointToUtf8(ret.codePoint, buf);
        assert(size);
        out.append(buf, size);
        iter += ret.consumedSize;
        continue;
      }
      case EscapeSeqResult::UNKNOWN:
        if (*(iter + 1) == '\'') {
          iter += 2;
          out += '\'';
          continue;
        }
        break;
      default:
        return false;
      }
    }
    out += *(iter++);
  }
  return true;
}

std::string Lexer::doubleElementToString(Token token) const {
  assert(this->withinRange(token));

  std::string str;
  str.reserve(token.size);

  const unsigned int stopPos = token.pos + token.size;
  for (unsigned int i = token.pos; i < stopPos; i++) {
    char ch = this->buf[i];
    if (ch == '\\' && i + 1 < stopPos) {
      char next = this->buf[++i];
      switch (next) {
      case '"':
      case '$':
      case '\\':
        ch = next;
        break;
      case '\n':
        continue;
      default:
        --i;
        break;
      }
    }
    str += ch;
  }
  return str;
}

std::string Lexer::toCmdArg(Token token) const {
  assert(this->withinRange(token));

  std::string str;
  str.reserve(token.size);

  for (unsigned int i = 0; i < token.size; i++) {
    char ch = this->buf[token.pos + i];
    if (ch == '\\') {
      char nextCh = this->buf[token.pos + ++i];
      switch (nextCh) {
      case '\n':
      case '\r':
        continue;
      default:
        ch = nextCh;
        break;
      }
    }
    str += ch;
  }
  return str;
}

std::string Lexer::toName(Token token) const {
  assert(this->withinRange(token));

  std::string name;
  for (unsigned int i = this->buf[token.pos] == '$' ? 1 : 0; i < token.size; i++) {
    char ch = this->buf[token.pos + i];
    switch (ch) {
    /**
     * ex. $true, ${true}, $@[
     */
    case '{':
    case '}':
    case '[':
      continue;
    default:
      name += ch;
      break;
    }
  }
  return name;
}

int64_t Lexer::toInt64(Token token, int &status) const {
  auto ref = this->toStrRef(token);
  auto ret = fromIntLiteral<int64_t>(ref.begin(), ref.end());
  status = ret.second ? 0 : 1;
  return ret.first;
}

double Lexer::toDouble(Token token, int &status) const {
  assert(this->withinRange(token));
  double value = convertToDouble(this->toTokenText(token).c_str(), status);
  assert(status > -1);
  return value;
}

static bool isIdStart(char ch) {
  return ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool Lexer::toEnvName(Token token, std::string &out) const {
  auto ref = this->toStrRef(token);
  if (isIdStart(ref[0])) {
    out += ref[0];
  } else {
    return false;
  }
  unsigned int size = ref.size();
  for (unsigned int i = 1; i < size; i++) {
    char ch = ref[i];
    if (isDecimal(ch) || isIdStart(ch)) {
      out += ch;
    } else {
      return false;
    }
  }
  return true;
}

static EscapeSeqResult ok(int code, unsigned short size) {
  return {
      .kind = EscapeSeqResult::OK,
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

EscapeSeqResult parseEscapeSeq(const char *begin, const char *end, bool needOctalPrefix) {
  if (begin == end || *begin != '\\' || (begin + 1) == end) {
    return err(EscapeSeqResult::END, 0);
  }
  const char *old = begin;
  begin++; // consume '\'
  char next = *(begin++);
  switch (next) {
  case '\\':
    return ok('\\');
  case 'a':
    return ok('\a');
  case 'b':
    return ok('\b');
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
    if (code <= 0x10FFFF) {
      return ok(static_cast<int>(code), static_cast<unsigned short>(begin - old));
    } else {
      return err(EscapeSeqResult::RANGE, static_cast<unsigned short>(begin - old));
    }
  }
  default:
    if (!isOctal(next) || (needOctalPrefix && next != '0')) {
      return err(EscapeSeqResult::UNKNOWN, static_cast<unsigned short>(begin - old));
    }
    unsigned int code = next - '0';
    for (unsigned int i = needOctalPrefix ? 0 : 1; i < 3; i++) {
      if (begin != end && isOctal(*begin)) {
        code *= 8;
        code += *(begin++) - '0';
      } else {
        break;
      }
    }
    return ok(static_cast<int>(code), static_cast<unsigned short>(begin - old));
  }
}

} // namespace ydsh