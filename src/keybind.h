/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#ifndef ARSH_KEYBIND_H
#define ARSH_KEYBIND_H

#include <unordered_map>
#include <vector>

#include "misc/detect.hpp"
#include "misc/resource.hpp"
#include "misc/result.hpp"
#include "misc/string_ref.hpp"

#include "keycode.h"

namespace arsh {

#define EACH_EDIT_ACTION_TYPE(OP)                                                                  \
  OP(ACCEPT, "accept")                             /* ENTER / CTRL-M / CTRL-J */                   \
  OP(CANCEL, "cancel")                             /* CTRL-C */                                    \
  OP(REVERT, "revert")                             /* ESC */                                       \
  OP(COMPLETE, "complete")                         /* TAB / CTRL-I */                              \
  OP(COMPLETE_BACKWARD, "complete-backward")       /* SHIFT-TAB */                                 \
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
  OP(BACKWARD_KILL_LINE, "backward-kill-line")     /* CTRL-U */                                    \
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

class KeyBindings {
private:
  template <typename T>
  using KeyEventMap = std::unordered_map<KeyEvent, T, KeyEvent::Hasher>;

  /**
   * keycode to edit action mapping
   */
  KeyEventMap<EditAction> values;

  /**
   * custom action name to action properties mapping
   */
  CustomActionMap customActions;

public:
  static const StrRefMap<EditActionType> &getEditActionTypes();

  static const StrRefMap<CustomActionType> &getCustomActionTypes();

  KeyBindings();

  const EditAction *findAction(KeyEvent event) const;

  const EditAction *findAction(const Optional<KeyEvent> &event) const {
    return event.hasValue() ? this->findAction(event.unwrap()) : nullptr;
  }

  /**
   *
   * @param key
   * maybe caret notation
   * @param name
   * must be an action name or empty
   * if a name is empty, clear binding
   * @param err
   * maybe null
   * @return
   *  if failed, return false
   */
  bool addBinding(StringRef key, StringRef name, std::string *err);

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
      std::is_same_v<void, std::invoke_result_t<Func, std::string &&, std::string &&>>;

  template <typename Func, enable_when<binding_consumer_requirement_v<Func>> = nullptr>
  void fillBindings(Func func) const {
    for (auto &e : this->values) {
      auto key = e.first.toString();
      std::string action = toString(e.second.type);
      if (e.second.type == EditActionType::CUSTOM) {
        action = this->customActions.findByIndex(e.second.customActionIndex)->first.get();
      }
      func(std::move(key), std::move(action));
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

private:
  enum class AddStatus : unsigned char {
    OK,
    UNDEF,
    LIMIT,
  };

  /**
   *
   * @param event
   * must be a valid key event
   * @param name
   * must be an action name or empty
   * if a name is empty, clear binding
   * @return
   */
  AddStatus addBinding(KeyEvent event, StringRef name);
};

} // namespace arsh

#endif // ARSH_KEYBIND_H
