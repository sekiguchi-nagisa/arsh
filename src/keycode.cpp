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

#include <cassert>
#include <csignal>

#include "constant.h"
#include "keycode.h"
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

// ######################
// ##     KeyEvent     ##
// ######################

static bool isShifted(int ch) { return ch >= 'A' && ch <= 'Z'; }

static int unshift(int ch) { return (ch - 'A') + 'a'; }

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
  case 2:
    return FunctionKey::INSERT;
  case 3:
    return FunctionKey::DELETE;
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
  case 57358:
    return FunctionKey::CAPS_LOCK;
  case 57359:
    return FunctionKey::SCROLL_LOCK;
  case 57360:
    return FunctionKey::NUM_LOCK;
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

static Optional<KeyEvent> resolveCSI(int num, ModifierKey modifiers, const char final) {
  switch (final) {
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
    break; // TODO: unicode code point
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

static Optional<KeyEvent> parseCSISeq(StringRef seq) { // TODO: kitty protocol (alternate key)
  assert(!seq.empty());
  ModifierKey modifiers{};
  const char final = seq.back();
  seq.removeSuffix(1);

  // extract param bytes (number)
  const int num = parseNum(seq, 1);
  if (num < 0) {
    goto ERROR;
  }

  // extract modifiers (number)
  if (seq.empty()) { // do nothing
  } else if (seq[0] == ';') {
    seq.removePrefix(1);
    int v = parseNum(seq, 0);
    if (v < 0 || v > UINT8_MAX) {
      goto ERROR;
    }
    if (v) {
      modifiers = static_cast<ModifierKey>(v - 1);
    }
  } else {
    goto ERROR;
  }

  if (seq.empty()) {
    return resolveCSI(num, modifiers, final);
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
  assert(seq[0] == '\x1b');
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
    if (ret.hasValue()) {
      if (auto event = ret.unwrap();
          event.isFuncKey() && event.asFuncKey() == FunctionKey::BRACKET_START) {
        if (seq == BRACKET_START) { // only accept the '\x1b[200~' sequence
          return event;
        }
        return {};
      }
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

Optional<KeyEvent> KeyEvent::parseHRNotation(StringRef ref, std::string *err) {
  (void)ref;
  (void)err;
  return {};
}

std::string KeyEvent::toString() const {
  std::string ret;
  {
    constexpr const char *table[] = {
#define GEN_TABLE(E, D, S) S,
        EACH_MODIFIER_KEY(GEN_TABLE)
#undef GEN_TABLE
    };

    for (unsigned int i = 0; i < std::size(table); i++) {
      const auto modifier = static_cast<ModifierKey>(1u << i);
      if (this->hasModifier(modifier)) {
        ret += table[i];
        ret += '+';
      }
    }
  }
  if (this->isFuncKey()) {
    constexpr const char *table[] = {
#define GEN_TABLE(E, S) #E,
        EACH_FUNCTION_KEY(GEN_TABLE)
#undef GEN_TABLE
    };
    if (unsigned int index = toUnderlying(this->asFuncKey()); index < std::size(table)) {
      ret += table[index];
    }
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

// #############################
// ##     CustomActionMap     ##
// #############################

const std::pair<CStrPtr, EditAction> *CustomActionMap::find(StringRef ref) const {
  if (auto iter = this->indexes.find(ref); iter != this->indexes.end()) {
    return &this->entries[iter->second];
  }
  return nullptr;
}

static auto lookup(const std::vector<std::pair<CStrPtr, EditAction>> &entries,
                   const std::pair<CStrPtr, EditAction> &key) {
  return std::lower_bound(
      entries.begin(), entries.end(), key,
      [](const std::pair<CStrPtr, EditAction> &x, const std::pair<CStrPtr, EditAction> &y) {
        return x.second.customActionIndex < y.second.customActionIndex;
      });
}

const std::pair<CStrPtr, EditAction> *CustomActionMap::findByIndex(unsigned int index) const {
  std::pair<CStrPtr, EditAction> dummy(nullptr, EditAction(CustomActionType::INSERT, index));
  if (const auto iter = lookup(this->entries, dummy); iter != this->entries.end()) {
    return iter.base();
  }
  return nullptr;
}

static unsigned int findFreshIndex(const std::vector<std::pair<CStrPtr, EditAction>> &entries) {
  const unsigned int size = entries.size();
  if (size == 0 || (entries[0].second.customActionIndex == 0 &&
                    entries[size - 1].second.customActionIndex == size - 1)) { // fast path
    return size;
  }

  // for sparse vector
  unsigned int retIndex = size;
  for (unsigned int i = 0; i < size; i++) {
    retIndex = entries[i].second.customActionIndex;
    if (i == retIndex) {
      continue;
    }
    retIndex = i;
    break;
  }
  return retIndex;
}

const std::pair<CStrPtr, EditAction> *CustomActionMap::add(StringRef name, CustomActionType type) {
  const unsigned int actionIndex = findFreshIndex(this->entries);
  auto entry = std::make_pair(CStrPtr(strdup(name.data())), EditAction(type, actionIndex));
  if (!this->indexes.emplace(entry.first.get(), actionIndex).second) {
    return nullptr; // already defined
  }

  auto iter = lookup(this->entries, entry);
  if (iter == this->entries.end()) { // not found
    this->entries.push_back(std::move(entry));
    return &this->entries.back();
  }
  assert(iter->second.customActionIndex != actionIndex);
  iter = this->entries.insert(iter, std::move(entry));
  return iter.base();
}

int CustomActionMap::remove(StringRef ref) {
  const auto iter = this->indexes.find(ref);
  if (iter == this->indexes.end()) {
    return -1;
  }
  const unsigned int removedIndex = iter->second;
  this->indexes.erase(iter);
  std::pair<CStrPtr, EditAction> dummy(nullptr, EditAction(CustomActionType::INSERT, removedIndex));
  if (const auto i = lookup(this->entries, dummy); i != this->entries.end()) {
    this->entries.erase(i);
  }
  return static_cast<int>(removedIndex);
}

// ########################
// ##     KeyBindings    ##
// ########################

std::string KeyBindings::parseCaret(StringRef caret) {
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

std::string KeyBindings::toCaret(StringRef value) {
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

#define CTRL_A_ "\x01"
#define CTRL_B_ "\x02"
#define CTRL_C_ "\x03"
#define CTRL_D_ "\x04"
#define CTRL_E_ "\x05"
#define CTRL_F_ "\x06"
#define CTRL_H_ "\x08"
#define CTRL_I_ "\x09"
#define TAB_ CTRL_I_
#define CTRL_J_ "\x0A"
#define CTRL_K_ "\x0B"
#define CTRL_L_ "\x0C"
#define CTRL_M_ "\x0D"
#define ENTER_ CTRL_M_
#define CTRL_N_ "\x0E"
#define CTRL_P_ "\x10"
#define CTRL_T_ "\x14"
#define CTRL_U_ "\x15"
#define CTRL_V_ "\x16"
#define CTRL_W_ "\x17"
#define CTRL_Y_ "\x19"
#define CTRL_Z_ "\x1A"
#define ESC_ "\x1b"
#define BACKSPACE_ "\x7F"

KeyBindings::KeyBindings() {
  // define edit action
  constexpr struct {
    const char key[7];
    EditActionType type;
  } entries[] = {
      // control character
      {ENTER_, EditActionType::ACCEPT},
      {CTRL_J_, EditActionType::ACCEPT},
      {CTRL_C_, EditActionType::CANCEL},
      {TAB_, EditActionType::COMPLETE},
      {CTRL_H_, EditActionType::BACKWARD_DELETE_CHAR},
      {BACKSPACE_, EditActionType::BACKWARD_DELETE_CHAR},
      {CTRL_D_, EditActionType::DELETE_OR_EXIT},
      {CTRL_T_, EditActionType::TRANSPOSE_CHAR},
      {CTRL_B_, EditActionType::BACKWARD_CHAR},
      {CTRL_F_, EditActionType::FORWARD_CHAR},
      {CTRL_P_, EditActionType::UP_OR_HISTORY},
      {CTRL_N_, EditActionType::DOWN_OR_HISTORY},
      {CTRL_U_, EditActionType::BACKWORD_KILL_LINE},
      {CTRL_K_, EditActionType::KILL_LINE},
      {CTRL_A_, EditActionType::BEGINNING_OF_LINE},
      {CTRL_E_, EditActionType::END_OF_LINE},
      {CTRL_L_, EditActionType::CLEAR_SCREEN},
      {CTRL_W_, EditActionType::BACKWARD_KILL_WORD},
      {CTRL_V_, EditActionType::INSERT_KEYCODE},
      {CTRL_Y_, EditActionType::YANK},
      {CTRL_Z_, EditActionType::UNDO},

      // escape sequence
      {ESC_ "b", EditActionType::BACKWARD_WORD},
      {ESC_ "f", EditActionType::FORWARD_WORD},
      {ESC_ "d", EditActionType::KILL_WORD},
      {ESC_ "y", EditActionType::YANK_POP},
      {ESC_ "/", EditActionType::REDO},
      {ESC_ ENTER_, EditActionType::NEWLINE},
      {ESC_ "<", EditActionType::BEGINNING_OF_BUF},
      {ESC_ ">", EditActionType::END_OF_BUF},
      {ESC_ "[1~", EditActionType::BEGINNING_OF_LINE},
      {ESC_ "[4~", EditActionType::END_OF_LINE}, // for putty
      {ESC_ "[3~", EditActionType::DELETE_CHAR}, // for putty
      {ESC_ "[200~", EditActionType::BRACKET_PASTE},
      {ESC_ "[1;3A", EditActionType::PREV_HISTORY},  // alt+up
      {ESC_ "[1;3B", EditActionType::NEXT_HISTORY},  // alt+down
      {ESC_ "[1;3D", EditActionType::BACKWARD_WORD}, // alt+left
      {ESC_ "[1;3C", EditActionType::FORWARD_WORD},  // alt+right
      {ESC_ "[A", EditActionType::UP_OR_HISTORY},    // up
      {ESC_ "[B", EditActionType::DOWN_OR_HISTORY},  // down
      {ESC_ "[D", EditActionType::BACKWARD_CHAR},    // left
      {ESC_ "[C", EditActionType::FORWARD_CHAR},     // right
      {ESC_ "[H", EditActionType::BEGINNING_OF_LINE},
      {ESC_ "[F", EditActionType::END_OF_LINE},
      {ESC_ "OH", EditActionType::BEGINNING_OF_LINE},
      {ESC_ "OF", EditActionType::END_OF_LINE},
      {ESC_ ESC_ "[A", EditActionType::PREV_HISTORY},  // for macOS terminal.app (alt+up)
      {ESC_ ESC_ "[B", EditActionType::NEXT_HISTORY},  // for macOS terminal.app (alt+up)
      {ESC_ ESC_ "[D", EditActionType::BACKWARD_WORD}, // for macOS terminal.app (alt+left)
      {ESC_ ESC_ "[C", EditActionType::FORWARD_WORD},  // for macOS terminal.app (alt+right)
  };
  for (auto &e : entries) {
    auto pair = this->values.emplace(e.key, e.type);
    (void)pair;
    assert(pair.second);
  }

  // define pager action
  constexpr struct {
    const char key[7];
    PagerAction action;
  } pagers[] = {
      {ENTER_, PagerAction::SELECT},
      {CTRL_J_, PagerAction::SELECT},
      {CTRL_C_, PagerAction::CANCEL},
      {ESC_, PagerAction::ESCAPE},
      {TAB_, PagerAction::NEXT},
      {ESC_ "[Z", PagerAction::PREV}, // shift-tab
      {CTRL_P_, PagerAction::PREV},
      {CTRL_N_, PagerAction::NEXT},

      {ESC_ "[1;3A", PagerAction::PREV},  // alt+up
      {ESC_ "[1;3B", PagerAction::NEXT},  // alt+down
      {ESC_ "[1;3D", PagerAction::LEFT},  // alt+left
      {ESC_ "[1;3C", PagerAction::RIGHT}, // alt+right
      {ESC_ "[A", PagerAction::PREV},     // up
      {ESC_ "[B", PagerAction::NEXT},     // down
      {ESC_ "[D", PagerAction::LEFT},     // left
      {ESC_ "[C", PagerAction::RIGHT},    // right

      // for mac
      {ESC_ ESC_ "[A", PagerAction::PREV},  // for macOS terminal.app (alt+up)
      {ESC_ ESC_ "[B", PagerAction::NEXT},  // for macOS terminal.app (alt+up)
      {ESC_ ESC_ "[D", PagerAction::LEFT},  // for macOS terminal.app (alt+left)
      {ESC_ ESC_ "[C", PagerAction::RIGHT}, // for macOS terminal.app (alt+right)
  };
  for (auto &e : pagers) {
    auto pair = this->pagerValues.emplace(e.key, e.action);
    (void)pair;
    assert(pair.second);
  }
}

const EditAction *KeyBindings::findAction(const std::string &keycode) const {
  auto iter = this->values.find(keycode);
  if (iter != this->values.end()) {
    return &iter->second;
  }
  return nullptr;
}

const PagerAction *KeyBindings::findPagerAction(const std::string &keycode) const {
  auto iter = this->pagerValues.find(keycode);
  if (iter != this->pagerValues.end()) {
    return &iter->second;
  }
  return nullptr;
}

const char *toString(EditActionType action) {
  constexpr const char *table[] = {
#define GEN_TABLE(E, S) S,
      EACH_EDIT_ACTION_TYPE(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[static_cast<unsigned int>(action)];
}

const StrRefMap<EditActionType> &KeyBindings::getEditActionTypes() {
  static const StrRefMap<EditActionType> actions = {
#define GEN_ENTRY(E, S) {S, EditActionType::E},
      EACH_EDIT_ACTION_TYPE(GEN_ENTRY)
#undef GEN_ENTRY
  };
  return actions;
}

const StrRefMap<CustomActionType> &KeyBindings::getCustomActionTypes() {
  static const StrRefMap<CustomActionType> types = {
#define GEN_ENTRY(E, S) {S, CustomActionType::E},
      EACH_CUSTOM_ACTION_TYPE(GEN_ENTRY)
#undef GEN_ENTRY
  };
  return types;
}

KeyBindings::AddStatus KeyBindings::addBinding(StringRef caret, StringRef name) {
  auto key = parseCaret(caret);
  if (key.empty() || !isControlChar(key[0])) {
    return AddStatus::INVALID_START_CHAR;
  }
  if (key == BRACKET_START) {
    return AddStatus::FORBID_BRACKET_START_CODE;
  }
  for (auto &e : caret) {
    if (!isascii(e)) {
      return AddStatus::INVALID_ASCII;
    }
  }

  auto &actionMap = getEditActionTypes();
  if (name.empty()) {
    this->values.erase(key);
  } else {
    EditAction action = EditActionType::ACCEPT;
    if (auto iter = actionMap.find(name); iter != actionMap.end()) {
      action = iter->second;
    } else if (auto *e = this->customActions.find(name); e) {
      action = e->second;
    } else {
      return AddStatus::UNDEF;
    }

    if (action.type == EditActionType::BRACKET_PASTE) {
      return AddStatus::FORBID_BRACKET_ACTION;
    }

    if (auto iter = this->values.find(key); iter != this->values.end()) {
      iter->second = action; // overwrite previous bind
    } else {
      if (this->values.size() == SYS_LIMIT_KEY_BINDING_MAX) {
        return AddStatus::LIMIT;
      }
      this->values.emplace(key, action);
    }
  }
  return AddStatus::OK;
}

Result<unsigned int, KeyBindings::DefineError> KeyBindings::defineCustomAction(StringRef name,
                                                                               StringRef type) {
  // check action name format
  if (name.empty()) {
    return Err(DefineError::INVALID_NAME);
  }
  for (auto &e : name) {
    if (isLetterOrDigit(e) || e == '-' || e == '_') {
      continue;
    }
    return Err(DefineError::INVALID_NAME);
  }

  // check action type
  CustomActionType customActionType;
  if (auto iter = getCustomActionTypes().find(type); iter != getCustomActionTypes().end()) {
    customActionType = iter->second;
  } else {
    return Err(DefineError::INVALID_TYPE);
  }

  // check already defined action name
  if (auto iter = getEditActionTypes().find(name); iter != getEditActionTypes().end()) {
    return Err(DefineError::DEFINED);
  }

  if (this->customActions.find(name)) {
    return Err(DefineError::DEFINED);
  }
  if (this->customActions.size() == SYS_LIMIT_CUSTOM_ACTION_MAX) {
    return Err(DefineError::LIMIT);
  }

  auto *ret = this->customActions.add(name, customActionType);
  assert(ret);
  return Ok(ret->second.customActionIndex);
}

int KeyBindings::removeCustomAction(StringRef name) {
  const int removedIndex = this->customActions.remove(name);
  if (removedIndex < 0) {
    return removedIndex;
  }

  // remove corresponding key-binding
  for (auto iter = this->values.begin(); iter != this->values.end();) {
    if (iter->second.type == EditActionType::CUSTOM &&
        iter->second.customActionIndex == static_cast<unsigned int>(removedIndex)) {
      iter = this->values.erase(iter);
    } else {
      ++iter;
    }
  }
  return removedIndex;
}

} // namespace arsh