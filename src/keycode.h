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

#include <unordered_map>
#include <vector>

#include "misc/detect.hpp"
#include "misc/enum_util.hpp"
#include "misc/flag_util.hpp"
#include "misc/resource.hpp"
#include "misc/result.hpp"
#include "signals.h"

namespace arsh {

inline bool isControlChar(int ch) { return (ch >= 0 && ch <= 31) || ch == 127; }

inline bool isEscapeChar(int ch) { return ch == '\x1b'; }

inline bool isCaretTarget(int ch) { return (ch >= '@' && ch <= '_') || ch == '?'; }

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
  OP(META, (1u << 5u), "meta")                                                                     \
  OP(CAPS, (1u << 6u), "caps_lock")                                                                \
  OP(NUM, (1u << 7u), "num_lock")

// for kitty keyboard protocol
enum class ModifierKey : unsigned char {
#define GEN_ENUM(E, D, S) E = (D),
  EACH_MODIFIER_KEY(GEN_ENUM)
#undef GEN_ENUM
};

template <>
struct allow_enum_bitop<ModifierKey> : std::true_type {};

#define EACH_FUNCTION_KEY(OP)                                                                      \
  OP(ESCAPE, "esc")                                                                                \
  OP(ENTER, "")                                                                                    \
  OP(TAB, "")                                                                                      \
  OP(BACKSPACE, "bs")                                                                              \
  OP(INSERT, "ins")                                                                                \
  OP(DELETE, "del")                                                                                \
  OP(LEFT, "")                                                                                     \
  OP(RIGHT, "")                                                                                    \
  OP(UP, "")                                                                                       \
  OP(DOWN, "")                                                                                     \
  OP(PAGE_UP, "pgup")                                                                              \
  OP(PAGE_DOWN, "pgdn")                                                                            \
  OP(HOME, "")                                                                                     \
  OP(END, "")                                                                                      \
  OP(CAPS_LOCK, "caps")                                                                            \
  OP(SCROLL_LOCK, "scrlk")                                                                         \
  OP(NUM_LOCK, "numlk")                                                                            \
  OP(PRINT_SCREEN, "prtsc")                                                                        \
  OP(PAUSE, "break")                                                                               \
  OP(MENU, "")                                                                                     \
  OP(F1, "")                                                                                       \
  OP(F2, "")                                                                                       \
  OP(F3, "")                                                                                       \
  OP(F4, "")                                                                                       \
  OP(F5, "")                                                                                       \
  OP(F6, "")                                                                                       \
  OP(F7, "")                                                                                       \
  OP(F8, "")                                                                                       \
  OP(F9, "")                                                                                       \
  OP(F10, "")                                                                                      \
  OP(F11, "")                                                                                      \
  OP(F12, "")                                                                                      \
  OP(BRACKET_START, "") /* for bracket-paste mode */

enum class FunctionKey : unsigned char {
#define GEN_ENUM(E, A) E,
  EACH_FUNCTION_KEY(GEN_ENUM)
#undef GEN_ENUM
};

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

  static Optional<KeyEvent> parseHRNotation(StringRef ref, std::string *err);

  constexpr KeyEvent() = default;

  constexpr explicit KeyEvent(int codePoint, ModifierKey modifiers = {})
      : value((static_cast<unsigned int>(codePoint) & CODE_MASK) << 8 | toUnderlying(modifiers)) {}

  constexpr explicit KeyEvent(FunctionKey funcKey, ModifierKey modifiers = {})
      : value((static_cast<unsigned int>(funcKey) & CODE_MASK) << 8 | toUnderlying(modifiers) |
              1u << 31u) {}

  bool isFuncKey() const { return this->value & (1u << 31u); }

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

  void clear() { this->keycode.clear(); }

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

#define EACH_EDIT_ACTION_TYPE(OP)                                                                  \
  OP(ACCEPT, "accept")                             /* ENTER / CTRL-M / CTRL-J */                   \
  OP(CANCEL, "cancel")                             /* CTRL-C */                                    \
  OP(COMPLETE, "complete")                         /* TAB / CTRL-I */                              \
  OP(BACKWARD_DELETE_CHAR, "backward-delete-char") /* CTRL-H / BACKSPACE */                        \
  OP(DELETE_CHAR, "delete-char")                   /* DELETE */                                    \
  OP(DELETE_OR_EXIT, "delete-or-exit")             /* CTRL-D */                                    \
  OP(TRANSPOSE_CHAR, "transpose-char")             /* CTRL-T */                                    \
  OP(BACKWARD_CHAR, "backward-char")               /* CTRL-B / LEFT */                             \
  OP(FORWARD_CHAR, "forward-char")                 /* CTRL-F / RIGHT */                            \
  OP(PREV_HISTORY, "prev-history")                 /* ALT-UP */                                    \
  OP(NEXT_HISTORY, "next-history")                 /* ALT-DOWN */                                  \
  OP(UP_OR_HISTORY, "up-or-history")               /* CTRL-P / UP */                               \
  OP(DOWN_OR_HISTORY, "down-or-history")           /* CTRL-N / DOWN */                             \
  OP(BACKWORD_KILL_LINE, "backward-kill-line")     /* CTRL-U */                                    \
  OP(KILL_LINE, "kill-line")                       /* CTRL-K */                                    \
  OP(BEGINNING_OF_LINE, "beginning-of-line")       /* CTRL-A / HOME */                             \
  OP(END_OF_LINE, "end-of-line")                   /* CTRL-E / EMD */                              \
  OP(BEGINNING_OF_BUF, "beginning-of-buffer")      /* ALT-< */                                     \
  OP(END_OF_BUF, "end-of-buffer")                  /* ALT-> */                                     \
  OP(CLEAR_SCREEN, "clear-screen")                 /* CTRL-L */                                    \
  OP(BACKWARD_KILL_WORD, "backward-kill-word")     /* CTRL-W */                                    \
  OP(KILL_WORD, "kill-word")                       /* ALT-D */                                     \
  OP(BACKWARD_WORD, "backward-word")               /* ALT-B / ALT-LEFT */                          \
  OP(FORWARD_WORD, "forward-word")                 /* ALT-F / ALT-RIGHT */                         \
  OP(BACKWARD_KILL_TOKEN, "backward-kill-token")                                                   \
  OP(KILL_TOKEN, "kill-token")                                                                     \
  OP(BACKWARD_TOKEN, "backward-token")                                                             \
  OP(FORWARD_TOKEN, "forward-token")                                                               \
  OP(NEWLINE, "newline")               /* ALT-ENTER */                                             \
  OP(YANK, "yank")                     /* CTRL-Y */                                                \
  OP(YANK_POP, "yank-pop")             /* ALT-Y */                                                 \
  OP(UNDO, "undo")                     /* CTRL-Z */                                                \
  OP(REDO, "redo")                     /* CTRL-_ */                                                \
  OP(INSERT_KEYCODE, "insert-keycode") /* CTRL-V */                                                \
  OP(BRACKET_PASTE, "bracket-paste")   /* ESC [200~ */                                             \
  OP(CUSTOM, "%custom")                /* for custom action */

enum class EditActionType : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_EDIT_ACTION_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

#define EACH_CUSTOM_ACTION_TYPE(OP)                                                                \
  OP(REPLACE_WHOLE, "replace-whole")                                                               \
  OP(REPLACE_WHOLE_ACCEPT, "replace-whole-accept")                                                 \
  OP(REPLACE_LINE, "replace-line")                                                                 \
  OP(INSERT, "insert")                                                                             \
  OP(HIST_SELCT, "hist-select")                                                                    \
  OP(KILL_RING_SELECT, "kill-ring-select")

enum class CustomActionType : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_CUSTOM_ACTION_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(EditActionType action);

struct EditAction {
  EditActionType type;

  CustomActionType customActionType; // for custom action
  unsigned int customActionIndex;    // for custom action

  EditAction(EditActionType type) // NOLINT
      : type(type), customActionType{}, customActionIndex(0) {}

  EditAction(CustomActionType type, unsigned int index)
      : type(EditActionType::CUSTOM), customActionType(type), customActionIndex(index) {}
};

enum class EditActionStatus : unsigned char {
  OK,
  ERROR,    // interval error during edit action
  CANCEL,   // cancel edit action
  CONTINUE, // continue completion candidates paging
  REVERT,   // revert previous edit op
};

class CustomActionMap {
private:
  StrRefMap<unsigned int> indexes;
  std::vector<std::pair<CStrPtr, EditAction>> entries;

public:
  const auto &getEntries() const { return this->entries; }

  size_t size() const { return this->entries.size(); }

  const std::pair<CStrPtr, EditAction> *find(StringRef ref) const;

  const std::pair<CStrPtr, EditAction> *findByIndex(unsigned int index) const;

  const std::pair<CStrPtr, EditAction> *add(StringRef name, CustomActionType type);

  /**
   * remove custom action
   * @param ref
   * @return
   * return the removed custom action index.
   * if not found, return -1
   */
  int remove(StringRef ref);
};

enum class PagerAction : unsigned char {
  SELECT,
  CANCEL,
  ESCAPE,
  PREV,
  NEXT,
  LEFT,
  RIGHT,
};

class KeyBindings {
public:
  static constexpr const char *BRACKET_START = "\x1b[200~";

private:
  /**
   * keycode to edit action mapping
   */
  std::unordered_map<std::string, EditAction> values;

  /**
   * custom action name to action properties mapping
   */
  CustomActionMap customActions;

  /**
   * keycode to pager action mapping
   */
  std::unordered_map<std::string, PagerAction> pagerValues;

public:
  static const StrRefMap<EditActionType> &getEditActionTypes();

  static const StrRefMap<CustomActionType> &getCustomActionTypes();

  /**
   * parse caret notation
   * @param caret
   * @return
   * if invalid caret notation, return empty string
   */
  static std::string parseCaret(StringRef caret);

  static std::string toCaret(StringRef value);

  KeyBindings();

  const EditAction *findAction(const std::string &keycode) const;

  const PagerAction *findPagerAction(const std::string &keycode) const;

  enum class AddStatus : unsigned char {
    OK,
    UNDEF,
    FORBID_BRACKET_START_CODE,
    FORBID_BRACKET_ACTION,
    INVALID_START_CHAR,
    INVALID_ASCII,
    LIMIT,
  };

  /**
   *
   * @param caret
   * maybe caret notation
   * @param name
   * must be an action name or empty
   * if a name is empty, clear binding
   * @return
   */
  AddStatus addBinding(StringRef caret, StringRef name);

  enum class DefineError : unsigned char {
    INVALID_NAME,
    INVALID_TYPE,
    DEFINED,
    LIMIT,
  };

  Result<unsigned int, DefineError> defineCustomAction(StringRef name, StringRef type);

  int removeCustomAction(StringRef name);

  template <typename Func>
  static constexpr bool binding_consumer_requirement_v =
      std::is_same_v<void, std::invoke_result_t<Func, StringRef, StringRef>>;

  template <typename Func, enable_when<binding_consumer_requirement_v<Func>> = nullptr>
  void fillBindings(Func func) const {
    for (auto &e : this->values) {
      auto caret = toCaret(e.first);
      const char *action = toString(e.second.type);
      if (e.second.type == EditActionType::CUSTOM) {
        action = this->customActions.findByIndex(e.second.customActionIndex)->first.get();
      }
      func(caret, action);
    }
  }

  template <typename Func>
  static constexpr bool action_consumer_requirement_v =
      std::is_same_v<void, std::invoke_result_t<Func, StringRef>>;

  template <typename Func, enable_when<action_consumer_requirement_v<Func>> = nullptr>
  void fillActions(Func func) const {
    for (auto &e : getEditActionTypes()) {
      if (e.second == EditActionType::CUSTOM) {
        continue;
      }
      func(e.first);
    }
    for (auto &e : this->customActions.getEntries()) {
      func(e.first.get());
    }
  }
};

} // namespace arsh

#endif // ARSH_KEYCODE_H
