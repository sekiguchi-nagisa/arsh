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

#include "arg_parser.h"
#include "vm.h"

namespace ydsh {

// ###############################
// ##     RequiredOptionSet     ##
// ###############################

RequiredOptionSet::RequiredOptionSet(const std::vector<ArgEntry> &entries) {
  for (auto &e : entries) {
    if (e.isRequire() || e.isPositional()) {
      assert(e.getIndexAsInt() <= UINT8_MAX);
      auto v = static_cast<unsigned char>(e.getIndexAsInt());
      assert(this->values.empty() || this->values.back() < v);
      this->values.push_back(v);
    }
  }
}

void RequiredOptionSet::del(unsigned char n) {
  auto iter = std::lower_bound(this->values.begin(), this->values.end(), n);
  if (iter != this->values.end() && *iter == n) {
    this->values.erase(iter);
  }
}

// #############################
// ##     ArgParserObject     ##
// #############################

bool ArgParserObject::parseAll(DSState &state, const ArrayObject &args, BaseObject &out) {
  this->instance.reset();
  auto begin = StrArrayIter(args.getValues().begin());
  auto end = StrArrayIter(args.getValues().end());
  RequiredOptionSet requiredSet(this->instance.getEntries());
  ArgParser::Result ret;
  bool help = false;

  // parse and set options
  while ((ret = this->instance(begin, end))) {
    if (ret.getOpt() == ArgEntry::HELP) {
      help = true;
      continue;
    }
    const unsigned int index = toUnderlying(ret.getOpt());
    assert(index <= UINT8_MAX);
    requiredSet.del(static_cast<unsigned char>(index));
    auto &entry = this->instance.getEntries()[index];
    assert(entry.getIndexAsInt() == index);
    switch (entry.getParseOp()) {
    case OptParseOp::NO_ARG:
      out[index] = DSValue::createBool(true); // set flag
      continue;
    case OptParseOp::HAS_ARG:
    case OptParseOp::OPT_ARG:
      StringRef arg = "";
      if (ret.hasArg()) {
        arg = ret.getValue();
      } else if (const char *v = entry.getDefaultValue()) {
        arg = v;
      }
      if (!this->checkAndSetArg(state, entry, arg, out)) {
        return false;
      }
      continue;
    }
  }
  if (ret.isError()) {
    auto v = ret.formatError();
    v += "\n";
    this->formatUsage(false, v);
    raiseError(state, TYPE::ArgParseError, std::move(v), 2);
    return false;
  }
  if (help) {
    std::string v;
    this->formatUsage(true, v);
    raiseError(state, TYPE::ArgParseError, std::move(v), 0);
    return false;
  }
  assert(ret.isEnd());
  return this->checkRequireOrPositionalArgs(state, requiredSet, begin, end, out);
}

bool ArgParserObject::checkAndSetArg(DSState &state, const ArgEntry &entry, StringRef arg,
                                     BaseObject &out) {
  std::string err;
  int64_t v = 0;
  if (entry.checkArg(arg, v, err)) {
    unsigned int index = entry.getIndexAsInt();
    if (entry.getCheckerKind() == ArgEntry::CheckerKind::INT) {
      out[index] = DSValue::createInt(v);
    } else {
      out[index] = DSValue::createStr(arg);
    }
    return true;
  } else {
    err += "\n";
    this->formatUsage(false, err);
    raiseError(state, TYPE::ArgParseError, std::move(err), 1);
    return false;
  }
}

bool ArgParserObject::checkRequireOrPositionalArgs(DSState &state,
                                                   const RequiredOptionSet &requiredSet,
                                                   StrArrayIter &begin, StrArrayIter end,
                                                   BaseObject &out) {
  for (auto &i : requiredSet.getValues()) {
    auto &e = this->instance.getEntries()[i];
    if (!e.isPositional()) {
      assert(e.isRequire());
      std::string err = "require ";
      if (char s = e.getShortName(); s != 0) {
        err += '-';
        err += s;
      }
      if (const char *l = e.getLongName(); l != nullptr) {
        if (e.getShortName()) {
          err += " or ";
        }
        err += "--";
        err += l;
      }
      err += " option";
      raiseError(state, TYPE::ArgParseError, std::move(err), 1);
      return false;
    }

    // set positional argument
    if (begin != end) {
      StringRef arg = *begin;
      ++begin;
      if (e.isRemainArg() && out[e.getIndexAsInt()].isInvalid()) {
        out[e.getIndexAsInt()] =
            DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
      }
      if (e.isRemainArg()) {
        auto &obj = typeAs<ArrayObject>(out[e.getIndexAsInt()]);
        if (!obj.append(state, DSValue::createStr(arg))) {
          return false;
        }
        for (; begin != end; ++begin) {
          if (!obj.append(state, DSValue::createStr(*begin))) {
            return false;
          }
        }
      } else {
        if (!this->checkAndSetArg(state, e, arg, out)) {
          return false;
        }
      }
    } else if (e.isRequire()) {
      std::string err = "require `";
      err += e.getArgName();
      err += "' argument";
      raiseError(state, TYPE::ArgParseError, std::move(err), 1);
      return false;
    }
  }
  return true;
}

} // namespace ydsh