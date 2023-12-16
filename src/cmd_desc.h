/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef ARSH_CMD_DESC_H
#define ARSH_CMD_DESC_H

namespace arsh {

struct BuiltinCmdDesc {
  const char *name;
  const char *usage;
  const char *detail;
};

unsigned int getBuiltinCmdSize();

const BuiltinCmdDesc *getBuiltinCmdDescList();

} // namespace arsh

#endif // ARSH_CMD_DESC_H
