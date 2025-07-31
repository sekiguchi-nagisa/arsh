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

#ifndef ARSH_KEYCODE_H
#define ARSH_KEYCODE_H

#include "misc/detect.hpp"
#include "misc/enum_util.hpp"
#include "misc/flag_util.hpp"
#include "misc/result.hpp"
#include "signals.h"

namespace arsh {

inline bool isControlChar(int ch) { return (ch >= 0 && ch <= 31) || ch == 127; }

inline bool isEscapeChar(int ch) { return ch == '\x1b'; }

inline bool isCaretTarget(int ch) { return (ch >= '@' && ch <= '_') || ch == '?'; }

inline bool isAsciiPrintable(int ch) { return ch >= 32 && ch <= 126; }

inline bool isShiftable(int ch) { return ch >= 'a' && ch <= 'z'; }

struct ReadWithTimeoutParam {
  bool retry;
  int timeoutMSec;
};

/**
 *
 * @param fd
 * @param buf
 * @param bufSize
 * @param param
 * @return
 * if timeout, return -2
 * if error, return -1 and set errno
 * otherwise, return non-negative number
 */
ssize_t readWithTimeout(int fd, char *buf, size_t bufSize, ReadWithTimeoutParam param);

inline ssize_t readRetryWithTimeout(int fd, char *buf, size_t bufSize, int timeoutMSec) {
  return readWithTimeout(fd, buf, bufSize, {.retry = true, .timeoutMSec = timeoutMSec});
}

#define EACH_MODIFIER_KEY(OP)                                                                      \
  OP(SHIFT, (1u << 0u), "shift")                                                                   \
  OP(ALT, (1u << 1u), "alt")                                                                       \
  OP(CTRL, (1u << 2u), "ctrl")                                                                     \
  OP(SUPER, (1u << 3u), "super")                                                                   \
  OP(HYPER, (1u << 4u), "hyper")                                                                   \
  OP(META, (1u << 5u), "meta")
// OP(CAPS, (1u << 6u), "caps_lock")
// OP(NUM, (1u << 7u), "num_lock")

// for kitty keyboard protocol
enum class ModifierKey : unsigned char {
#define GEN_ENUM(E, D, S) E = (D),
  EACH_MODIFIER_KEY(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(ModifierKey modifier);

template <>
struct allow_enum_bitop<ModifierKey> : std::true_type {};

#define EACH_FUNCTION_KEY(OP)                                                                      \
  OP(ESCAPE, "escape") /* esc */                                                                   \
  OP(ENTER, "enter")                                                                               \
  OP(TAB, "tab")                                                                                   \
  OP(BACKSPACE, "backspace") /* bs */                                                              \
  OP(INSERT, "insert")       /* ins */                                                             \
  OP(DELETE, "delete")       /* del */                                                             \
  OP(LEFT, "left")                                                                                 \
  OP(RIGHT, "right")                                                                               \
  OP(UP, "up")                                                                                     \
  OP(DOWN, "down")                                                                                 \
  OP(PAGE_UP, "page_up")     /* pgup */                                                            \
  OP(PAGE_DOWN, "page_down") /* pgdn */                                                            \
  OP(HOME, "home")                                                                                 \
  OP(END, "end")                                                                                   \
  /*OP(CAPS_LOCK)*/                  /* caps */                                                    \
  /*OP(SCROLL_LOCK, "scroll_lock")*/ /* scrlk */                                                   \
  /*OP(NUM_LOCK)*/                   /* numlk */                                                   \
  OP(PRINT_SCREEN, "print_screen")   /* prtsc */                                                   \
  OP(PAUSE, "pause")                 /* break */                                                   \
  OP(MENU, "menu")                                                                                 \
  OP(F1, "f1")                                                                                     \
  OP(F2, "f2")                                                                                     \
  OP(F3, "f3")                                                                                     \
  OP(F4, "f4")                                                                                     \
  OP(F5, "f5")                                                                                     \
  OP(F6, "f6")                                                                                     \
  OP(F7, "f7")                                                                                     \
  OP(F8, "f8")                                                                                     \
  OP(F9, "f9")                                                                                     \
  OP(F10, "f10")                                                                                   \
  OP(F11, "f11")                                                                                   \
  OP(F12, "f12")                                                                                   \
  OP(BRACKET_START, "bracket_start") /* for bracket-paste mode */

enum class FunctionKey : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_FUNCTION_KEY(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(FunctionKey funcKey);

class KeyEvent {
private:
  static constexpr const char *BRACKET_START = "\x1b[200~";
  static constexpr unsigned int CODE_MASK = (1u << 24) - 1; // 23bit
  static_assert(sizeof(FunctionKey) * 8 <= 23);

  /**
   * | 1bit (if 1, function key) | 23bit (code point or function key) | 8bit (modifiers)
   */
  unsigned int value{0};

public:
  static Optional<KeyEvent> fromEscapeSeq(StringRef seq);

  static Optional<KeyEvent> fromKeyName(StringRef ref, std::string *err);

  /**
   * parse caret notation
   * @param caret
   * @return
   * if invalid caret notation, return empty string
   */
  static std::string parseCaret(StringRef caret);

  static std::string toCaret(StringRef value);

  constexpr KeyEvent() = default;

  constexpr explicit KeyEvent(int codePoint, ModifierKey modifiers = {})
      : value((static_cast<unsigned int>(codePoint) & CODE_MASK) << 8 | toUnderlying(modifiers)) {}

  constexpr explicit KeyEvent(FunctionKey funcKey, ModifierKey modifiers = {})
      : value((static_cast<unsigned int>(funcKey) & CODE_MASK) << 8 | toUnderlying(modifiers) |
              1u << 31u) {}

  bool isFuncKey() const { return this->value & (1u << 31u); }

  bool isCodePoint() const { return !this->isFuncKey(); }

  bool isBracketedPasteStart() const {
    return this->isFuncKey() && this->asFuncKey() == FunctionKey::BRACKET_START;
  }

  ModifierKey modifiers() const { return static_cast<ModifierKey>(this->value & 0xFF); }

  bool hasModifier(ModifierKey modifier) const { return hasFlag(this->modifiers(), modifier); }

  int asCodePoint() const {
    assert(!this->isFuncKey());
    return static_cast<int>(this->asCode());
  }

  FunctionKey asFuncKey() const {
    assert(this->isFuncKey());
    return static_cast<FunctionKey>(this->asCode());
  }

  bool operator==(const KeyEvent &o) const { return this->value == o.value; }

  unsigned int rawValue() const { return this->value; }

  std::string toString() const;

  struct Hasher {
    size_t operator()(const KeyEvent &event) const {
      return std::hash<unsigned int>()(event.rawValue());
    }
  };

private:
  unsigned int asCode() const { return (this->value >> 8) & CODE_MASK; }
};

class KeyCodeReader {
public:
  static constexpr int DEFAULT_READ_TIMEOUT_MSEC = 100;

private:
  int fd{-1};
  int timeout{DEFAULT_READ_TIMEOUT_MSEC};
  std::string keycode;      // single utf8 character (maybe raw bytes) or escape sequence
  Optional<KeyEvent> event; // recognized key event (via ANSI Escape Sequence)

public:
  explicit KeyCodeReader(int fd) : fd(fd) {}

  void setTimeout(int t) { this->timeout = t; }

  int getTimeout() const { return this->timeout; }

  bool empty() const { return this->keycode.empty(); }

  const std::string &get() const { return this->keycode; }

  Optional<KeyEvent> getEvent() const { return this->event; }

  void clear() {
    this->keycode.clear();
    this->event = {};
  }

  bool hasControlChar() const { return !this->empty() && isControlChar(this->keycode[0]); }

  bool hasEscapeSeq() const { return !this->empty() && isEscapeChar(this->keycode[0]); }

  /**
   * fetch code
   * @param watchSigSet
   * @return
   * size of read
   * if read failed, return -1
   */
  ssize_t fetch(AtomicSigSet &&watchSigSet);

  ssize_t fetch() {
    AtomicSigSet set;
    set.add(SIGWINCH);
    return this->fetch(std::move(set));
  }

  bool hasBracketedPasteStart() const {
    return this->event.hasValue() && this->event.unwrap().isBracketedPasteStart();
  }

  /**
   * for `Report all keys as escape sequence` feature in kitty keyboard protocol.
   * get non-modified code point
   * @return
   * if failed, return -1
   */
  int getEscapedPlainCodePoint() const {
    if (!this->event.hasValue()) {
      return -1;
    }
    const auto e = this->event.unwrap();
    if (!e.isCodePoint()) {
      return -1;
    }
    int codePoint = e.asCodePoint();
    if (const auto m = e.modifiers(); m == ModifierKey{}) {
      return codePoint;
    } else if (m == ModifierKey::SHIFT && isShiftable(codePoint)) {
      return (codePoint - 'a') + 'A';
    }
    return -1;
  }

  template <typename Func>
  static constexpr bool consumer_requirement_v =
      std::is_same_v<bool, std::invoke_result_t<Func, StringRef>>;

  template <typename Func, enable_when<consumer_requirement_v<Func>> = nullptr>
  bool intoBracketedPasteMode(Func func) const {
    constexpr char ENTER = 13;
    constexpr char ESC = 27;

    errno = 0;
    bool noMem = false;
    while (true) {
      char buf;
      if (ssize_t r = readRetryWithTimeout(this->fd, &buf, 1, this->timeout); r <= 0) {
        if (r < 0) {
          if (r == -2) {
            errno = ETIME;
          }
          return false;
        }
        goto END;
      }
      switch (buf) {
      case ENTER:
        if (!func({"\n", 1})) { // insert \n instead of \r
          noMem = true;
        }
        continue;
      case ESC: { // bracket stop \e[201~
        char seq[6];
        seq[0] = '\x1b';
        constexpr char expect[] = {'[', '2', '0', '1', '~'};
        unsigned int count = 0;
        for (; count < std::size(expect); count++) {
          if (ssize_t r = readRetryWithTimeout(this->fd, seq + count + 1, 1, this->timeout);
              r <= 0) {
            if (r < 0) {
              if (r == -2) {
                errno = ETIME;
              }
              return false;
            }
            goto END;
          }
          if (seq[count + 1] != expect[count]) {
            if (!func({seq, count + 2})) {
              noMem = true;
            }
            break;
          }
        }
        if (count == std::size(expect)) {
          goto END; // end bracket paste mode
        }
        continue;
      }
      default:
        if (!func({&buf, 1})) {
          noMem = true;
        }
        break;
      }
    }

  END:
    if (noMem) {
      errno = ENOMEM;
      return false;
    }
    return true;
  }
};

} // namespace arsh

#endif // ARSH_KEYCODE_H
