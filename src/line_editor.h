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

#ifndef YDSH_LINE_EDITOR_H
#define YDSH_LINE_EDITOR_H

#include <termios.h>

#include "keycode.h"
#include "line_renderer.h"
#include "rotate.h"

struct linenoiseState;

namespace ydsh {

class LineBuffer;

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd; // does not close it

  bool lock{false};

  bool rawMode{false};

  bool highlight{false};

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
   * must be `(Module, String) -> [String]` type
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
   * for custom actions
   * must be `(String, [String]!) -> String!' type
   */
  std::vector<ObjPtr<Object>> customCallbacks;

public:
  LineEditorObject();

  ~LineEditorObject();

  ssize_t readline(DSState &state, StringRef prompt, char *buf, size_t bufLen);

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

  void enableHighlight() { this->highlight = true; }

  /**
   *
   * @param state
   * @param key
   * @param name
   * @return
   * if error, return false
   */
  bool addKeyBind(DSState &state, StringRef key, StringRef name);

  bool defineCustomAction(DSState &state, StringRef name, StringRef type, ObjPtr<Object> callback);

  const auto &getKeyBindings() const { return this->keyBindings; }

  bool setConfig(DSState &state, StringRef name, const DSValue &value);

  DSValue getConfigs(DSState &state) const;

  enum class CompStatus {
    OK,
    ERROR,    // interval error during completion
    CANCEL,   // cancel completion
    CONTINUE, // continue completion candidates paging
  };

private:
  int enableRawMode(int fd);

  void disableRawMode(int fd);

  void refreshLine(struct linenoiseState &l, bool repaint = true,
                   ObserverPtr<ArrayPager> pager = nullptr);

  ssize_t accept(DSState &state, struct linenoiseState &l);

  /**
   * entry point of actual line edit function
   * @param state
   * @param buf
   * @param bufSize
   * @param prompt
   * @return
   */
  ssize_t editLine(DSState &state, StringRef prompt, char *buf, size_t bufSize);

  ssize_t editInRawMode(DSState &state, struct linenoiseState &l);

  CompStatus completeLine(DSState &state, struct linenoiseState &ls, KeyCodeReader &reader);

  DSValue kickCallback(DSState &state, DSValue &&callback, CallArgs &&callArgs);

  ObjPtr<ArrayObject> kickCompletionCallback(DSState &state, StringRef line);

  bool kickHistSyncCallback(DSState &state, const LineBuffer &buf);

  /**
   *
   * @param state
   * @param buf
   * @param type
   * @param index
   * @return
   * if has error or does not insert, return false
   */
  bool kickCustomCallback(DSState &state, LineBuffer &buf, CustomActionType type,
                          unsigned int index);
};

} // namespace ydsh

#endif // YDSH_LINE_EDITOR_H
