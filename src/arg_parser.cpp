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

namespace arsh {

// ###############################
// ##     RequiredOptionSet     ##
// ###############################

RequiredOptionSet::RequiredOptionSet(const std::vector<ArgEntry> &entries) {
  const size_t size = entries.size();
  for (size_t i = 0; i < size; i++) {
    if (auto &e = entries[i]; e.isRequired() || e.isPositional()) {
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

// ############################
// ##     XORArgGroupSet     ##
// ############################

std::pair<unsigned int, bool> XORArgGroupSet::add(Entry entry) {
  bool r = false;
  const auto iter =
      std::lower_bound(this->values.begin(), this->values.end(), entry,
                       [](const Entry &x, const Entry &y) { return x.groupId < y.groupId; });
  if (iter == this->values.end() || iter->groupId != entry.groupId) {
    this->values.insert(iter, entry);
    r = true;
  }
  unsigned int index = iter - this->values.begin();
  return {index, r};
}

bool XORArgGroupSet::has(int8_t groupId) const {
  const Entry entry{
      .index = 0,
      .shortOp = false,
      .groupId = static_cast<unsigned char>(groupId),
  };
  const auto iter =
      std::lower_bound(this->values.begin(), this->values.end(), entry,
                       [](const Entry &x, const Entry &y) { return x.groupId < y.groupId; });
  return iter != this->values.end();
}

static bool verboseUsage(const ARState &st, const BaseObject &out) {
  auto &type = st.typePool.get(out.getTypeID());
  return hasFlag(cast<CLIRecordType>(type).getAttr(), CLIRecordType::Attr::VERBOSE);
}

static void raiseCLIUsage(ARState &state, const ArgParser &parser, const StringRef message,
                          bool verbose, int status) {
  if (auto ret = parser.formatUsage(message, verbose); ret.hasValue()) {
    raiseError(state, TYPE::CLIError, std::move(ret.unwrap()), status);
  } else {
    raiseStringLimit(state);
  }
}

static bool checkAndSetArg(ARState &state, const ArgParser &parser, const ArgEntry &entry,
                           StringRef arg, bool shortOpt, BaseObject &out) {
  int64_t v = 0;
  if (std::string err; entry.checkArg(arg, shortOpt, v, err)) {
    unsigned int offset = entry.getFieldOffset();
    if (entry.getCheckerKind() == ArgEntry::CheckerKind::INT) {
      out[offset] = Value::createInt(v);
    } else {
      out[offset] = Value::createStr(arg);
    }
    return true;
  } else {
    raiseCLIUsage(state, parser, err, verboseUsage(state, out), 1);
    return false;
  }
}

static bool checkXORGroup(ARState &state, const ArgParser &parser, const unsigned short entryIndex,
                          const bool shortOpt, const BaseObject &out, XORArgGroupSet &set) {
  const XORArgGroupSet::Entry xorEntry{
      .index = entryIndex,
      .shortOp = shortOpt,
      .groupId = static_cast<unsigned char>(parser.getEntries()[entryIndex].getXORGroupId()),
  };

  if (const auto ret = set.add(xorEntry); !ret.second) {
    auto &prev = set.getValues()[ret.first];
    if (prev.index == entryIndex) { // itself
      return true;
    }
    std::string err = parser.getEntries()[entryIndex].toOptName(shortOpt);
    err += " option is not allowed after ";
    err += parser.getEntries()[prev.index].toOptName(prev.shortOp);
    err += " option";
    raiseCLIUsage(state, parser, err, verboseUsage(state, out), 1);
    return false;
  }
  return true;
}

static bool checkRequireOrPositionalArgs(ARState &state, const ArgParser &parser,
                                         const RequiredOptionSet &requiredSet,
                                         const XORArgGroupSet &xorGroupSet, StrArrayIter &begin,
                                         const StrArrayIter end, BaseObject &out) {
  const bool verbose = verboseUsage(state, out);
  for (auto &i : requiredSet.getValues()) {
    auto &e = parser.getEntries()[i];
    if (e.isOption()) {
      assert(e.isRequired());
      if (e.inXORGroup() && xorGroupSet.has(e.getXORGroupId())) {
        continue;
      }
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
      raiseCLIUsage(state, parser, err, verbose, 1);
      return false;
    }

    // set positional argument
    if (!e.isPositional()) {
      continue;
    }
    if (begin != end) {
      StringRef arg = *begin;
      ++begin;
      if (e.isRemainArg() && out[e.getFieldOffset()].isInvalid()) {
        out[e.getFieldOffset()] = Value::create<ArrayObject>(state.typePool.get(TYPE::StringArray));
      }
      if (e.isRemainArg()) {
        auto &obj = typeAs<ArrayObject>(out[e.getFieldOffset()]);
        if (!obj.append(state, Value::createStr(arg))) {
          return false;
        }
        for (; begin != end; ++begin) {
          if (!obj.append(state, Value::createStr(*begin))) {
            return false;
          }
        }
      } else {
        if (!checkAndSetArg(state, parser, e, arg, true, out)) {
          --begin;
          return false;
        }
      }
    } else if (e.isRequired()) {
      std::string err = "require `";
      err += e.getArgName();
      err += "' argument";
      raiseCLIUsage(state, parser, err, verbose, 1);
      return false;
    }
  }
  return true;
}

static ObjPtr<BaseObject> createSubCmd(ARState &state, const StringRef base,
                                       const CLIRecordType &type, const StringRef subCmd) {
  auto *modType = state.typePool.getModTypeById(type.resolveBelongedModId());
  assert(modType);
  auto *handle = modType->lookupVisibleSymbolAtModule(state.typePool,
                                                      toMethodFullName(type.typeId(), OP_INIT));
  assert(handle);
  assert(handle->isMethodHandle());
  auto instance = VM::callConstructor(state, cast<MethodHandle>(*handle), makeArgs());
  if (state.hasError()) {
    return nullptr;
  }

  auto name = Value::createStr(base);
  if (!name.appendAsStr(state, " ") || !name.appendAsStr(state, subCmd)) {
    return nullptr;
  }
  auto obj = toObjPtr<BaseObject>(instance);
  (*obj)[0] = std::move(name); // overwrite cmd name
  return obj;
}

static BaseObject *parseCommandLineImpl(ARState &state, StrArrayIter &iter, const StrArrayIter end,
                                        BaseObject &out) {
  auto &type = cast<CLIRecordType>(state.typePool.get(out.getTypeID()));
  auto instance = createArgParser(out[0].asStrRef(), type);

  RequiredOptionSet requiredSet(instance.getEntries());
  XORArgGroupSet xorGroupSet;
  ArgParser::Result ret;
  bool help = false;
  bool stop = false;

  // parse and set options
  while ((ret = instance(iter, end))) {
    const auto entryIndex = toUnderlying(ret.getOpt());
    requiredSet.del(entryIndex);
    auto &entry = instance.getEntries()[entryIndex];
    if (entry.isHelp()) {
      help = true;
      continue;
    }
    if (entry.inXORGroup()) {
      if (!checkXORGroup(state, instance, entryIndex, ret.isShort(), out, xorGroupSet)) {
        return nullptr;
      }
    }
    switch (entry.getParseOp()) {
    case OptParseOp::NO_ARG: // set flag
      out[entry.getFieldOffset()] = Value::createBool(!entry.hasAttr(ArgEntryAttr::STORE_FALSE));
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
        return nullptr;
      }
      break;
    }
    if (entry.hasAttr(ArgEntryAttr::STOP_OPTION)) {
      stop = true;
      ret = ArgParser::Result(); // end
      break;
    }
  }
  if (ret.isError()) {
    auto v = ret.formatError();
    raiseCLIUsage(state, instance, v, verboseUsage(state, out), 2);
    return nullptr;
  }
  if (help) {
    raiseCLIUsage(state, instance, "", true, 0);
    return nullptr;
  }
  assert(ret.isEnd());
  if (!checkRequireOrPositionalArgs(state, instance, requiredSet, xorGroupSet, iter, end, out)) {
    return nullptr;
  }
  // try parse sub-command
  if (!stop && iter != end && hasFlag(type.getAttr(), CLIRecordType::Attr::HAS_SUBCMD)) {
    const StringRef arg = *iter;
    if (auto [subCmdType, fieldOffset] = type.findSubCmdInfo(state.typePool, arg); subCmdType) {
      ++iter;
      const auto obj = createSubCmd(state, instance.getCmdName(), *subCmdType, arg);
      if (obj) {
        out[fieldOffset] = obj;
      }
      return obj.get();
    }
    std::string err = "unknown command: ";
    appendAsPrintable(*iter, SYS_LIMIT_STRING_MAX, err);
    raiseCLIUsage(state, instance, err, verboseUsage(state, out), 2);
    return nullptr;
  }
  return &out;
}

CLIParseResult parseCommandLine(ARState &state, const ArrayObject &args, BaseObject &out) {
  auto iter = StrArrayIter(args.begin());
  const auto begin = iter;
  const auto end = StrArrayIter(args.end());
  BaseObject *obj = &out;
  bool status;
  while (true) {
    auto *ptr = parseCommandLineImpl(state, iter, end, *obj);
    if (!ptr) {
      status = false;
      break;
    }
    if (ptr == obj) {
      status = true;
      break;
    }
    obj = ptr;
  }
  return {
      .index = static_cast<unsigned int>(iter - begin),
      .status = status,
  };
}

} // namespace arsh