/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef ARSH_LINE_EDITOR_H
#define ARSH_LINE_EDITOR_H

#include <termios.h>

#include "keycode.h"
#include "line_buffer.h"
#include "line_renderer.h"
#include "object.h"

struct linenoiseState;

namespace arsh {

class ArrayPager;

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd; // does not close it

  bool lock{false};

  bool rawMode{false};

  /**
   * enable language specific features (syntax highlight, auto-line continuation)
   */
  bool langExtension{false};

  bool continueLine{false};

  bool useBracketedPaste{true};

  bool useFlowControl{true};

  unsigned char eaw{0}; // must be 0-2. if 0, auto-detect east asian width

  ANSIEscapeSeqMap escapeSeqMap;

  termios orgTermios{};

  KeyBindings keyBindings;

  /**
   * maintains killed text
   */
  KillRing killRing;

  /**
   * must be `(String) -> String` type
   * may be null
   */
  ObjPtr<Object> promptCallback;

  /**
   * must be `(Module, String) -> Candidates` type
   * may be null
   */
  ObjPtr<Object> completionCallback;

  /**
   * must be `[String]' type
   * may be null
   */
  ObjPtr<ArrayObject> history;

  /**
   * must be `(String, [String]) -> Void' type
   * may be null
   */
  ObjPtr<Object> histSyncCallback;

  /**
   * for custom actions (custom action index as meta data)
   * must be `(String, [String]?) -> String?' type
   */
  std::vector<Value> customCallbacks;

public:
  LineEditorObject();

  ~LineEditorObject();

  ssize_t readline(ARState &state, StringRef prompt, char *buf, size_t bufLen);

  bool locked() const { return this->lock; }

  void setPromptCallback(ObjPtr<Object> callback) { this->promptCallback = std::move(callback); }

  void setCompletionCallback(ObjPtr<Object> callback) {
    this->completionCallback = std::move(callback);
  }

  void setHistory(ObjPtr<ArrayObject> hist) { this->history = std::move(hist); }

  void setHistSyncCallback(ObjPtr<Object> callback) {
    this->histSyncCallback = std::move(callback);
  }

  void setColor(StringRef colorSetting) {
    this->escapeSeqMap = ANSIEscapeSeqMap::fromString(colorSetting);
  }

  /**
   *
   * @param state
   * @param key
   * @param name
   * @return
   * if error, return false
   */
  bool addKeyBind(ARState &state, StringRef key, StringRef name);

  bool defineCustomAction(ARState &state, StringRef name, StringRef type, ObjPtr<Object> callback);

  const auto &getKeyBindings() const { return this->keyBindings; }

  bool setConfig(ARState &state, StringRef name, const Value &value);

  Value getConfigs(ARState &state) const;

private:
  int enableRawMode(int fd);

  void disableRawMode(int fd);

  void refreshLine(struct linenoiseState &l, bool repaint = true,
                   ObserverPtr<ArrayPager> pager = nullptr);

  ssize_t accept(ARState &state, struct linenoiseState &l);

  /**
   * entry point of actual line edit function
   * @param state
   * @param buf
   * @param bufSize
   * @param prompt
   * @return
   */
  ssize_t editLine(ARState &state, StringRef prompt, char *buf, size_t bufSize);

  ssize_t editInRawMode(ARState &state, struct linenoiseState &l);

  EditActionStatus completeLine(ARState &state, struct linenoiseState &ls, KeyCodeReader &reader);

  Value kickCallback(ARState &state, Value &&callback, CallArgs &&callArgs);

  ObjPtr<ArrayObject> kickCompletionCallback(ARState &state, StringRef line);

  bool kickHistSyncCallback(ARState &state, const LineBuffer &buf);

  using custom_callback_iter = std::vector<Value>::const_iterator;

  custom_callback_iter lookupCustomCallback(unsigned int index) const;

  /**
   *
   * @param state
   * @param buf
   * @param type
   * @param index
   * @return
   * if has error or does not insert, return false
   */
  bool kickCustomCallback(ARState &state, LineBuffer &buf, CustomActionType type,
                          unsigned int index);
};

} // namespace arsh

#endif // ARSH_LINE_EDITOR_H
