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

#ifndef ARSH_ARG_PARSER_H
#define ARSH_ARG_PARSER_H

#include "arg_parser_base.h"
#include "object.h"

namespace arsh {

inline ArgParser createArgParser(StringRef cmdName, const CLIRecordType &type) {
  return ArgParser::create(cmdName, type.getEntries(), type.getDesc());
}

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

class XORArgGroupSet {
public:
  struct Entry {
    unsigned short index;
    bool shortOp;
    unsigned char groupId;
  };

private:
  FlexBuffer<Entry> values;

public:
  const auto &getValues() const { return this->values; }

  std::pair<unsigned int, bool> add(Entry entry);

  bool has(int8_t groupId) const;
};

struct CLIParseResult {
  unsigned int index;
  bool status;

  explicit operator bool() const { return this->status; }
};

/**
 * may recursively call interpreter
 * @param state
 * @param args
 * @param out
 * must be CLIRecordType
 * @return
 */
CLIParseResult parseCommandLine(ARState &state, ObjPtr<ArrayObject> args, ObjPtr<BaseObject> out);

inline void showCommandLineUsage(const ErrorObject &obj) {
  FILE *fp = obj.getStatus() == 0 ? stdout : stderr;
  auto ref = obj.getMessage().asStrRef();
  fwrite(ref.data(), sizeof(char), ref.size(), fp);
  fputc('\n', fp);
  fflush(fp);
}

} // namespace arsh

#endif // ARSH_ARG_PARSER_H
