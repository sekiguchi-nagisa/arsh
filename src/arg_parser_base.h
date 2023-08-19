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

#ifndef YDSH_ARG_PARSER_BASE_H
#define YDSH_ARG_PARSER_BASE_H

#include <limits>

#include "constant.h"
#include "misc/buffer.hpp"
#include "misc/enum_util.hpp"
#include "misc/opt_parser.hpp"
#include "misc/resource.hpp"

namespace ydsh {

enum class ArgEntryAttr : unsigned short {
  REQUIRE = 1u << 0u,     // require option
  POSITIONAL = 1u << 1u,  // positional argument
  REMAIN = 1u << 2u,      // remain argument (last positional argument that accept string array)
  STORE_FALSE = 1u << 3u, // for flag options (no-arg option)
};

template <>
struct allow_enum_bitop<ArgEntryAttr> : std::true_type {};

enum class ArgEntryIndex : unsigned short {};

// 56 bytes

class ArgEntry : public OptParser<ArgEntryIndex>::Option {
public:
  static constexpr auto HELP = static_cast<ArgEntryIndex>(SYS_LIMIT_ARG_ENTRY_MAX);

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
  CheckerKind checkerKind{CheckerKind::NOP};
  std::string defaultValue; // for OptParseOp::OPT_ARG. may be null

  union {
    struct {
      int64_t min; // inclusive
      int64_t max; // inclusive
    } intRange;

    struct {
      char **list;
      size_t len;
    } choice;
  };

public:
  static ArgEntry newHelp();

  explicit ArgEntry(ArgEntryIndex index, unsigned char fieldOffset)
      : OptParser<ArgEntryIndex>::Option(index), fieldOffset(fieldOffset), intRange({0, 0}) {}

  ~ArgEntry();

  ArgEntry(ArgEntry &&o) noexcept // NOLINT
      : OptParser<ArgEntryIndex>::Option(std::move(o)), fieldOffset(o.fieldOffset), attr(o.attr),
        checkerKind(o.checkerKind), defaultValue(std::move(o.defaultValue)) {
    switch (this->checkerKind) {
    case CheckerKind::NOP:
      break;
    case CheckerKind::INT:
      this->intRange = o.intRange;
      break;
    case CheckerKind::CHOICE:
      this->choice = o.choice;
      o.choice = {nullptr, 0};
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

  void setParseOp(OptParseOp parseOp) { this->op = parseOp; }

  OptParseOp getParseOp() const { return this->op; }

  void setAttr(ArgEntryAttr a) { this->attr = a; }

  ArgEntryAttr getAttr() const { return this->attr; }

  bool hasAttr(ArgEntryAttr a) const { return hasFlag(this->attr, a); }

  bool isRequire() const { return this->hasAttr(ArgEntryAttr::REQUIRE); }

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

  void setChoice(FlexBuffer<char *> &&buf) {
    this->destroyCheckerData();
    if (!buf.empty()) {
      this->checkerKind = CheckerKind::CHOICE;
      this->choice.len = buf.size();
      this->choice.list = std::move(buf).take();
    }
  }

  std::pair<const char *const *, const char *const *> getChoice() const {
    auto begin = static_cast<const char *const *>(this->choice.list);
    auto end = static_cast<const char *const *>(this->choice.list + this->choice.len);
    return {begin, end};
  }

  CheckerKind getCheckerKind() const { return this->checkerKind; }

  void setShortName(char ch) { this->shortOptName = ch; }

  char getShortName() const { return this->shortOptName; }

  void setLongName(const char *name) { this->longOptName = name; }

  const std::string &getLongName() const { return this->longOptName; }

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
   * @param out
   * if CheckerKind::INT, set parsed value
   * otherwise, set 0
   * @param err
   * if has error, set error message
   * @return
   */
  bool checkArg(StringRef arg, int64_t &out, std::string &err) const;

private:
  void destroyCheckerData();
};

class ArgParser : public OptParser<ArgEntryIndex> {
private:
  using parser = OptParser<ArgEntryIndex>;

  const std::vector<ArgEntry> &entries;
  std::unique_ptr<const parser::Option[]> options;

public:
  static ArgParser create(const std::vector<ArgEntry> &entries);

  ArgParser(const std::vector<ArgEntry> &entries, size_t size,
            std::unique_ptr<const parser::Option[]> &&options)
      : OptParser(size, options.get()), entries(entries), options(std::move(options)) {}

  const auto &getEntries() const { return this->entries; }

  void formatUsage(StringRef cmdName, bool printOptions, std::string &out) const;
};

} // namespace ydsh

#endif // YDSH_ARG_PARSER_BASE_H
