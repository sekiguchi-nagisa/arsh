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

#include <functional>
#include <unordered_map>
#include <vector>

#include "misc/detect.hpp"
#include "misc/resource.hpp"
#include "misc/result.hpp"
#include "misc/string_ref.hpp"

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
  OP(CLEAR_SCREEN, "clear-screen")                 /* CTRL-L */                                    \
  OP(BACKWARD_KILL_WORD, "backward-kill-word")     /* CTRL-W */                                    \
  OP(KILL_WORD, "kill-word")                       /* ALT-D */                                     \
  OP(BACKWARD_WORD, "backward-word")               /* ALT-B / ALT-LEFT */                          \
  OP(FORWARD_WORD, "forward-word")                 /* ALT-F / ALT-RIGHT */                         \
  OP(NEWLINE, "newline")                           /* ALT-ENTER */                                 \
  OP(BRACKET_PASTE, "bracket-paste")               /* ESC [200~ */                                 \
  OP(CUSTOM, "%custom")                            /* for custom action */

enum class EditActionType : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_EDIT_ACTION_TYPE(GEN_ENUM)
#undef GEN_ENUM
};

#define EACH_CUSTOM_ACTION_TYPE(OP)                                                                \
  OP(REPLACE_WHOLE, "replace-whole")                                                               \
  OP(REPLACE_WHOLE_ACCEPT, "replace-whole-accept")                                                 \
  OP(REPLACE_LINE, "replace-line")                                                                 \
  OP(INSERT, "insert")

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

class CustomActionMap {
private:
  StrRefMap<unsigned int> indexes;
  std::vector<std::pair<CStrPtr, EditAction>> entries;

public:
  const auto &getEntries() const { return this->entries; }

  size_t size() const { return this->entries.size(); }

  const std::pair<CStrPtr, EditAction> *find(StringRef ref) const;

  const auto &operator[](size_t index) const { return this->entries[index]; }

  const std::pair<CStrPtr, EditAction> *add(StringRef name, CustomActionType type);
};

class KeyBindings {
public:
  static constexpr const char *CTRL_C = "\x03";
  static constexpr const char *TAB = "\x09";
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

  const EditAction *findAction(const std::string &keycode);

  enum class AddStatus {
    OK,
    UNDEF,
    FORBIT_BRACKET_START_CODE,
    FORBTT_BRACKET_ACTION,
    INVALID_START_CHAR,
    INVALID_ASCII,
    LIMIT,
  };

  /**
   *
   * @param caret
   * may be caret notation
   * @param name
   * must be action name or empty
   * if name is empty, clear binding
   * @return
   */
  AddStatus addBinding(StringRef caret, StringRef name);

  enum class DefineError {
    INVALID_NAME,
    INVALID_TYPE,
    DEFINED,
    LIMIT,
  };

  Result<unsigned int, DefineError> defineCustomAction(StringRef name, StringRef type);

  template <typename Func>
  static constexpr bool binding_consumer_requirement_v =
      std::is_same_v<void, std::invoke_result_t<Func, StringRef, StringRef>>;

  template <typename Func, enable_when<binding_consumer_requirement_v<Func>> = nullptr>
  void fillBindings(Func func) const {
    for (auto &e : this->values) {
      auto caret = toCaret(e.first);
      const char *action = toString(e.second.type);
      if (e.second.type == EditActionType::CUSTOM) {
        action = this->customActions[e.second.customActionIndex].first.get();
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

} // namespace ydsh

#endif // YDSH_KEYCODE_H
