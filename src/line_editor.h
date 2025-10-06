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

#include "keybind.h"
#include "object.h"
#include "renderer.h"

namespace arsh {

enum class LineEditorFeature : unsigned char {
  LANG_EXTENSION = 1u << 0u, // syntax highlight, auto-line continuation
  BRACKETED_PASTE = 1u << 1u,
  FLOW_CONTROL = 1u << 2u,
  SEMANTIC_PROMPT = 1u << 3u,         // OSC133 (shell integration/semantic prompt)
  KITTY_KEYBOARD_PROTOCOL = 1u << 4u, // emit CSI = 5 u sequence
  XTERM_MODIFY_OTHER_KEYS = 1u << 5u, // emit CSI > 4 ; 1 m sequence
};

template <>
struct allow_enum_bitop<LineEditorFeature> : std::true_type {};

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd; // does not close it

  bool lock{false};

  bool rawMode{false};

  bool continueLine{false};

  unsigned char eaw{0}; // must be 0-2. if 0, auto-detect east asian width

  LineEditorFeature features{LineEditorFeature::BRACKETED_PASTE};

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
   * for history synchronization and pre-exec hook
   * must be `(String, [String]?) -> Void' type
   * may be null
   */
  ObjPtr<Object> acceptorCallback;

  /**
   * for custom actions (custom action index as meta-data)
   * must be `(String, [String]?) -> String?' type
   */
  std::vector<Value> customCallbacks;

  /**
   * for abbreviation. (pattern -> expanded)
   */
  std::unordered_map<std::string, std::string> abbrMap;

public:
  explicit LineEditorObject(ARState &state);

  ~LineEditorObject();

  /**
   * read line with line editing
   * @param state
   * @param prompt
   * @param buf
   * @param bufLen
   * @return
   * read size of input string.
   * if error, return a negative number (less than -1) and set errno
   */
  ssize_t readline(ARState &state, StringRef prompt, char *buf, size_t bufLen);

  bool locked() const { return this->lock; }

  void setPromptCallback(ObjPtr<Object> callback) { this->promptCallback = std::move(callback); }

  void setCompletionCallback(ObjPtr<Object> callback) {
    this->completionCallback = std::move(callback);
  }

  void setHistory(ObjPtr<ArrayObject> hist) { this->history = std::move(hist); }

  void setAcceptorCallback(ObjPtr<Object> callback) {
    this->acceptorCallback = std::move(callback);
  }

  void setColor(StringRef colorSetting) {
    this->escapeSeqMap = ANSIEscapeSeqMap::fromString(colorSetting);
  }

  bool addKeyBind(StringRef key, StringRef name, std::string &err) {
    return this->keyBindings.addBinding(key, name, &err);
  }

  bool defineCustomAction(ARState &state, StringRef name, StringRef type,
                          ObjPtr<Object> &&callback);

  const auto &getKeyBindings() const { return this->keyBindings; }

  bool defineAbbr(ARState &state, StringRef pattern, StringRef expansion);

  const auto &getAbbrMap() const { return this->abbrMap; }

  bool setConfig(ARState &state, StringRef name, const Value &value);

  Value getConfigs(ARState &state) const;

  /**
   * get read keycode (raw bytes) and recognized event
   * @param state
   * @return
   * (keycode, event): (String, String).
   * if throw error, return empty
   */
  Value getkey(ARState &state);

private:
  void setFeature(LineEditorFeature f, bool set) {
    if (set) {
      setFlag(this->features, f);
    } else {
      unsetFlag(this->features, f);
    }
  }

  bool hasFeature(LineEditorFeature f) const { return hasFlag(this->features, f); }

  int enableRawMode(int fd);

  void disableRawMode(int fd);

  void refreshLine(ARState &state, RenderingContext &ctx, bool repaint = true,
                   ObserverPtr<ArrayPager> pager = nullptr);

  ssize_t accept(ARState &state, RenderingContext &ctx, bool expandAbbr);

  /**
   * entry point of actual line edit function
   * @param state
   * @param ctx
   * @return
   */
  ssize_t editLine(ARState &state, RenderingContext &ctx);

  ssize_t editInRawMode(ARState &state, RenderingContext &ctx);

  EditActionStatus completeLine(ARState &state, RenderingContext &ctx, KeyCodeReader &reader,
                                bool backward);

  Value kickCallback(ARState &state, Value &&callback, CallArgs &&callArgs);

  ObjPtr<ArrayObject> kickCompletionCallback(ARState &state, StringRef line);

  bool kickAcceptorCallback(ARState &state, const LineBuffer &buf);

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
