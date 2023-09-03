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
    while (!this->choice.empty()) {
      free(this->choice.back());
      this->choice.pop_back();
    }
    this->choice.~Choice();
    this->checkerKind = CheckerKind::NOP;
  }
}

bool ArgEntry::checkArg(StringRef arg, bool shortOpt, int64_t &out, std::string &err) const {
  out = 0;
  std::string optName;
  if (this->checkerKind != CheckerKind::NOP && this->isOption()) {
    if (shortOpt) {
      optName += '-';
      optName += this->getShortName();
    } else {
      optName += "--";
      optName += this->getLongName();
    }
  }
  switch (this->checkerKind) {
  case CheckerKind::NOP:
    break;
  case CheckerKind::INT: {
    assert(this->intRange.min <= this->intRange.max);
    auto ret = convertToDecimal<int64_t>(arg.begin(), arg.end());
    if (!ret) {
      err += "invalid argument: `";
      err += arg;
      err += '\'';
      if (!optName.empty()) {
        err += " for ";
        err += optName;
        err += " option";
      }
      err += ", must be decimal integer";
      return false;
    }
    if (ret.value >= this->intRange.min && ret.value <= this->intRange.max) {
      out = ret.value;
      return true;
    } else {
      err += "invalid argument: `";
      err += arg;
      err += '\'';
      if (!optName.empty()) {
        err += " for ";
        err += optName;
        err += " option";
      }
      err += ", must be [";
      err += std::to_string(this->intRange.min);
      err += ", ";
      err += std::to_string(this->intRange.max);
      err += "]";
      return false;
    }
  }
  case CheckerKind::CHOICE: {
    for (auto &e : this->choice) {
      if (arg == e) {
        return true;
      }
    }
    err += "invalid argument: `";
    err += arg;
    err += '\'';
    if (!optName.empty()) {
      err += " for ";
      err += optName;
      err += " option";
    }
    err += ", must be {";
    unsigned int size = this->choice.size();
    for (unsigned int i = 0; i < size; i++) {
      if (i > 0) {
        err += ", ";
      }
      err += this->choice[i];
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

std::string ArgParser::formatUsage(StringRef message, bool verbose) const {
  std::string out;
  if (!message.empty()) {
    out += message;
    out += '\n';
  }

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
  out += this->cmdName;
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
        if (e.isRemainArg()) {
          out += "...";
        }
        if (!e.isRequire()) {
          out += ']';
        }
      }
    }
  }

  if (verbose) {
    out += "\n\n";
    this->formatOptions(out);
  }
  return out;
}

} // namespace ydsh