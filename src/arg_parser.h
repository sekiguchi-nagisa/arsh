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

#ifndef YDSH_ARG_PARSER_H
#define YDSH_ARG_PARSER_H

#include "arg_parser_base.h"
#include "object.h"

namespace ydsh {

class RequiredOptionSet {
private:
  /**
   * maintains require option index and positional argument index
   */
  FlexBuffer<unsigned short> values; // must be sorted

public:
  explicit RequiredOptionSet(const std::vector<ArgEntry> &entries);

  const auto &getValues() const { return this->values; }

  void del(unsigned char n);
};

/**
 *
 * @param state
 * @param args
 * @param out
 * must be CLIRecordType
 * @return
 */
bool parseArgs(DSState &state, const ArrayObject &args, BaseObject &out);

} // namespace ydsh

#endif // YDSH_ARG_PARSER_H
