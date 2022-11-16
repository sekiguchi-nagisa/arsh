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

#include "object.h"

struct linenoiseState;

namespace ydsh {

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd;

  bool lock{false};

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

public:
  LineEditorObject();

  ~LineEditorObject();

  char *readline(DSState &state, StringRef promptRef); // pseudo entry point

  bool locked() const { return this->lock; }

  void setPromptCallback(ObjPtr<DSObject> callback) { this->promptCallback = std::move(callback); }

  void setCompletionCallback(ObjPtr<DSObject> callback) {
    this->completionCallback = std::move(callback);
  }

private:
  /**
   * actual line edit function
   * @param buf
   * @param buflen
   * @param prompt
   * @return
   */
  int editInRawMode(DSState &state, char *buf, size_t buflen, const char *prompt);

  int completeLine(DSState &state, struct linenoiseState *ls, char *cbuf, int clen, int *code);

  DSValue kickCallback(DSState &state, DSValue &&callback, CallArgs &&callArgs);

  ObjPtr<ArrayObject> kickCompletionCallback(DSState &state, StringRef line);
};

} // namespace ydsh

#endif // YDSH_LINE_EDITOR_H
