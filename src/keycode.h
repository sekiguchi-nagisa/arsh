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

#ifndef YDSH_KEYCODE_H
#define YDSH_KEYCODE_H

#include <string>
#include <unordered_map>

namespace ydsh {

inline bool isControlChar(int ch) { return (ch >= 0 && ch <= 31) || ch == 127; }

inline bool isEscapeChar(int ch) { return ch == '\x1b'; }

class KeyCodeReader {
private:
  int fd{-1};
  std::string keycode; // single utf8 character or escape sequence

public:
  explicit KeyCodeReader(int fd) : fd(fd) {}

  bool empty() const { return this->keycode.empty(); }

  const std::string &get() const { return this->keycode; }

  std::string take() {
    std::string tmp;
    std::swap(tmp, this->keycode);
    return tmp;
  }

  void clear() { this->keycode.clear(); }

  bool hasControlChar() const { return !this->empty() && isControlChar(this->keycode[0]); }

  bool hasEscapeSeq() const { return !this->empty() && isEscapeChar(this->keycode[0]); }

  /**
   * fetch code
   * @return
   * size of read
   * if read failed, return -1
   * //FIXME: read timeout
   */
  ssize_t fetch();
};

#define EACH_EDIT_ACTION(OP)                                                                       \
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
  OP(SEARCH_HISTORY, "search-history")             /* CTRL-R */                                    \
  OP(BACKWORD_KILL_LINE, "backward-kill-line")     /* CTRL-U */                                    \
  OP(KILL_LINE, "kill-line")                       /* CTRL-K */                                    \
  OP(BEGINNING_OF_LINE, "beginning-of-line")       /* CTRL-A / HOME */                             \
  OP(END_OF_LINE, "end-of-line")                   /* CTRL-E / EMD */                              \
  OP(CLEAR_SCREEN, "clear-screen")                 /* CTRL-L */                                    \
  OP(BACKWARD_KILL_WORD, "backward-kill-word")     /* CTRL-W */                                    \
  OP(KILL_WORD, "kill-word")                       /* ALT-D */                                     \
  OP(BACKWARD_WORD, "backward-word")               /* ALT-B / ALT-LEFT */                          \
  OP(FORWARD_WORD, "forward-word")                 /* ALT-F / ALT-RIGHT */                         \
  OP(NEWLINE, "newline")                           /* ALT-ENTER */                                 \
  OP(BRACKET_PASTE, "bracket-paste")               /* ESC [200~ */

enum class EditAction : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_EDIT_ACTION(GEN_ENUM)
#undef GEN_ENUM
};

class KeyBinding {
public:
  static constexpr const char *CTRL_C = "\x03";
  static constexpr const char *TAB = "\x09";

private:
  /**
   * keycode to edit action mapping
   */
  std::unordered_map<std::string, EditAction> values;

public:
  KeyBinding();

  const EditAction *findAction(const std::string &keycode);
};

} // namespace ydsh

#endif // YDSH_KEYCODE_H
