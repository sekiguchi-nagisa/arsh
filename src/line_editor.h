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

#include "highlighter.h"
#include "object.h"

struct linenoiseState;

namespace ydsh {

#define EACH_EDIT_ACTION(OP)                                                                       \
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
  OP(SEARCH_HISTORY, "search-history")             /* CTRL-R */                                    \
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
  OP(BRACKET_PASTE, "bracket-paste")               /* ESC [200~ */

#define EACH_EDIT_HIST_OP(OP)                                                                      \
  OP(INIT, "init")                                                                                 \
  OP(DEINIT, "deinit") /* take extra arg (final buffer content) */                                 \
  OP(PREV, "prev")     /* take extra arg (current buffer content) */                               \
  OP(NEXT, "next")     /* take extra arg (current buffer content) */                               \
  OP(SEARCH, "search") /* take extra arg (current buffer content) */

enum class EditAction : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_EDIT_ACTION(GEN_ENUM)
#undef GEN_ENUM
};

class KeyCodeReader;

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd;

  bool lock{false};

  bool rawMode{false};

  bool highlight{false};

  bool continueLine{false};

  ANSIEscapeSeqMap escapeSeqMap;

  std::string highlightCache;

  termios orgTermios{};

  std::unordered_map<std::string, EditAction> actionMap;

  /**
   * must be `(String) -> String` type
   * may be null
   */
  ObjPtr<DSObject> promptCallback;

  /**
   * must be `(Module, String) -> [String]` type
   * may be null
   */
  ObjPtr<DSObject> completionCallback;

  /**
   * must be `(String, String) -> String!` type
   * may be null
   */
  ObjPtr<DSObject> historyCallback;

  enum class HistOp {
#define GEN_ENUM(E, S) E,
    EACH_EDIT_HIST_OP(GEN_ENUM)
#undef GEN_ENUM
  };

public:
  LineEditorObject();

  ~LineEditorObject();

  char *readline(DSState &state, StringRef promptRef); // pseudo entry point

  bool locked() const { return this->lock; }

  void setPromptCallback(ObjPtr<DSObject> callback) { this->promptCallback = std::move(callback); }

  void setCompletionCallback(ObjPtr<DSObject> callback) {
    this->completionCallback = std::move(callback);
  }

  void setHistoryCallback(ObjPtr<DSObject> callback) {
    this->historyCallback = std::move(callback);
  }

  void setColor(StringRef colorSetting) {
    this->escapeSeqMap = ANSIEscapeSeqMap::fromString(colorSetting);
  }

  void enableHighlight() { this->highlight = true; }

private:
  void resetKeybind();

  int enableRawMode(int fd);

  void disableRawMode(int fd);

  void refreshLine(struct linenoiseState &l, bool doHighlight = true);

  /**
   * entry point of actual line edit function
   * @param state
   * @param buf
   * @param buflen
   * @param prompt
   * @return
   */
  int editLine(DSState &state, char *buf, size_t buflen, const char *prompt);

  int editInRawMode(DSState &state, struct linenoiseState &l);

  enum class CompStatus {
    OK,
    ERROR,
    CANCEL,
  };

  CompStatus completeLine(DSState &state, struct linenoiseState &ls, KeyCodeReader &reader);

  size_t insertEstimatedSuffix(struct linenoiseState &ls, const ArrayObject &candidates);

  DSValue kickCallback(DSState &state, DSValue &&callback, CallArgs &&callArgs);

  ObjPtr<ArrayObject> kickCompletionCallback(DSState &state, StringRef line);

  /**
   *
   * @param state
   * @param op
   * @param l
   * @param multiline
   * @return
   * if update buffer content, return true
   */
  bool kickHistoryCallback(DSState &state, HistOp op, struct linenoiseState *l,
                           bool multiline = false);

  /**
   * rotate history with whole buffer content or multi-line aware cursor up/down
   * @param state
   * @param op
   * @param l
   * @param continueRotate
   * @return
   * if update buffer content, return true
   */
  bool rotateHistoryOrUpDown(DSState &state, HistOp op, struct linenoiseState &l,
                             bool continueRotate);
};

} // namespace ydsh

#endif // YDSH_LINE_EDITOR_H
