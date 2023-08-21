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

#include <algorithm>

#include "arg_parser_base.h"
#include "misc/num_util.hpp"

namespace ydsh {

// ######################
// ##     ArgEntry     ##
// ######################

ArgEntry ArgEntry::newHelp(ArgEntryIndex index) {
  ArgEntry entry(index, 0);
  entry.setShortName('h');
  entry.setLongName("help");
  entry.setDetail("show this help message");
  return entry;
}

ArgEntry::~ArgEntry() { this->destroyCheckerData(); }

void ArgEntry::destroyCheckerData() {
  if (this->checkerKind == CheckerKind::CHOICE) {
    for (size_t i = 0; i < this->choice.len; i++) {
      free(this->choice.list[i]);
    }
    free(this->choice.list);
    this->checkerKind = CheckerKind::NOP;
    this->choice.len = 0;
    this->choice.list = nullptr;
  }
}

bool ArgEntry::checkArg(StringRef arg, int64_t &out, std::string &err) const {
  out = 0;
  switch (this->checkerKind) {
  case CheckerKind::NOP:
    break;
  case CheckerKind::INT: {
    assert(this->intRange.min <= this->intRange.max);
    auto ret = convertToDecimal<int64_t>(arg.begin(), arg.end());
    if (!ret) {
      err += "invalid argument: `";
      err += arg;
      err += "', must be decimal integer";
      return false;
    }
    if (ret.value >= this->intRange.min && ret.value <= this->intRange.max) {
      out = ret.value;
      return true;
    } else {
      err += "invalid argument: `";
      err += arg;
      err += "', must be [";
      err += std::to_string(this->intRange.min);
      err += ", ";
      err += std::to_string(this->intRange.max);
      err += "]";
      return false;
    }
  }
  case CheckerKind::CHOICE: {
    for (size_t i = 0; i < this->choice.len; i++) {
      if (arg == this->choice.list[i]) {
        return true;
      }
    }
    err += "invalid argument: `";
    err += arg;
    err += "', must be {";
    for (size_t i = 0; i < this->choice.len; i++) {
      if (i > 0) {
        err += ", ";
      }
      err += this->choice.list[i];
    }
    err += "}";
    return false;
  }
  }
  return true;
}

// #######################
// ##     ArgParser     ##
// #######################

void ArgParser::formatUsage(StringRef cmdName, bool printOptions, std::string &out) const {
  unsigned int optCount = 0;
  unsigned int argCount = 0;
  for (auto &e : this->entries) {
    if (e.isHelp()) {
      continue;
    } else if (e.isOption()) {
      optCount++;
    } else {
      argCount++;
    }
  }

  out += "Usage: ";
  out += cmdName;
  if (optCount) {
    out += " [OPTIONS]";
  }

  if (argCount) {
    for (auto &e : this->entries) {
      if (e.isPositional()) {
        out += ' ';
        if (!e.isRequire()) {
          out += '[';
        }
        assert(!e.getArgName().empty());
        out += e.getArgName();
        if (!e.isRequire()) {
          if (e.isRemainArg()) {
            out += " ...";
          }
          out += ']';
        }
      }
    }
  }

  if (printOptions) {
    out += "\n\n";
    this->formatOptions(out);
  }
}

} // namespace ydsh