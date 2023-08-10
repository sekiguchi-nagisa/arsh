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

#include "misc/buffer.hpp"
#include "misc/enum_util.hpp"
#include "misc/opt_parser.hpp"
#include "misc/resource.hpp"

namespace ydsh {

class ArgEntry {
public:
  enum class Index : unsigned short {};

  static constexpr auto HELP =
      static_cast<Index>(std::numeric_limits<std::underlying_type_t<Index>>::max());

  enum class CheckerKind : unsigned char {
    NOP,    // no check
    INT,    // parse int and check range
    CHOICE, // check choice list
  };

private:
  Index index{0}; // for corresponding field offset
  OptParseOp parseOp{OptParseOp::NO_ARG};
  bool require{false};
  bool positional{false}; // for positional arguments
  bool remain{false};     // for last positional argument (array)
  CheckerKind checkerKind{CheckerKind::NOP};
  char shortOptName{0};
  CStrPtr longOptName{nullptr};  // may be null
  CStrPtr defaultValue{nullptr}; // for OptParseOp::OPT_ARG. may be null
  CStrPtr argName{nullptr};      // may be null
  CStrPtr detail{nullptr};       // may be null
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
  explicit ArgEntry(unsigned int index)
      : index(static_cast<Index>(static_cast<std::underlying_type_t<Index>>(index))),
        intRange({0, 0}) {}

  ~ArgEntry();

  Index getIndex() const { return this->index; }

  unsigned int getIndexAsInt() const { return toUnderlying(this->getIndex()); }

  void setParseOp(OptParseOp op) { this->parseOp = op; }

  OptParseOp getParseOp() const { return this->parseOp; }

  void setRequire(bool r) { this->require = r; }

  bool isRequire() const { return this->require; }

  void setPositional(bool p) { this->positional = p; }

  bool isPositional() const { return this->positional; }

  void setRemainArg(bool r) {
    this->setPositional(r);
    this->remain = r;
  }

  bool isRemainArg() const { return this->remain; }

  void setIntRange(int64_t min, int64_t max) {
    this->destroyCheckerData();
    this->checkerKind = CheckerKind::INT;
    this->intRange.min = min;
    this->intRange.max = max;
  }

  void setChoice(FlexBuffer<char *> &&buf) {
    this->destroyCheckerData();
    if (!buf.empty()) {
      this->checkerKind = CheckerKind::CHOICE;
      this->choice.len = buf.size();
      this->choice.list = std::move(buf).take();
    }
  }

  CheckerKind getCheckerKind() const { return this->checkerKind; }

  void setShortName(char ch) { this->shortOptName = ch; }

  char getShortName() const { return this->shortOptName; }

  void setLongName(const char *name) { this->longOptName.reset(strdup(name)); }

  const char *getLongName() const { return this->longOptName.get(); }

  void setDefaultValue(const char *v) { this->defaultValue.reset(strdup(v)); }

  const char *getDefaultValue() const { return this->defaultValue.get(); }

  void setArgName(const char *name) { this->argName.reset(strdup(name)); }

  const char *getArgName() const { return this->argName.get(); }

  void setDetail(const char *value) { this->detail.reset(strdup(value)); }

  bool isOption() const { return !this->isPositional(); }

  OptParser<ArgEntry::Index>::Option toOption() const;

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

class ArgParser : public OptParser<ArgEntry::Index> {
private:
  using parser = OptParser<ArgEntry::Index>;

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
