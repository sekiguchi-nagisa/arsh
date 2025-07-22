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

#include "keybind.h"
#include "constant.h"
#include "format_util.h"
#include "misc/format.hpp"

namespace arsh {

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
      {ESC_ "[3~", EditActionType::DELETE_CHAR},
      {ESC_ "[1;3A", EditActionType::PREV_HISTORY},   // alt+up
      {ESC_ "[1;3B", EditActionType::NEXT_HISTORY},   // alt+down
      {ESC_ "[1;3D", EditActionType::BACKWARD_WORD},  // alt+left
      {ESC_ "[1;3C", EditActionType::FORWARD_WORD},   // alt+right
      {ESC_ "[A", EditActionType::UP_OR_HISTORY},     // up
      {ESC_ "[B", EditActionType::DOWN_OR_HISTORY},   // down
      {ESC_ "[D", EditActionType::BACKWARD_CHAR},     // left
      {ESC_ "[C", EditActionType::FORWARD_CHAR},      // right
      {ESC_ "[H", EditActionType::BEGINNING_OF_LINE}, // home
      {ESC_ "[F", EditActionType::END_OF_LINE},       // end
  };
  for (auto &e : entries) {
    auto event = KeyEvent::fromEscapeSeq(e.key);
    assert(event.hasValue());
    auto pair = this->values.emplace(event.unwrap(), e.type);
    static_cast<void>(pair);
    assert(pair.second);
  }

  // define pager action
  constexpr struct {
    const char key[7];
    PagerAction action;
  } pagers[] = {
      {ENTER_, PagerAction::SELECT},      {CTRL_J_, PagerAction::SELECT},
      {CTRL_C_, PagerAction::CANCEL},     {ESC_, PagerAction::ESCAPE},
      {TAB_, PagerAction::NEXT},          {ESC_ "[Z", PagerAction::PREV}, // shift-tab
      {CTRL_P_, PagerAction::PREV},       {CTRL_N_, PagerAction::NEXT},

      {ESC_ "[1;3A", PagerAction::PREV},  // alt+up
      {ESC_ "[1;3B", PagerAction::NEXT},  // alt+down
      {ESC_ "[1;3D", PagerAction::LEFT},  // alt+left
      {ESC_ "[1;3C", PagerAction::RIGHT}, // alt+right
      {ESC_ "[A", PagerAction::PREV},     // up
      {ESC_ "[B", PagerAction::NEXT},     // down
      {ESC_ "[D", PagerAction::LEFT},     // left
      {ESC_ "[C", PagerAction::RIGHT},    // right
  };
  for (auto &e : pagers) {
    auto event = KeyEvent::fromEscapeSeq(e.key);
    assert(event.hasValue());
    auto pair = this->pagerValues.emplace(event.unwrap(), e.action);
    (void)pair;
    assert(pair.second);
  }
}

const EditAction *KeyBindings::findAction(KeyEvent event) const {
  if (auto iter = this->values.find(event); iter != this->values.end()) {
    return &iter->second;
  }
  return nullptr;
}

const PagerAction *KeyBindings::findPagerAction(KeyEvent event) const {
  if (auto iter = this->pagerValues.find(event); iter != this->pagerValues.end()) {
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

KeyBindings::AddStatus KeyBindings::addBinding(const KeyEvent event, const StringRef name) {
  auto &actionMap = getEditActionTypes();
  if (name.empty()) {
    this->values.erase(event);
  } else {
    EditAction action = EditActionType::ACCEPT;
    if (auto iter = actionMap.find(name); iter != actionMap.end()) {
      action = iter->second;
    } else if (auto *e = this->customActions.find(name); e) {
      action = e->second;
    } else {
      return AddStatus::UNDEF;
    }
    if (auto iter = this->values.find(event); iter != this->values.end()) {
      iter->second = action; // overwrite previous bind
    } else {
      if (this->values.size() == SYS_LIMIT_KEY_BINDING_MAX) {
        return AddStatus::LIMIT;
      }
      this->values.emplace(event, action);
    }
  }
  return AddStatus::OK;
}

bool KeyBindings::addBinding(StringRef key, StringRef name, std::string *err) {
  const char *header = "";
  std::string dummy;
  char tail = '\'';

  Optional<KeyEvent> event;
  if (key.size() > 1 && key[0] == '^') { // caret notation
    auto seq = KeyEvent::parseCaret(key);
    event = KeyEvent::fromEscapeSeq(seq);
    if (!event.hasValue() || event.unwrap().isBracketPasteStart()) {
      header = "unrecognized escape sequence: `";
      goto ERROR;
    }
  } else { // keyname
    event = KeyEvent::fromKeyName(key, &dummy);
    if (!event.hasValue()) {
      key = dummy;
      tail = '\0';
      goto ERROR;
    }
  }
  if (const auto e = event.unwrap(); e.isCodePoint()) {
    if (!isAsciiPrintable(e.asCodePoint())) { // kitty protocol supports arbitrary code points
      header = "keycode must be ascii printable char or function key: `";
      dummy = e.toString();
      key = dummy;
      goto ERROR;
    }
    if (const auto m = e.modifiers(); m == ModifierKey{} || m == ModifierKey::SHIFT) {
      header = "ascii keycode needs at-least one modifier (except for shift): `";
      dummy = e.toString();
      key = dummy;
      goto ERROR;
    }
  }

  switch (this->addBinding(event.unwrap(), name)) {
  case AddStatus::OK:
    return true;
  case AddStatus::UNDEF:
    header = "undefined edit action: `";
    key = name;
    break;
  case AddStatus::LIMIT:
    header = "number of key bindings reaches limit (up to ";
    dummy = std::to_string(SYS_LIMIT_KEY_BINDING_MAX);
    key = dummy;
    tail = ')';
    break;
  }

ERROR:
  if (err) {
    *err = header;
    appendAsPrintable(key, SYS_LIMIT_ERROR_MSG_MAX - 1, *err);
    if (tail) {
      *err += tail;
    }
  }
  return false;
}

Result<unsigned int, KeyBindings::DefineError> KeyBindings::defineCustomAction(StringRef name,
                                                                               StringRef type) {
  // check an action name format
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

  // check already the defined action name
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