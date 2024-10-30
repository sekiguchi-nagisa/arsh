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
#include "constant.h"
#include "misc/fatal.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "misc/unicode.hpp"

namespace arsh {

const char *toString(TokenKind kind) {
  constexpr const char *table[] = {
#define GEN_NAME(ENUM, STR) STR,
      EACH_TOKEN(GEN_NAME)
#undef GEN_NAME
  };
  return table[toUnderlying(kind)];
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

const char *resolveUnaryOpName(TokenKind op) {
  switch (op) {
  case TokenKind::PLUS: // +
    return OP_PLUS;
  case TokenKind::MINUS: // -
    return OP_MINUS;
  case TokenKind::NOT: // not
    return OP_NOT;
  default:
    fatal("unsupported unary op: %s\n", toString(op));
  }
}

const char *resolveBinaryOpName(TokenKind op) {
  switch (op) {
  case TokenKind::ADD:
    return OP_ADD;
  case TokenKind::SUB:
    return OP_SUB;
  case TokenKind::MUL:
    return OP_MUL;
  case TokenKind::DIV:
    return OP_DIV;
  case TokenKind::MOD:
    return OP_MOD;
  case TokenKind::EQ:
    return OP_EQ;
  case TokenKind::NE:
    return OP_NE;
  case TokenKind::LT:
    return OP_LT;
  case TokenKind::GT:
    return OP_GT;
  case TokenKind::LE:
    return OP_LE;
  case TokenKind::GE:
    return OP_GE;
  case TokenKind::AND:
    return OP_AND;
  case TokenKind::OR:
    return OP_OR;
  case TokenKind::XOR:
    return OP_XOR;
  case TokenKind::LSHIFT:
    return OP_LSHIFT;
  case TokenKind::RSHIFT:
    return OP_RSHIFT;
  case TokenKind::URSHIFT:
    return OP_URSHIFT;
  case TokenKind::MATCH:
    return OP_MATCH;
  case TokenKind::UNMATCH:
    return OP_UNMATCH;
  default:
    fatal("unsupported binary op: %s\n", toString(op));
  }
}

TokenKind resolveAssignOp(TokenKind op) {
  switch (op) {
  case TokenKind::INC:
    return TokenKind::ADD;
  case TokenKind::DEC:
    return TokenKind::SUB;
  case TokenKind::ADD_ASSIGN:
    return TokenKind::ADD;
  case TokenKind::SUB_ASSIGN:
    return TokenKind::SUB;
  case TokenKind::MUL_ASSIGN:
    return TokenKind::MUL;
  case TokenKind::DIV_ASSIGN:
    return TokenKind::DIV;
  case TokenKind::MOD_ASSIGN:
    return TokenKind::MOD;
  case TokenKind::NULL_ASSIGN:
    return TokenKind::NULL_COALE;
  default:
    fatal("unsupported assign op: %s\n", toString(op));
  }
}

std::pair<std::string, RedirOp> resolveRedirOp(TokenKind kind, StringRef ref) {
  unsigned int i = 0;
  for (; i < ref.size(); i++) {
    if (!isDigit(ref[i])) {
      break;
    }
  }
  auto prefix = ref.slice(0, i);
  RedirOp op = RedirOp::NOP;

#define EACH_REDIR_OP_MAPPING(OP)                                                                  \
  OP(REDIR_IN, REDIR_IN, 0)                                                                        \
  OP(REDIR_OUT, REDIR_OUT, 1)                                                                      \
  OP(REDIR_OUT_CLOBBER, CLOBBER_OUT, 1)                                                            \
  OP(REDIR_APPEND, APPEND_OUT, 1)                                                                  \
  OP(REDIR_OUT_ERR, REDIR_OUT_ERR, 1)                                                              \
  OP(REDIR_OUT_ERR_CLOBBER, CLOBBER_OUT_ERR, 1)                                                    \
  OP(REDIR_APPEND_OUT_ERR, APPEND_OUT_ERR, 1)                                                      \
  OP(REDIR_IN_OUT, REDIR_IN_OUT, 0)                                                                \
  OP(REDIR_DUP_IN, DUP_FD, 0)                                                                      \
  OP(REDIR_DUP_OUT, DUP_FD, 1)                                                                     \
  OP(REDIR_HERE_DOC, HERE_DOC, 0)                                                                  \
  OP(REDIR_HERE_DOC_DASH, HERE_DOC, 0)                                                             \
  OP(REDIR_HERE_STR, HERE_STR, 0)

  switch (kind) {
#define GEN_REDIR_CASE(K, O, D)                                                                    \
  case TokenKind::K:                                                                               \
    if (prefix.empty()) {                                                                          \
      prefix = #D;                                                                                 \
    }                                                                                              \
    op = RedirOp::O;                                                                               \
    break;

    // clang-format off
  EACH_REDIR_OP_MAPPING(GEN_REDIR_CASE)
    // clang-format on

#undef GEN_REDIR_CASE
  default:
    break;
  }

#undef EACH_REDIR_OP_MAPPING
  return {prefix.toString(), op};
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
  case yycPARAM:
    mode = "PARAM(";
    break;
  case yycHERE:
    mode = "HERE(";
    break;
  case yycEXP_HERE:
    mode = "EXP_HERE(";
    break;
  case yycATTR:
    mode = "ATTR(";
    break;
  }
  std::string value = mode;
  if (this->skipNL()) {
    value += "skipNL, ";
  }
  value += std::to_string(this->hereDepth());
  value += ")";
  return value;
}

// ###################
// ##     Lexer     ##
// ###################

IntrusivePtr<Lexer> Lexer::fromFullPath(const char *fullPath, ByteBuffer &&buf) {
  char *path = strdup(fullPath);
  const char *ptr = strrchr(path, '/');
  path[ptr == path ? 1 : ptr - path] = '\0';
  return LexerPtr::create(fullPath, std::move(buf), CStrPtr(path));
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

void Lexer::setHereDocStart(TokenKind hereOp, Token startToken, unsigned int redirPos) {
  assert(hereOp == TokenKind::REDIR_HERE_DOC || hereOp == TokenKind::REDIR_HERE_DOC_DASH);
  HereDocState::Attr attr{};
  if (hereOp == TokenKind::REDIR_HERE_DOC_DASH) {
    setFlag(attr, HereDocState::Attr::IGNORE_TAB);
  }
  if (this->startsWith(startToken, '\'')) {
    assert(startToken.size > 2);
    startToken = startToken.slice(1, startToken.size - 1);
  } else {
    setFlag(attr, HereDocState::Attr::EXPAND);
  }
  assert(startToken.size > 0);
  this->hereDocStates.back().add(startToken, attr, redirPos);
}

bool Lexer::tryExitHereDocMode(unsigned int startPos) {
  assert(this->hereDocStates.back());

  Token token = {
      .pos = startPos,
      .size = this->getPos() - startPos,
  };
  StringRef ref = this->toStrRef(token);
  if (ref.endsWith("\n")) {
    ref.removeSuffix(1);
  } else {
    return false;
  }
  StringRef start = this->toStrRef(this->hereDocStates.back().curHereDocState().token);
  if (ref == start) { // exit heredoc
    this->hereDocStates.back().shift();
    this->popLexerMode();
    this->tryEnterHereDocMode();
    return true;
  }
  return false;
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

  StringRef ref = this->toStrRef(token);
  ref.removePrefix(2); // prefix "$'"
  ref.removeSuffix(1); // suffix "'"

  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    if (*iter == '\\') {
      auto ret = parseEscapeSeq(iter, end, false);
      switch (ret.kind) {
      case EscapeSeqResult::OK_CODE: {
        char buf[4];
        unsigned int size = UnicodeUtil::codePointToUtf8(ret.codePoint, buf);
        assert(size);
        out.append(buf, size);
        iter += ret.consumedSize;
        continue;
      }
      case EscapeSeqResult::OK_BYTE: {
        auto b = static_cast<unsigned int>(ret.codePoint);
        char buf[1];
        buf[0] = static_cast<char>(static_cast<unsigned char>(b));
        out.append(buf, 1);
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
        out.assign(iter, ret.consumedSize);
        return false;
      }
    }
    out += *(iter++);
  }
  return true;
}

std::string Lexer::doubleElementToString(Token token, const bool skipDouble) const {
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
        if (skipDouble) {
          i--;
        } else {
          ch = next;
        }
        break;
      case '$':
      case '`':
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

std::string Lexer::toHereDocBody(Token token, HereDocState::Attr attr) const {
  if (hasFlag(attr, HereDocState::Attr::IGNORE_TAB)) {
    while (token.size > 0 && this->startsWith(token, '\t')) {
      token = token.sliceFrom(1);
    }
  }
  if (hasFlag(attr, HereDocState::Attr::EXPAND)) {
    return this->doubleElementToString(token, true);
  } else {
    return this->toTokenText(token);
  }
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

std::pair<int64_t, bool> Lexer::toInt64(Token token) const {
  auto ref = this->toStrRef(token);
  assert(!ref.empty());
  const bool decimal = ref[0] != '0';
  auto ret = convertToNum<uint64_t>(ref.begin(), ref.end(), 0);
  if (ret) {
    if (decimal) {
      return makeSigned(ret.value, false);
    } else {
      return {static_cast<int64_t>(ret.value), true};
    }
  }
  return {static_cast<int64_t>(ret.value), false};
}

std::pair<double, bool> Lexer::toDouble(Token token) const {
  assert(this->withinRange(token));
  auto ret = convertToDouble(this->toTokenText(token).c_str());
  assert(ret || ret.kind == DoubleConversionResult::OUT_OF_RANGE);
  return {ret.value, static_cast<bool>(ret)};
}

bool Lexer::toEnvName(Token token, std::string &out) const {
  auto ref = this->toStrRef(token);
  out = ref.toString();
  return isValidIdentifier(ref);
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

void appendAsPrintable(StringRef ref, size_t maxSize, std::string &out) {
  const auto old = errno;
  for (int ch : ref) { // for arm32/arm64
    if (unlikely(out.size() >= maxSize)) {
      if (maxSize >= 3) {
        out.resize(maxSize - 3);
        out += "...";
      }
      break;
    }
    if ((ch >= 0 && ch < 32) || ch == 127) {
      char d[16];
      snprintf(d, std::size(d), "\\x%02x", ch);
      out += d;
    } else {
      out += static_cast<char>(ch);
    }
  }
  errno = old;
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