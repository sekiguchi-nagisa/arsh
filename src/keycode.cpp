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

#include <poll.h>
#include <unistd.h>

#include <cassert>

#include "constant.h"
#include "keycode.h"
#include "misc/unicode.hpp"

namespace ydsh {

// ###########################
// ##     KeyCodeReader     ##
// ###########################

ssize_t readWithTimeout(int fd, char *buf, size_t bufSize, int timeoutMSec) {
  errno = 0;
  if (timeoutMSec > -1) {
    struct pollfd pollfd[1]{};
    pollfd[0].fd = fd;
    pollfd[0].events = POLLIN;
    if (int r = poll(pollfd, std::size(pollfd), timeoutMSec); r <= 0) {
      return r; // error or timeout
    }
  }
  return read(fd, buf, bufSize);
}

static ssize_t readBytes(int fd, char (&buf)[8]) {
  ssize_t readSize = readWithTimeout(fd, &buf[0], 1);
  if (readSize <= 0) {
    return readSize;
  }
  const unsigned int byteSize = UnicodeUtil::utf8ByteSize(buf[0]);
  for (unsigned int i = 1; i < byteSize; i++) {
    if (readWithTimeout(fd, &buf[i], 1, 100) <= 0) {
      break;
    }
    readSize++;
  }
  return readSize;
}

#define READ_BYTE(b, bs)                                                                           \
  do {                                                                                             \
    if (readWithTimeout(this->fd, (b) + (bs), 1, this->timeout) <= 0) {                            \
      goto END;                                                                                    \
    } else {                                                                                       \
      seqSize++;                                                                                   \
    }                                                                                              \
  } while (false)

ssize_t KeyCodeReader::fetch() {
  constexpr const char ESC = '\x1b';
  char buf[8];
  ssize_t readSize = readBytes(this->fd, buf);
  if (readSize <= 0) {
    return readSize;
  }
  assert(readSize > 0 && readSize < 5);
  this->keycode.assign(buf, static_cast<size_t>(readSize));
  if (isEscapeChar(buf[0])) {
    assert(readSize == 1);
    char seq[8];
    unsigned int seqSize = 0;
    READ_BYTE(seq, seqSize);
    if (seq[0] != '[' && seq[0] != 'O' && seq[0] != ESC) { // ESC ? sequence
      goto END;
    }

    READ_BYTE(seq, seqSize);
    if (seq[0] == '[') {                    // ESC [ sequence
      if (seq[1] >= '0' && seq[1] <= '9') { // ESC [ n x
        READ_BYTE(seq, seqSize);
        if ((seq[1] == '2' && seq[2] == '0') ||
            (seq[1] == '1' && seq[2] == ';')) { // ESC [200~ or ESC [1;3A
          READ_BYTE(seq, seqSize);
          READ_BYTE(seq, seqSize);
          goto END;
        }
      } else { // ESC [ x
        goto END;
      }
    } else if (seq[0] == 'O') { // ESC O sequence
      goto END;
    } else if (seq[0] == ESC) {
      if (seq[1] == '[') { // ESC ESC [ ? sequence
        READ_BYTE(seq, seqSize);
        goto END;
      }
    }
  END:
    this->keycode.append(seq, seqSize);
  }
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

const std::pair<CStrPtr, EditAction> *CustomActionMap::add(StringRef name, CustomActionType type) {
  auto index = static_cast<unsigned int>(this->entries.size());
  auto entry = std::make_pair(CStrPtr(strdup(name.data())), EditAction(type, index));
  if (auto pair = this->indexes.emplace(entry.first.get(), index); pair.second) {
    this->entries.push_back(std::move(entry));
    return &this->entries.back();
  }
  return nullptr;
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
#define ESC_ "\x1b"
#define BACKSPACE_ "\x7F"

KeyBindings::KeyBindings() {
  // define edit action
  constexpr struct {
    const char *key;
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

      // escape sequence
      {ESC_ "b", EditActionType::BACKWARD_WORD},
      {ESC_ "f", EditActionType::FORWARD_WORD},
      {ESC_ "d", EditActionType::KILL_WORD},
      //      {ESC_ "y", EditActionType::YANK_POP}, //FIXME:
      {ESC_ ENTER_, EditActionType::NEWLINE},
      {ESC_ "<", EditActionType::BEGINNING_OF_BUF},
      {ESC_ ">", EditActionType::END_OF_BUF},
      {ESC_ "[1~", EditActionType::BEGINNING_OF_LINE},
      {ESC_ "[4~", EditActionType::END_OF_LINE}, // for putty
      {ESC_ "[3~", EditActionType::DELETE_CHAR}, // for putty
      {ESC_ "[200~", EditActionType::BRACKET_PASTE},
      {ESC_ "[1;3A", EditActionType::PREV_HISTORY},
      {ESC_ "[1;3B", EditActionType::NEXT_HISTORY},
      {ESC_ "[1;3D", EditActionType::BACKWARD_WORD},
      {ESC_ "[1;3C", EditActionType::FORWARD_WORD},
      {ESC_ "[A", EditActionType::UP_OR_HISTORY},
      {ESC_ "[B", EditActionType::DOWN_OR_HISTORY},
      {ESC_ "[D", EditActionType::BACKWARD_CHAR},
      {ESC_ "[C", EditActionType::FORWARD_CHAR},
      {ESC_ "[H", EditActionType::BEGINNING_OF_LINE},
      {ESC_ "[F", EditActionType::END_OF_LINE},
      {ESC_ "OH", EditActionType::BEGINNING_OF_LINE},
      {ESC_ "OF", EditActionType::END_OF_LINE},
      {ESC_ ESC_ "[A", EditActionType::PREV_HISTORY},  // for mac
      {ESC_ ESC_ "[B", EditActionType::NEXT_HISTORY},  // for mac
      {ESC_ ESC_ "[D", EditActionType::BACKWARD_WORD}, // for mac
      {ESC_ ESC_ "[C", EditActionType::FORWARD_WORD},  // for mac
  };
  for (auto &e : entries) {
    auto pair = this->values.emplace(e.key, e.type);
    (void)pair;
    assert(pair.second);
  }

  // define pager action
  constexpr struct {
    const char *key;
    PagerAction action;
  } pagers[] = {
      {ENTER_, PagerAction::SELECT},
      {CTRL_J_, PagerAction::SELECT},
      {CTRL_C_, PagerAction::CANCEL},
      {ESC_, PagerAction::CANCEL},
      {TAB_, PagerAction::NEXT},
      {ESC_ "[Z", PagerAction::PREV}, // shift-tab
      {CTRL_P_, PagerAction::PREV},
      {CTRL_N_, PagerAction::NEXT},

      {ESC_ "[1;3A", PagerAction::PREV},
      {ESC_ "[1;3B", PagerAction::NEXT},
      {ESC_ "[1;3D", PagerAction::LEFT},
      {ESC_ "[1;3C", PagerAction::RIGHT},
      {ESC_ "[A", PagerAction::PREV},
      {ESC_ "[B", PagerAction::NEXT},
      {ESC_ "[D", PagerAction::LEFT},
      {ESC_ "[C", PagerAction::RIGHT},

      // for mac
      {ESC_ ESC_ "[A", PagerAction::PREV},
      {ESC_ ESC_ "[B", PagerAction::NEXT},
      {ESC_ ESC_ "[D", PagerAction::LEFT},
      {ESC_ ESC_ "[C", PagerAction::RIGHT},
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
  const char *table[] = {
#define GEN_TABLE(E, S) S,
      EACH_EDIT_ACTION_TYPE(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[static_cast<unsigned int>(action)];
}

const StrRefMap<EditActionType> &KeyBindings::getEditActionTypes() {
  static StrRefMap<EditActionType> actions = {
#define GEN_ENTRY(E, S) {S, EditActionType::E},
      EACH_EDIT_ACTION_TYPE(GEN_ENTRY)
#undef GEN_ENTRY
  };
  return actions;
}

const StrRefMap<CustomActionType> &KeyBindings::getCustomActionTypes() {
  static StrRefMap<CustomActionType> types = {
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
    if (std::isalnum(e) || e == '-' || e == '_') {
      continue;
    } else {
      return Err(DefineError::INVALID_NAME);
    }
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

} // namespace ydsh