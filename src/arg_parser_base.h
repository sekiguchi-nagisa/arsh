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

#ifndef ARSH_ARG_PARSER_BASE_H
#define ARSH_ARG_PARSER_BASE_H

#include <limits>

#include "constant.h"
#include "misc/buffer.hpp"
#include "misc/enum_util.hpp"
#include "misc/opt_parser.hpp"
#include "misc/resource.hpp"
#include "misc/result.hpp"

namespace arsh {

enum class ArgEntryAttr : unsigned short {
  REQUIRED = 1u << 0u,    // require option
  POSITIONAL = 1u << 1u,  // positional argument
  REMAIN = 1u << 2u,      // remain argument (last positional argument that accept string array)
  STORE_FALSE = 1u << 3u, // for flag options (no-arg option)
  STOP_OPTION = 1u << 4u, // stop option recognition (like --)
};

template <>
struct allow_enum_bitop<ArgEntryAttr> : std::true_type {};

enum class ArgEntryIndex : unsigned short {};

class ArgEntry : public OptParseOption<ArgEntryIndex> {
public:
  static_assert(std::numeric_limits<std::underlying_type_t<ArgEntryIndex>>::max() ==
                SYS_LIMIT_ARG_ENTRY_MAX);

  enum class CheckerKind : unsigned char {
    NOP,    // no check
    INT,    // parse int and check range
    CHOICE, // check choice list
  };

private:
  unsigned char fieldOffset{0}; // corresponding field offset
  ArgEntryAttr attr{};
  int8_t xorGroupId{-1};
  CheckerKind checkerKind{CheckerKind::NOP};
  std::string defaultValue; // for OptParseOp::OPT_ARG. may be null

  using Choice = FlexBuffer<char *>;

  union {
    struct {
      int64_t min; // inclusive
      int64_t max; // inclusive
    } intRange;

    Choice choice;
  };

public:
  static ArgEntry newHelp(ArgEntryIndex index);

  explicit ArgEntry(ArgEntryIndex index, unsigned char fieldOffset)
      : OptParseOption(index), fieldOffset(fieldOffset), intRange({0, 0}) {}

  ~ArgEntry();

  ArgEntry(ArgEntry &&o) noexcept // NOLINT
      : OptParseOption(std::move(static_cast<OptParseOption &>(o))), fieldOffset(o.fieldOffset),
        attr(o.attr), xorGroupId(o.xorGroupId), checkerKind(o.checkerKind),
        defaultValue(std::move(o.defaultValue)) {
    switch (this->checkerKind) {
    case CheckerKind::NOP:
      break;
    case CheckerKind::INT:
      this->intRange = o.intRange;
      break;
    case CheckerKind::CHOICE:
      this->choice = std::move(o.choice);
      break;
    }
    o.checkerKind = CheckerKind::NOP;
  }

  ArgEntry &operator=(ArgEntry &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~ArgEntry();
      new (this) ArgEntry(std::move(o));
    }
    return *this;
  }

  ArgEntryIndex getIndex() const { return this->kind; }

  unsigned char getFieldOffset() const { return this->fieldOffset; }

  bool isHelp() const { return this->getFieldOffset() == 0; }

  void setParseOp(OptParseOp parseOp) { this->op = parseOp; }

  OptParseOp getParseOp() const { return this->op; }

  void setAttr(ArgEntryAttr a) { this->attr = a; }

  ArgEntryAttr getAttr() const { return this->attr; }

  bool hasAttr(ArgEntryAttr a) const { return hasFlag(this->attr, a); }

  bool isRequired() const { return this->hasAttr(ArgEntryAttr::REQUIRED); }

  bool isPositional() const { return this->hasAttr(ArgEntryAttr::POSITIONAL); }

  bool isRemainArg() const { return this->hasAttr(ArgEntryAttr::REMAIN); }

  void setIntRange(int64_t min, int64_t max) {
    this->destroyCheckerData();
    this->checkerKind = CheckerKind::INT;
    this->intRange.min = min;
    this->intRange.max = max;
  }

  std::pair<int64_t, int64_t> getIntRange() const {
    auto [min, max] = this->intRange;
    return {min, max};
  }

  void addChoice(char *e) {
    if (this->checkerKind != CheckerKind::CHOICE) {
      this->checkerKind = CheckerKind::CHOICE;
      new (&this->choice) Choice();
    }
    this->choice.push_back(e);
  }

  const auto &getChoice() const {
    assert(this->checkerKind == CheckerKind::CHOICE);
    return this->choice;
  }

  void setXORGroupId(unsigned char id) {
    if (id <= SYS_LIMIT_XOR_ARG_GROUP_NUM) {
      this->xorGroupId = static_cast<int8_t>(id);
    }
  }

  int8_t getXORGroupId() const { return this->xorGroupId; }

  bool inXORGroup() const { return this->getXORGroupId() != -1; }

  CheckerKind getCheckerKind() const { return this->checkerKind; }

  void setShortName(char ch) { this->shortOptName = ch; }

  char getShortName() const { return this->shortOptName; }

  void setLongName(const char *name) { this->longOptName = name; }

  const std::string &getLongName() const { return this->longOptName; }

  std::string toOptName(bool shortOpt) const {
    std::string value;
    value += '-';
    if (shortOpt) {
      value += this->getShortName();
    } else {
      value += '-';
      value += this->getLongName();
    }
    return value;
  }

  void setDefaultValue(const char *v) { this->defaultValue = v; }

  const std::string &getDefaultValue() const { return this->defaultValue; }

  void setArgName(const char *name) { this->argName = name; }

  const std::string &getArgName() const { return this->argName; }

  void setDetail(const char *value) { this->detail = value; }

  const std::string &getDetail() const { return this->detail; }

  bool isOption() const { return !this->isPositional(); }

  /**
   *
   * @param arg
   * @param shortOpt
   * @param out
   * if CheckerKind::INT, set parsed value
   * otherwise, set 0
   * @param err
   * if has error, set error message
   * @return
   */
  bool checkArg(StringRef arg, bool shortOpt, int64_t &out, std::string &err) const;

private:
  void destroyCheckerData();
};

class ArgParser : public OptParser<ArgEntryIndex, ArgEntry> {
private:
  using parser = OptParser<ArgEntryIndex, ArgEntry>;

  const StringRef cmdName;
  const std::vector<ArgEntry> &entries;
  const StringRef desc;

public:
  static ArgParser create(StringRef cmdName, const std::vector<ArgEntry> &entries, StringRef desc) {
    size_t index = 0;
    for (; index < entries.size(); index++) {
      if (!entries[index].isOption()) {
        break;
      }
    }
    return {cmdName, entries, index, desc};
  }

  ArgParser(StringRef cmdName, const std::vector<ArgEntry> &entries, size_t size, StringRef desc)
      : OptParser(size, entries.data()), cmdName(cmdName), entries(entries), desc(desc) {}

  const auto &getEntries() const { return this->entries; }

  /**
   *
   * @param message
   * @param verbose
   * @return
   * if string size reacheas limit, return invalid
   */
  Optional<std::string> formatUsage(StringRef message, bool verbose) const;
};

} // namespace arsh

#endif // ARSH_ARG_PARSER_BASE_H
