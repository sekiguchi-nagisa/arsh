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
#include "object.h"
#include "renderer.h"

namespace arsh {

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd; // does not close it

  bool lock{false};

  bool rawMode{false};

  bool continueLine{false};

  /**
   * enable language specific features (syntax highlight, auto-line continuation)
   */
  bool langExtension{false};

  bool useBracketedPaste{true};

  bool useFlowControl{false}; // disabled by default

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
  explicit LineEditorObject(ARState &state);

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

  bool defineCustomAction(ARState &state, StringRef name, StringRef type,
                          ObjPtr<Object> &&callback);

  const auto &getKeyBindings() const { return this->keyBindings; }

  bool setConfig(ARState &state, StringRef name, const Value &value);

  Value getConfigs(ARState &state) const;

private:
  int enableRawMode(int fd);

  void disableRawMode(int fd);

  void refreshLine(ARState &state, RenderingContext &ctx, bool repaint = true,
                   ObserverPtr<ArrayPager> pager = nullptr);

  ssize_t accept(ARState &state, RenderingContext &ctx);

  /**
   * entry point of actual line edit function
   * @param state
   * @param ctx
   * @return
   */
  ssize_t editLine(ARState &state, RenderingContext &ctx);

  ssize_t editInRawMode(ARState &state, RenderingContext &ctx);

  EditActionStatus completeLine(ARState &state, RenderingContext &ctx, KeyCodeReader &reader);

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

  /**
   * @param state
   * @return
   * if need refresh, return true. otherwise, return false
   */
  bool handleSignals(ARState &state);
};

} // namespace arsh

#endif // ARSH_LINE_EDITOR_H
