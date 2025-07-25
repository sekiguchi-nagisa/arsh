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

#include <unistd.h>

#include "keycode.h"
#include "keyname_lex.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"
#include "misc/unicode.hpp"

#ifdef __linux__

#include <poll.h>

/**
 *
 * @param fd
 * @param timeoutMSec
 * @param mask
 * @return
 * if input is ready, return 0
 * if error, return -1 and set errno
 * if timeout, return -2
 */
static int waitForInputReady(int fd, int timeoutMSec, const sigset_t *mask) {
  struct pollfd fds[1];
  fds[0].fd = fd;
  fds[0].events = POLLIN;
  const timespec timespec = {
      .tv_sec = timeoutMSec / 1000,
      .tv_nsec = static_cast<long>(timeoutMSec) % 1000 * 1000 * 1000,
  };
  int ret = ppoll(fds, std::size(fds), timeoutMSec < 0 ? nullptr : &timespec, mask);
  if (ret <= 0) {
    if (ret == 0) {
      return -2;
    }
    return -1;
  }
  return 0;
}

#else

#include <sys/select.h>

/**
 * for macOS.
 * macOS poll is completely broken for pty
 * @param fd
 * @param timeoutMSec
 * @return
 * if input is ready, return 0
 * if error, return -1 and set errno
 * if timeout, return -2
 */
static int waitForInputReady(int fd, int timeoutMSec, const sigset_t *mask) {
  fd_set fds;
  const timespec timespec = {
      .tv_sec = timeoutMSec / 1000,
      .tv_nsec = static_cast<long>(timeoutMSec) % 1000 * 1000 * 1000,
  };
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  int ret = pselect(fd + 1, &fds, nullptr, nullptr, timeoutMSec < 0 ? nullptr : &timespec, mask);
  if (ret <= 0) {
    if (ret == 0) {
      return -2;
    }
    return -1;
  }
  return 0;
}

#endif

namespace arsh {

const char *toString(ModifierKey modifier) {
  switch (modifier) {
#define GEN_CASE(E, B, S)                                                                          \
  case ModifierKey::E:                                                                             \
    return S;
    EACH_MODIFIER_KEY(GEN_CASE)
#undef GEN_CASE
  default:
    return "";
  }
}

const char *toString(FunctionKey funcKey) {
  switch (funcKey) {
#define GEN_CASE(E, S)                                                                             \
  case FunctionKey::E:                                                                             \
    return S;
    EACH_FUNCTION_KEY(GEN_CASE)
#undef GEN_CASE
  default:
    return "";
  }
}

// ######################
// ##     KeyEvent     ##
// ######################

static bool isShifted(int ch) { return ch >= 'A' && ch <= 'Z'; }

static int unshift(int ch) { return (ch - 'A') + 'a'; }

static bool isShiftable(int ch) { return ch >= 'a' && ch <= 'z'; }

static bool isShiftableOrSpace(int ch) { return isShiftable(ch) || ch == ' '; }

static Optional<KeyEvent> parseSS3Seq(const char ch) {
  FunctionKey funcKey;
  switch (ch) {
  case 'A':
    funcKey = FunctionKey::UP;
    break;
  case 'B':
    funcKey = FunctionKey::DOWN;
    break;
  case 'C':
    funcKey = FunctionKey::RIGHT;
    break;
  case 'D':
    funcKey = FunctionKey::LEFT;
    break;
  case 'F':
    funcKey = FunctionKey::END;
    break;
  case 'H':
    funcKey = FunctionKey::HOME;
    break;
  case 'P':
    funcKey = FunctionKey::F1;
    break;
  case 'Q':
    funcKey = FunctionKey::F2;
    break;
  case 'R':
    funcKey = FunctionKey::F3;
    break;
  case 'S':
    funcKey = FunctionKey::F4;
    break;
  default:
    return {};
  }
  return KeyEvent(funcKey);
}

static int parseNum(StringRef &seq, int defaultValue) {
  StringRef::size_type pos = 0;
  while (pos < seq.size() && isDigit(seq[pos])) {
    pos++;
  }
  if (pos) {
    StringRef param = seq.slice(0, pos);
    seq = seq.substr(pos);
    const auto ret = convertToNum10<int32_t>(param.begin(), param.end());
    if (!ret || ret.value < 0) {
      return -1;
    }
    return ret.value;
  }
  return defaultValue;
}

static FunctionKey resolveTildeFuncKey(int num) {
  switch (num) {
  case 1:
    return FunctionKey::HOME; // for GNU Screen
  case 2:
    return FunctionKey::INSERT;
  case 3:
    return FunctionKey::DELETE;
  case 4:
    return FunctionKey::END; // for GNU Screen
  case 5:
    return FunctionKey::PAGE_UP;
  case 6:
    return FunctionKey::PAGE_DOWN;
  case 7:
    return FunctionKey::HOME;
  case 8:
    return FunctionKey::END;
  case 11:
    return FunctionKey::F1;
  case 12:
    return FunctionKey::F2;
  case 13:
    return FunctionKey::F3;
  case 14:
    return FunctionKey::F4;
  case 15:
    return FunctionKey::F5;
  case 17:
    return FunctionKey::F6;
  case 18:
    return FunctionKey::F7;
  case 19:
    return FunctionKey::F8;
  case 20:
    return FunctionKey::F9;
  case 21:
    return FunctionKey::F10;
  case 23:
    return FunctionKey::F11;
  case 24:
    return FunctionKey::F12;
  case 29:
    return FunctionKey::MENU;
  case 200:
    return FunctionKey::BRACKET_START;
  default:
    break;
  }
  return FunctionKey::ESCAPE; // not found
}

static FunctionKey resolveUFuncKey(int num) {
  switch (num) {
  case 9:
    return FunctionKey::TAB;
  case 13:
    return FunctionKey::ENTER;
  case 27:
    return FunctionKey::ESCAPE;
  case 127:
    return FunctionKey::BACKSPACE;
  // case 57358:
  //   return FunctionKey::CAPS_LOCK;
  case 57359:
    return FunctionKey::SCROLL_LOCK;
  // case 57360:
  //   return FunctionKey::NUM_LOCK;
  case 57361:
    return FunctionKey::PRINT_SCREEN;
  case 57362:
    return FunctionKey::PAUSE;
  case 57363:
    return FunctionKey::MENU;
  default:
    break;
  }
  return FunctionKey::BRACKET_START; // not found
}

static Optional<KeyEvent> resolveCodePoint(int num, ModifierKey modifiers, int alternateCode) {
  // ignore Unicode Private Use Area (kitty may treat theme as additional functional keys)
  if (num >= 57344 && num <= 63743) {
    return {};
  }
  if (alternateCode > -1 && !hasFlag(modifiers, ModifierKey::SHIFT)) {
    return {}; // invalid alternate code (need shift modifier)
  }
  if (hasFlag(modifiers, ModifierKey::SHIFT) && alternateCode > -1 && !isShiftable(num)) {
    num = alternateCode;
    unsetFlag(modifiers, ModifierKey::SHIFT);
  }
  return KeyEvent(num, modifiers);
}

static Optional<KeyEvent> resolveCSI(int num, ModifierKey modifiers, int alternateCode,
                                     const char finalByte) {
  switch (finalByte) {
  case 'A':
    if (num == 1) {
      return KeyEvent(FunctionKey::UP, modifiers);
    }
    break;
  case 'B':
    if (num == 1) {
      return KeyEvent(FunctionKey::DOWN, modifiers);
    }
    break;
  case 'C':
    if (num == 1) {
      return KeyEvent(FunctionKey::RIGHT, modifiers);
    }
    break;
  case 'D':
    if (num == 1) {
      return KeyEvent(FunctionKey::LEFT, modifiers);
    }
    break;
  case 'F':
    if (num == 1) {
      return KeyEvent(FunctionKey::END, modifiers);
    }
    break;
  case 'H':
    if (num == 1) {
      return KeyEvent(FunctionKey::HOME, modifiers);
    }
    break;
  case 'P':
    if (num == 1) {
      return KeyEvent(FunctionKey::F1, modifiers);
    }
    break;
  case 'Q':
    if (num == 1) {
      return KeyEvent(FunctionKey::F2, modifiers);
    }
    break;
  case 'S':
    if (num == 1) {
      return KeyEvent(FunctionKey::F4, modifiers);
    }
    break;
  case 'Z':
    if (num == 1) {
      return KeyEvent(FunctionKey::TAB, modifiers | ModifierKey::SHIFT);
    }
    break;
  case 'u':
    if (auto funcKey = resolveUFuncKey(num); funcKey != FunctionKey::BRACKET_START) {
      return KeyEvent(funcKey, modifiers);
    }
    return resolveCodePoint(num, modifiers, alternateCode);
  case '~':
    if (auto funcKey = resolveTildeFuncKey(num); funcKey != FunctionKey::ESCAPE) {
      return KeyEvent(funcKey, modifiers);
    }
    break;
  default:
    break;
  }
  return {};
}

constexpr unsigned int numOfModifiers() {
  constexpr ModifierKey table[] = {
#define GEN_TABLE(E, D, S) ModifierKey::E,
      EACH_MODIFIER_KEY(GEN_TABLE)
#undef GEN_TABLE
  };
  return std::size(table);
}

constexpr ModifierKey fillModifiers() {
  ModifierKey modifiers{};
  constexpr ModifierKey table[] = {
#define GEN_TABLE(E, D, S) ModifierKey::E,
      EACH_MODIFIER_KEY(GEN_TABLE)
#undef GEN_TABLE
  };
  for (auto &e : table) {
    setFlag(modifiers, e);
  }
  return modifiers;
}

static Optional<KeyEvent> parseCSISeq(StringRef seq) {
  assert(!seq.empty());
  ModifierKey modifiers{};
  int alternateCode = -1;
  const char finalByte = seq.back();
  seq.removeSuffix(1);

  // extract param bytes (number)
  const int num = parseNum(seq, 1);
  if (num < 0) {
    goto ERROR;
  }

  if (!seq.empty() && seq[0] == ':') { // alternate key
    seq.removePrefix(1);
    if (seq.empty()) {
      goto ERROR;
    }
    if (seq[0] != ':') {
      alternateCode = parseNum(seq, -1);
      if (alternateCode < 0) {
        goto ERROR;
      }
    }
  }
  if (!seq.empty() && seq[0] == ':') { // base-layout key
    seq.removePrefix(1);
    if (parseNum(seq, -1) < 0) { // TODO: support base-layout key (currently, parse but ignore)
      goto ERROR;
    }
  }

  // extract modifiers (number)
  if (seq.empty()) { // do nothing
  } else if (seq[0] == ';') {
    constexpr int MODIFIERS_LIMIT = toUnderlying(fillModifiers()) + 1;

    seq.removePrefix(1);
    int v = parseNum(seq, 0);
    if (v < 0 || v > MODIFIERS_LIMIT) {
      goto ERROR;
    }
    if (v) {
      modifiers = static_cast<ModifierKey>(v - 1);
    }
  } else {
    goto ERROR;
  }

  // event type
  if (!seq.empty() && seq[0] == ':') {
    seq.removePrefix(1);
    if (parseNum(seq, -1) != 1) { // only accept '1' (press event)
      goto ERROR;
    }
  }

  // TODO: associated text of kitty protocol
  if (seq.empty()) {
    return resolveCSI(num, modifiers, alternateCode, finalByte);
  }

ERROR:
  return {};
}

Optional<KeyEvent> KeyEvent::fromEscapeSeq(const StringRef seq) {
  if (seq.empty() || !isControlChar(seq[0])) {
    return {};
  }
  if (seq.size() == 1) { // C0 control codes
    auto v = static_cast<unsigned char>(seq[0]);
    v ^= 64;
    assert(isCaretTarget(v));
    if (isShifted(v)) {
      v = unshift(v);
    }
    return KeyEvent(v, ModifierKey::CTRL);
  }
  if (seq[0] != '\x1b') {
    return {};
  }
  if (seq.size() == 2) { // ESC ?
    int ch = static_cast<unsigned char>(seq[1]);
    if (ch >= 0 && ch <= 127) {
      auto modifiers = ModifierKey::ALT;
      if (isControlChar(ch)) {
        setFlag(modifiers, ModifierKey::CTRL);
        auto v = static_cast<unsigned char>(ch);
        v ^= 64;
        ch = v;
        if (isShifted(ch)) {
          ch = unshift(ch);
        }
      }
      if (isShifted(ch)) {
        setFlag(modifiers, ModifierKey::SHIFT);
        ch = unshift(ch);
      }
      return KeyEvent(ch, modifiers);
    }
    return {};
  }
  if (seq.size() == 3 && seq[1] == 'O') { // SS3 ( ESC O ?)
    return parseSS3Seq(seq[2]);
  }
  if (const char next = seq[1]; next == '[') { // 'ESC [ ?'
    auto ret = parseCSISeq(seq.substr(2));
    if (ret.hasValue() && ret.unwrap().isBracketedPasteStart() && seq != BRACKET_START) {
      return {}; // only accept the '\x1b[200~' sequence (ignore extra modifiers)
    }
    return ret;
  } else if (next == '\x1b' && seq.size() == 4 && seq[2] == '[') {
    FunctionKey funcKey;
    switch (seq[3]) { // '\e \e [ ?' (alt+arrow in macOS terminal.app)
    case 'A':
      funcKey = FunctionKey::UP;
      break;
    case 'B':
      funcKey = FunctionKey::DOWN;
      break;
    case 'C':
      funcKey = FunctionKey::RIGHT;
      break;
    case 'D':
      funcKey = FunctionKey::LEFT;
      break;
    default:
      return {};
    }
    return KeyEvent(funcKey, ModifierKey::ALT);
  }
  return {};
}

static std::string normalizeIdentifier(const StringRef ref) {
  std::string value;
  for (char ch : ref) {
    if (ch == '_') {
      continue;
    }
    if (isShifted(ch)) {
      ch = static_cast<char>(unshift(ch));
    }
    value += ch;
  }
  return value;
}

static std::unordered_map<std::string, ModifierKey> initModifierMap() {
  std::unordered_map<std::string, ModifierKey> ret;
  constexpr unsigned int N = numOfModifiers();
  for (unsigned int i = 0; i < N; i++) {
    const auto modifier = static_cast<ModifierKey>(1u << i);
    std::string key = normalizeIdentifier(toString(modifier));
    assert(!key.empty());
    ret.emplace(std::move(key), modifier);
  }
  return ret;
}

static const ModifierKey *lookupModifier(const std::string &normalizedKey) {
  static const auto modifierMap = initModifierMap();
  if (auto iter = modifierMap.find(normalizedKey); iter != modifierMap.end()) {
    return &iter->second;
  }
  return nullptr;
}

constexpr unsigned int numOfFuncKey() {
  constexpr FunctionKey table[] = {
#define GEN_TABLE(E, S) FunctionKey::E,
      EACH_FUNCTION_KEY(GEN_TABLE)
#undef GEN_TABLE
  };
  return std::size(table);
}

static std::unordered_map<std::string, FunctionKey> initFuncKeyMap() {
  std::unordered_map<std::string, FunctionKey> ret;
  constexpr unsigned int N = numOfFuncKey();
  for (unsigned int i = 0; i < N; i++) {
    auto funcKey = static_cast<FunctionKey>(i);
    if (funcKey == FunctionKey::BRACKET_START) {
      continue;
    }
    auto key = normalizeIdentifier(toString(funcKey));
    assert(!key.empty());
    ret.emplace(std::move(key), funcKey);
  }

  // add alias
  constexpr struct {
    char alias[7];
    FunctionKey funcKey;
  } aliasTable[] = {
      {"esc", FunctionKey::ESCAPE},
      {"bs", FunctionKey::BACKSPACE},
      {"ins", FunctionKey::INSERT},
      {"del", FunctionKey::DELETE},
      {"pgup", FunctionKey::PAGE_UP},
      {"pgdn", FunctionKey::PAGE_DOWN},
      /*{"caps", FunctionKey::CAPS_LOCK},*/
      {"scrlk", FunctionKey::SCROLL_LOCK},
      /*{"numlk", FunctionKey::NUM_LOCK},*/ {"prtsc", FunctionKey::PRINT_SCREEN},
      {"break", FunctionKey::PAUSE},
  };
  for (auto &e : aliasTable) {
    ret.emplace(e.alias, e.funcKey);
  }
  return ret;
}

static const FunctionKey *lookupFuncKey(const std::string &normalizedKey) {
  static const auto funcKeyMap = initFuncKeyMap();
  if (auto iter = funcKeyMap.find(normalizedKey); iter != funcKeyMap.end()) {
    return &iter->second;
  }
  return nullptr;
}

static char lookupAsciiChar(const std::string &normalizedKey) {
  constexpr struct {
    char abbr[7];
    char ch;
  } entries[] = {{"space", ' '}, {"plus", '+'}, {"minus", '-'}};
  for (auto &e : entries) {
    if (e.abbr == normalizedKey) {
      return e.ch;
    }
  }
  return '\0';
}

class KeyNameParser {
private:
  KeyNameLexer lexer;
  Token token;
  KeyNameTokenKind kind{};
  ModifierKey modifiers{};
  std::string *err{nullptr};

public:
  KeyNameParser(StringRef ref, std::string *err) : lexer(ref), err(err) {}

  /**
   * sequence : modifier ('+' | '-') sequence
   *          | ASCII_CHAR
   *          | funcKey
   * modifier : IDENTIFIER
   * funcKey  : IDENTIFIER
   */
  Optional<KeyEvent> operator()() {
    this->fetchNext();

    KeyEvent event;
    std::string key;
    while (this->kind == KeyNameTokenKind::IDENTIFIER) {
      key = this->getNormalizedKey();
      auto *modifier = lookupModifier(key);
      if (!modifier) {
        break;
      }
      this->fetchNext();
      if (this->kind == KeyNameTokenKind::PLUS || this->kind == KeyNameTokenKind::MINUS) {
        this->fetchNext();
        setFlag(this->modifiers, *modifier);
        key.clear();
      } else {
        if (this->err) {
          *err += "need '+' or '-' after modifier";
        }
        goto ERROR;
      }
    }

    switch (this->kind) {
    case KeyNameTokenKind::ASCII_CHAR:
    case KeyNameTokenKind::PLUS:
    case KeyNameTokenKind::MINUS: {
      StringRef ref = this->getTokenText();
      assert(ref.size() == 1);
      char ch = ref[0];
      if (hasFlag(this->modifiers, ModifierKey::SHIFT) && !isShiftableOrSpace(ch)) {
        this->reportShiftError();
        goto ERROR;
      }
      if (isShifted(ch)) {
        ch = static_cast<char>(unshift(ch));
        setFlag(this->modifiers, ModifierKey::SHIFT);
      }
      event = KeyEvent(ch, this->modifiers);
      this->fetchNext();
      break;
    }
    case KeyNameTokenKind::IDENTIFIER: {
      if (key.empty()) {
        key = this->getNormalizedKey();
      }
      if (auto *funcKey = lookupFuncKey(key)) {
        event = KeyEvent(*funcKey, this->modifiers);
      } else if (char ch = lookupAsciiChar(key); ch != '\0') {
        if (hasFlag(this->modifiers, ModifierKey::SHIFT) && !isShiftableOrSpace(ch)) {
          this->reportShiftError();
          goto ERROR;
        }
        event = KeyEvent(ch, this->modifiers);
      } else {
        if (this->err) {
          *this->err += "unrecognized modifier or function key: ";
          *this->err += this->getTokenText();
        }
        goto ERROR;
      }
      this->fetchNext();
      break;
    }
    default:
      if (this->err) {
        *this->err += "need modifiers or keyname: ";
        *this->err += this->getTokenText();
      }
      goto ERROR;
    }

    if (this->kind == KeyNameTokenKind::EOS) {
      return event;
    }
    if (this->err) {
      *this->err += "invalid token: ";
      *this->err += this->lexer.toStrRef(this->token);
    }

  ERROR:
    return {};
  }

private:
  void fetchNext() { this->kind = this->lexer.nextToken(this->token); }

  void reportShiftError() const {
    if (this->err) {
      *this->err += "shift modifier is only allowed with lower letter or function key";
    }
  }

  StringRef getTokenText() const { return this->lexer.toStrRef(this->token); }

  std::string getNormalizedKey() const { return normalizeIdentifier(this->getTokenText()); }
};

Optional<KeyEvent> KeyEvent::fromKeyName(StringRef ref, std::string *err) {
  KeyNameParser parser(ref, err);
  return parser();
}

std::string KeyEvent::parseCaret(StringRef caret) {
  std::string value;
  auto size = caret.size();
  for (StringRef::size_type i = 0; i < size; i++) {
    char ch = caret[i];
    if (ch == '^' && i + 1 < size && isCaretTarget(caret[i + 1])) {
      i++;
      unsigned int v = static_cast<unsigned char>(caret[i]);
      v ^= 64;
      assert(isControlChar(static_cast<int>(v)));
      ch = static_cast<char>(static_cast<int>(v));
    }
    value += ch;
  }
  return value;
}

std::string KeyEvent::toCaret(StringRef value) {
  std::string ret;
  for (char ch : value) {
    if (isControlChar(ch)) {
      unsigned int v = static_cast<unsigned char>(ch);
      v ^= 64;
      assert(isCaretTarget(static_cast<int>(v)));
      ret += "^";
      ret += static_cast<char>(static_cast<int>(v));
    } else {
      ret += ch;
    }
  }
  return ret;
}

std::string KeyEvent::toString() const {
  std::string ret;
  constexpr unsigned int N = numOfModifiers();
  for (unsigned int i = 0; i < N; i++) {
    const auto modifier = static_cast<ModifierKey>(1u << i);
    if (this->hasModifier(modifier)) {
      ret += arsh::toString(modifier);
      ret += '+';
    }
  }
  if (this->isFuncKey()) {
    ret += arsh::toString(this->asFuncKey());
  } else if (this->asCodePoint() == ' ') {
    ret += "space";
  } else {
    char buf[4];
    const unsigned int len = UnicodeUtil::codePointToUtf8(this->asCodePoint(), buf);
    ret.append(buf, len);
  }
  return ret;
}

// ###########################
// ##     KeyCodeReader     ##
// ###########################

ssize_t readWithTimeout(const int fd, char *buf, const size_t bufSize,
                        const ReadWithTimeoutParam param) {
  if (param.timeoutMSec > -1) {
    while (true) {
      errno = 0;
      const int r = waitForInputReady(fd, param.timeoutMSec, nullptr);
      if (r != 0) {
        if (r == -1 && param.retry && errno == EINTR) {
          continue;
        }
        return r;
      }
      break;
    }
  }
  while (true) {
    errno = 0;
    const ssize_t readSize = read(fd, buf, bufSize);
    if (readSize < 0 && param.retry && (errno == EINTR || errno == EAGAIN)) {
      continue;
    }
    return readSize;
  }
}

static ssize_t readCodePoint(int fd, char (&buf)[8]) {
  ssize_t readSize = readRetryWithTimeout(fd, &buf[0], 1, -1); // no-timeout
  if (readSize <= 0) {
    return readSize;
  }
  const unsigned int byteSize = UnicodeUtil::utf8ByteSize(buf[0]);
  for (unsigned int i = 1; i < byteSize; i++) {
    if (const ssize_t r = readRetryWithTimeout(fd, &buf[i], 1, 100); r <= 0) {
      if (r == -1) {
        return -1;
      }
      break;
    }
    readSize++;
  }
  return readSize;
}

static ssize_t readAndAppendByte(int fd, std::string &out, int timeout) {
  char b;
  const ssize_t r = readRetryWithTimeout(fd, &b, 1, timeout);
  if (r > 0) {
    out += b;
  }
  return r;
}

#define READ_AND_APPEND_BYTE()                                                                     \
  do {                                                                                             \
    ssize_t r = readAndAppendByte(this->fd, this->keycode, this->timeout);                         \
    if (r <= 0) {                                                                                  \
      if (r == -1) {                                                                               \
        return -1; /* read error */                                                                \
      }                                                                                            \
      goto END; /* read timeout or EOF */                                                          \
    }                                                                                              \
  } while (false)

ssize_t KeyCodeReader::fetch(AtomicSigSet &&watchSigSet) {
  this->event = {};
  {
    sigset_t set;
    sigfillset(&set);
    while (!watchSigSet.empty()) {
      const int sigNum = watchSigSet.popPendingSig();
      sigdelset(&set, sigNum);
    }
    errno = 0;
    if (waitForInputReady(this->fd, -1, &set) == -1) {
      return -1;
    }
  }

  // read code point
  {
    char buf[8];
    ssize_t readSize = readCodePoint(this->fd, buf);
    if (readSize <= 0) {
      return readSize;
    }
    assert(readSize > 0 && readSize < 5);
    this->keycode.assign(buf, static_cast<size_t>(readSize));
  }

  if (isEscapeChar(this->keycode[0])) {
    assert(this->keycode.size() == 1);
    READ_AND_APPEND_BYTE();
    if (const char next = this->keycode.back(); next == '[') { // CSI sequence ('\e [ ?')
      READ_AND_APPEND_BYTE();
      char ch = this->keycode.back();

      // try to consume parameter bytes [0-9:;<=>?]
      while (ch >= 0x30 && ch <= 0x3F) {
        READ_AND_APPEND_BYTE();
        ch = this->keycode.back();
      }

      // try to consume intermediate bytes [ !"#$%&'()*+,-./]
      while (ch >= 0x20 && ch <= 0x2F) {
        READ_AND_APPEND_BYTE();
        ch = this->keycode.back();
      }

      // consume a single final byte [@A-Z[\]^_`a-z{|}~]
      if (ch >= 0x40 && ch <= 0x7E) {
        goto END; // valid CSI sequence
      }
    } else if (next == 'O') { // '\e O ?'
      READ_AND_APPEND_BYTE();
    } else if (next == '\x1b') { // '\e \e [ ?' (alt+arrow in macOS terminal.app)
      READ_AND_APPEND_BYTE();
      if (this->keycode.back() == '[') {
        READ_AND_APPEND_BYTE();
      }
    }
  }
END:
  this->event = KeyEvent::fromEscapeSeq(this->keycode);
  return static_cast<ssize_t>(this->keycode.size());
}

} // namespace arsh