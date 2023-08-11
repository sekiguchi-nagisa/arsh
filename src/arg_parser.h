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

class ArgParserObject : public ObjectWithRtti<ObjectKind::ArgParser> {
private:
  DSValue cmdName;
  ArgParser instance;

public:
  ArgParserObject(const ArgParserType &type, DSValue cmdName)
      : ObjectWithRtti(type), cmdName(std::move(cmdName)),
        instance(ArgParser::create(type.getElementType().getEntries())) {}

  const DSValue &getCmdName() const { return this->cmdName; }

  void formatUsage(bool printOptions, std::string &out) const {
    this->instance.formatUsage(this->cmdName.asStrRef(), printOptions, out);
  }

  /**
   * parse and instantiate object (call constructor)
   * @param state
   * @param args
   * @param out
   * @return
   * if has error, return false
   */
  bool parseAll(DSState &state, const ArrayObject &args, BaseObject &out);

private:
  /**
   * @param entry
   * @param arg
   * @param out
   * @return
   * if has error, return false
   */
  bool checkAndSetArg(DSState &state, const ArgEntry &entry, StringRef arg, BaseObject &out) const;

  bool checkRequireOrPositionalArgs(DSState &state, const RequiredOptionSet &requiredSet,
                                    StrArrayIter &begin, StrArrayIter end, BaseObject &out);
};

} // namespace ydsh

#endif // YDSH_ARG_PARSER_H
