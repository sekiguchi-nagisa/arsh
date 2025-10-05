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

KeyBindings::KeyBindings() {
  // define edit action
  constexpr struct {
    KeyEvent event;
    EditActionType type;
  } entries[] = {
      // control character (ctrl+)
      {KeyEvent(FunctionKey::ENTER), EditActionType::ACCEPT}, // normally via enter (^M)
      {KeyEvent('j', ModifierKey::CTRL), EditActionType::ACCEPT},
      {KeyEvent('c', ModifierKey::CTRL), EditActionType::CANCEL},
      {KeyEvent(FunctionKey::TAB), EditActionType::COMPLETE}, // normally via tab (^I)
      {KeyEvent('h', ModifierKey::CTRL), EditActionType::BACKWARD_DELETE_CHAR},
      {KeyEvent(FunctionKey::BACKSPACE),
       EditActionType::BACKWARD_DELETE_CHAR}, // normally via backspace (^?)
      {KeyEvent('d', ModifierKey::CTRL), EditActionType::DELETE_OR_EXIT},
      {KeyEvent('t', ModifierKey::CTRL), EditActionType::TRANSPOSE_CHAR},
      {KeyEvent('b', ModifierKey::CTRL), EditActionType::BACKWARD_CHAR},
      {KeyEvent('f', ModifierKey::CTRL), EditActionType::FORWARD_CHAR},
      {KeyEvent('p', ModifierKey::CTRL), EditActionType::UP_OR_HISTORY},
      {KeyEvent('n', ModifierKey::CTRL), EditActionType::DOWN_OR_HISTORY},
      {KeyEvent('u', ModifierKey::CTRL), EditActionType::BACKWARD_KILL_LINE},
      {KeyEvent('k', ModifierKey::CTRL), EditActionType::KILL_LINE},
      {KeyEvent('a', ModifierKey::CTRL), EditActionType::BEGINNING_OF_LINE},
      {KeyEvent('e', ModifierKey::CTRL), EditActionType::END_OF_LINE},
      {KeyEvent('l', ModifierKey::CTRL), EditActionType::CLEAR_SCREEN},
      {KeyEvent(FunctionKey::BACKSPACE, ModifierKey::CTRL),
       EditActionType::BACKWARD_KILL_WORD}, // for kitty
      {KeyEvent('w', ModifierKey::CTRL), EditActionType::BACKWARD_KILL_WORD},
      {KeyEvent('v', ModifierKey::CTRL), EditActionType::INSERT_KEYCODE},
      {KeyEvent('y', ModifierKey::CTRL), EditActionType::YANK},
      {KeyEvent('z', ModifierKey::CTRL), EditActionType::UNDO},

      // escape sequence (alt+)
      {KeyEvent('b', ModifierKey::ALT), EditActionType::BACKWARD_WORD},
      {KeyEvent('f', ModifierKey::ALT), EditActionType::FORWARD_WORD},
      {KeyEvent('d', ModifierKey::ALT), EditActionType::KILL_WORD},
      {KeyEvent('y', ModifierKey::ALT), EditActionType::YANK_POP},
      {KeyEvent('/', ModifierKey::ALT), EditActionType::REDO},
      {KeyEvent(FunctionKey::ENTER, ModifierKey::ALT), EditActionType::NEWLINE}, // for kitty
      {KeyEvent('<', ModifierKey::ALT), EditActionType::BEGINNING_OF_BUF},
      {KeyEvent('>', ModifierKey::ALT), EditActionType::END_OF_BUF},
      {KeyEvent(FunctionKey::DELETE), EditActionType::DELETE_CHAR},
      {KeyEvent(FunctionKey::UP, ModifierKey::ALT), EditActionType::PREV_HISTORY},
      {KeyEvent(FunctionKey::DOWN, ModifierKey::ALT), EditActionType::NEXT_HISTORY},
      {KeyEvent(FunctionKey::LEFT, ModifierKey::ALT), EditActionType::BACKWARD_WORD},
      {KeyEvent(FunctionKey::RIGHT, ModifierKey::ALT), EditActionType::FORWARD_WORD},
      {KeyEvent(FunctionKey::UP), EditActionType::UP_OR_HISTORY},
      {KeyEvent(FunctionKey::DOWN), EditActionType::DOWN_OR_HISTORY},
      {KeyEvent(FunctionKey::LEFT), EditActionType::BACKWARD_CHAR},
      {KeyEvent(FunctionKey::RIGHT), EditActionType::FORWARD_CHAR},
      {KeyEvent(FunctionKey::HOME), EditActionType::BEGINNING_OF_LINE},
      {KeyEvent(FunctionKey::END), EditActionType::END_OF_LINE},
      {KeyEvent(FunctionKey::ESCAPE), EditActionType::PAGER_REVERT},
      {KeyEvent(FunctionKey::TAB, ModifierKey::SHIFT), EditActionType::COMPLETE_BACKWARD},
  };
  for (auto &e : entries) {
    const auto pair = this->values.emplace(e.event, e.type);
    static_cast<void>(pair);
    assert(pair.second);
  }
}

const EditAction *KeyBindings::findAction(KeyEvent event) const {
  if (auto iter = this->values.find(event); iter != this->values.end()) {
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
    if (!event.hasValue() || event.unwrap().isBracketedPasteStart()) {
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