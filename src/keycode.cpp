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

#include "constant.h"
#include "keycode.h"
#include "misc/unicode.hpp"

namespace ydsh {

// ###########################
// ##     KeyCodeReader     ##
// ###########################

static ssize_t readCodePoint(int fd, char (&buf)[8], int &code) {
  ssize_t readSize = read(fd, &buf[0], 1);
  if (readSize <= 0) {
    return readSize;
  }
  unsigned int byteSize = UnicodeUtil::utf8ByteSize(buf[0]);
  if (byteSize < 1 || byteSize > 4) {
    return -1;
  } else if (byteSize > 1) {
    readSize = read(fd, &buf[1], byteSize - 1);
    if (readSize <= 0) {
      return readSize;
    }
  }
  return static_cast<ssize_t>(UnicodeUtil::utf8ToCodePoint(buf, std::size(buf), code));
}

#define READ_BYTE(b, bs)                                                                           \
  do {                                                                                             \
    if (read(this->fd, (b) + (bs), 1) <= 0) {                                                      \
      goto END;                                                                                    \
    } else {                                                                                       \
      seqSize++;                                                                                   \
    }                                                                                              \
  } while (false)

ssize_t KeyCodeReader::fetch() {
  constexpr const char ESC = '\x1b';
  char buf[8];
  int code;
  ssize_t readSize = readCodePoint(this->fd, buf, code);
  if (readSize <= 0) {
    return readSize;
  }
  assert(readSize > 0 && readSize < 5);
  this->keycode.assign(buf, static_cast<size_t>(readSize));
  if (isEscapeChar(code)) {
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

// ########################
// ##     KeyBindings    ##
// ########################

static bool isCaretTarget(int ch) { return (ch >= '@' && ch <= '_') || ch == '?'; }

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
#define CTRL_R_ "\x12"
#define CTRL_T_ "\x14"
#define CTRL_U_ "\x15"
#define CTRL_W_ "\x17"
#define ESC_ "\x1b"
#define BACKSPACE_ "\x7F"

KeyBindings::KeyBindings() {
  // control character
  this->values.emplace(ENTER_, EditAction::ACCEPT);
  this->values.emplace(CTRL_J_, EditAction::ACCEPT);
  this->values.emplace(CTRL_C_, EditAction::CANCEL);
  this->values.emplace(TAB_, EditAction::COMPLETE);
  this->values.emplace(CTRL_H_, EditAction::BACKWARD_DELETE_CHAR);
  this->values.emplace(BACKSPACE_, EditAction::BACKWARD_DELETE_CHAR);
  this->values.emplace(CTRL_D_, EditAction::DELETE_OR_EXIT);
  this->values.emplace(CTRL_T_, EditAction::TRANSPOSE_CHAR);
  this->values.emplace(CTRL_B_, EditAction::BACKWARD_CHAR);
  this->values.emplace(CTRL_F_, EditAction::FORWARD_CHAR);
  this->values.emplace(CTRL_P_, EditAction::UP_OR_HISTORY);
  this->values.emplace(CTRL_N_, EditAction::DOWN_OR_HISTORY);
  this->values.emplace(CTRL_R_, EditAction::SEARCH_HISTORY);
  this->values.emplace(CTRL_U_, EditAction::BACKWORD_KILL_LINE);
  this->values.emplace(CTRL_K_, EditAction::KILL_LINE);
  this->values.emplace(CTRL_A_, EditAction::BEGINNING_OF_LINE);
  this->values.emplace(CTRL_E_, EditAction::END_OF_LINE);
  this->values.emplace(CTRL_L_, EditAction::CLEAR_SCREEN);
  this->values.emplace(CTRL_W_, EditAction::BACKWARD_KILL_WORD);

  // escape sequence
  this->values.emplace(ESC_ "b", EditAction::BACKWARD_WORD);
  this->values.emplace(ESC_ "f", EditAction::FORWARD_WORD);
  this->values.emplace(ESC_ "d", EditAction::KILL_WORD);
  this->values.emplace(ESC_ ENTER_, EditAction::NEWLINE);
  this->values.emplace(ESC_ "[1~", EditAction::BEGINNING_OF_LINE);
  this->values.emplace(ESC_ "[4~", EditAction::END_OF_LINE); // for putty
  this->values.emplace(ESC_ "[3~", EditAction::DELETE_CHAR); // for putty
  this->values.emplace(ESC_ "[200~", EditAction::BRACKET_PASTE);
  this->values.emplace(ESC_ "[1;3A", EditAction::PREV_HISTORY);
  this->values.emplace(ESC_ "[1;3B", EditAction::NEXT_HISTORY);
  this->values.emplace(ESC_ "[1;3D", EditAction::BACKWARD_WORD);
  this->values.emplace(ESC_ "[1;3C", EditAction::FORWARD_WORD);
  this->values.emplace(ESC_ "[A", EditAction::UP_OR_HISTORY);
  this->values.emplace(ESC_ "[B", EditAction::DOWN_OR_HISTORY);
  this->values.emplace(ESC_ "[D", EditAction::BACKWARD_CHAR);
  this->values.emplace(ESC_ "[C", EditAction::FORWARD_CHAR);
  this->values.emplace(ESC_ "[H", EditAction::BEGINNING_OF_LINE);
  this->values.emplace(ESC_ "[F", EditAction::END_OF_LINE);
  this->values.emplace(ESC_ "OH", EditAction::BEGINNING_OF_LINE);
  this->values.emplace(ESC_ "OF", EditAction::END_OF_LINE);
  this->values.emplace(ESC_ ESC_ "[A", EditAction::PREV_HISTORY);  // for mac
  this->values.emplace(ESC_ ESC_ "[B", EditAction::NEXT_HISTORY);  // for mac
  this->values.emplace(ESC_ ESC_ "[D", EditAction::BACKWARD_WORD); // for mac
  this->values.emplace(ESC_ ESC_ "[C", EditAction::FORWARD_WORD);  // for mac
}

const EditAction *KeyBindings::findAction(const std::string &keycode) {
  auto iter = this->values.find(keycode);
  if (iter != this->values.end()) {
    return &iter->second;
  }
  return nullptr;
}

const char *toString(EditAction action) {
  const char *table[] = {
#define GEN_TABLE(E, S) S,
      EACH_EDIT_ACTION(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[static_cast<unsigned int>(action)];
}

static StrRefMap<EditAction> initActionNameMap() {
  StrRefMap<EditAction> actions = {
#define GEN_ENTRY(E, S) {S, EditAction::E},
      EACH_EDIT_ACTION(GEN_ENTRY)
#undef GEN_ENTRY
  };
  return actions;
}

KeyBindings::AddStatus KeyBindings::addBinding(StringRef caret, StringRef name) {
  static auto actionMap = initActionNameMap();

  auto key = parseCaret(caret);
  if (key.empty() || !isControlChar(key[0])) {
    return AddStatus::INVALID_START_CHAR;
  }
  if (key == BRACKET_START) {
    return AddStatus::FORBIT_BRACKET_START_CODE;
  }
  for (auto &e : caret) {
    if (!isascii(e)) {
      return AddStatus::INVALID_ASCII;
    }
  }

  if (name.empty()) {
    this->values.erase(key);
  } else {
    auto action = EditAction::ACCEPT;
    if (auto iter = actionMap.find(name); iter != actionMap.end()) {
      action = iter->second;
    } else {
      return AddStatus::UNDEF;
    }

    if (action == EditAction::BRACKET_PASTE) {
      return AddStatus::FORBTT_BRACKET_ACTION;
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

} // namespace ydsh