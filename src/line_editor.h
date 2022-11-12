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

namespace ydsh {

class LineEditorObject : public ObjectWithRtti<ObjectKind::LineEditor> {
private:
  int inFd;
  int outFd;

public:
  LineEditorObject();

  ~LineEditorObject();

  char *readline(const char *prompt); // pseudo entry point
};

} // namespace ydsh

#endif // YDSH_LINE_EDITOR_H
