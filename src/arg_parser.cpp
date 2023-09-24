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
  const size_t size = entries.size();
  for (size_t i = 0; i < size; i++) {
    auto &e = entries[i];
    if (e.isRequire() || e.isPositional()) {
      assert(i <= SYS_LIMIT_ARG_ENTRY_MAX);
      auto v = static_cast<unsigned short>(i);
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

static bool verboseUsage(const DSState &st, const BaseObject &out) {
  auto &type = st.typePool.get(out.getTypeID());
  return hasFlag(cast<CLIRecordType>(type).getAttr(), CLIRecordType::Attr::VERBOSE);
}

static bool checkAndSetArg(DSState &state, const ArgParser &parser, const ArgEntry &entry,
                           StringRef arg, bool shortOpt, BaseObject &out) {
  std::string err;
  int64_t v = 0;
  if (entry.checkArg(arg, shortOpt, v, err)) {
    unsigned int offset = entry.getFieldOffset();
    if (entry.getCheckerKind() == ArgEntry::CheckerKind::INT) {
      out[offset] = DSValue::createInt(v);
    } else {
      out[offset] = DSValue::createStr(arg);
    }
    return true;
  } else {
    err = parser.formatUsage(err, verboseUsage(state, out));
    raiseError(state, TYPE::CLIError, std::move(err), 1);
    return false;
  }
}

static bool checkRequireOrPositionalArgs(DSState &state, const ArgParser &parser,
                                         const RequiredOptionSet &requiredSet, StrArrayIter &begin,
                                         const StrArrayIter end, BaseObject &out) {
  const bool verbose = verboseUsage(state, out);
  for (auto &i : requiredSet.getValues()) {
    auto &e = parser.getEntries()[i];
    if (!e.isPositional()) {
      assert(e.isRequire());
      std::string err = "require ";
      if (char s = e.getShortName(); s != 0) {
        err += '-';
        err += s;
      }
      if (const auto &l = e.getLongName(); !l.empty()) {
        if (e.getShortName()) {
          err += " or ";
        }
        err += "--";
        err += l;
      }
      err += " option";
      err = parser.formatUsage(err, verbose);
      raiseError(state, TYPE::CLIError, std::move(err), 1);
      return false;
    }

    // set positional argument
    if (begin != end) {
      StringRef arg = *begin;
      ++begin;
      if (e.isRemainArg() && out[e.getFieldOffset()].isInvalid()) {
        out[e.getFieldOffset()] =
            DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
      }
      if (e.isRemainArg()) {
        auto &obj = typeAs<ArrayObject>(out[e.getFieldOffset()]);
        if (!obj.append(state, DSValue::createStr(arg))) {
          return false;
        }
        for (; begin != end; ++begin) {
          if (!obj.append(state, DSValue::createStr(*begin))) {
            return false;
          }
        }
      } else {
        if (!checkAndSetArg(state, parser, e, arg, true, out)) {
          --begin;
          return false;
        }
      }
    } else if (e.isRequire()) {
      std::string err = "require `";
      err += e.getArgName();
      err += "' argument";
      err = parser.formatUsage(err, verbose);
      raiseError(state, TYPE::CLIError, std::move(err), 1);
      return false;
    }
  }
  return true;
}

CLIParseResult parseCommandLine(DSState &state, const ArrayObject &args, BaseObject &out) {
  auto &type = state.typePool.get(out.getTypeID());
  assert(isa<CLIRecordType>(type));
  auto instance = ArgParser::create(out[0].asStrRef(), cast<CLIRecordType>(type).getEntries());

  const auto begin = StrArrayIter(args.getValues().begin());
  auto iter = begin;
  const auto end = StrArrayIter(args.getValues().end());
  RequiredOptionSet requiredSet(instance.getEntries());
  ArgParser::Result ret;
  bool help = false;
  bool status = false;

  // parse and set options
  while ((ret = instance(iter, end))) {
    const auto entryIndex = toUnderlying(ret.getOpt());
    requiredSet.del(entryIndex);
    auto &entry = instance.getEntries()[entryIndex];
    if (entry.isHelp()) {
      help = true;
      continue;
    }
    switch (entry.getParseOp()) {
    case OptParseOp::NO_ARG: // set flag
      out[entry.getFieldOffset()] = DSValue::createBool(!entry.hasAttr(ArgEntryAttr::STORE_FALSE));
      break;
    case OptParseOp::HAS_ARG:
    case OptParseOp::OPT_ARG:
      StringRef arg = "";
      if (ret.hasArg()) {
        arg = ret.getValue();
      } else if (const auto &v = entry.getDefaultValue(); !v.empty()) {
        arg = v;
      }
      if (!checkAndSetArg(state, instance, entry, arg, ret.isShort(), out)) {
        --iter;
        goto END;
      }
      break;
    }
    if (entry.hasAttr(ArgEntryAttr::STOP_OPTION)) {
      ret = ArgParser::Result(); // end
      break;
    }
  }
  if (ret.isError()) {
    auto v = ret.formatError();
    v = instance.formatUsage(v, verboseUsage(state, out));
    raiseError(state, TYPE::CLIError, std::move(v), 2);
    goto END;
  }
  if (help) {
    auto v = instance.formatUsage("", true);
    raiseError(state, TYPE::CLIError, std::move(v), 0);
    goto END;
  }
  assert(ret.isEnd());
  status = checkRequireOrPositionalArgs(state, instance, requiredSet, iter, end, out);

END:
  return {
      .index = static_cast<unsigned int>(iter - begin),
      .status = status,
  };
}

} // namespace ydsh