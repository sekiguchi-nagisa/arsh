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

#ifndef MISC_LIB_OPT_PARSER_HPP
#define MISC_LIB_OPT_PARSER_HPP

#include <vector>

#include "format.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

template <typename T>
class OptParseResult {
public:
  static_assert(std::is_enum_v<T>, "must be enum type");

  enum class Status : unsigned char {
    OK,        // recognize option
    OK_ARG,    // recognize option and argument
    UNDEF,     // undefined option
    NEED_ARG,  // option recognized, but missing argument
    REACH_END, // stop parsing (successfully recognize all options (may remain arguments))
  };

private:
  Status status;

  /**
   * recognized option identifier
   * only available if status is OK or NEED_ARG
   */
  T opt;

  /**
   * if status is OK, indicate recognized argument
   *   (if specify OPT_ARG and no argument, will be null
   * if status is UNDEF, indicate unrecognized option name (without prefix - or --)
   */
  StringRef value;

public:
  OptParseResult(Status s, T o, StringRef v) : status(s), opt(o), value(v) {}

  OptParseResult() : OptParseResult(Status::REACH_END, T{}, nullptr) {}

  static OptParseResult ok(T o) { return {Status::OK, o, ""}; }

  static OptParseResult ok(T o, StringRef arg) { return {Status::OK_ARG, o, arg}; }

  static OptParseResult undef(StringRef opt) { return {Status::UNDEF, T{}, opt}; }

  static OptParseResult needArg(T o, StringRef opt) { return {Status::NEED_ARG, o, opt}; }

  explicit operator bool() const {
    return this->status == Status::OK || this->status == Status::OK_ARG;
  }

  Status getStatus() const { return this->status; }

  T getOpt() const { return this->opt; }

  StringRef getValue() const { return this->value; }

  bool hasArg() const { return this->status == Status::OK_ARG; }

  bool isEnd() const { return this->status == Status::REACH_END; }

  bool isError() const { return this->status == Status::UNDEF || this->status == Status::NEED_ARG; }

  bool formatError(std::string &out) const {
    const char *prefix;
    switch (this->status) {
    case Status::UNDEF:
      prefix = "invalid option: ";
      break;
    case Status::NEED_ARG:
      prefix = "need argument: ";
      break;
    default:
      return false;
    }
    out += prefix;
    out += this->value.size() == 1 ? "-" : "--";
    out += this->value;
    return true;
  }

  std::string formatError() const {
    std::string v;
    this->formatError(v);
    return v;
  }
};

enum class OptParseOp : unsigned char {
  NO_ARG,
  HAS_ARG, // require additional argument
  OPT_ARG, // may have additional argument
};

template <typename T>
struct OptParseOption {
  using type = T;

  T kind{};
  OptParseOp op{OptParseOp::NO_ARG};
  char shortOptName{0};    // may be null char if no short option
  std::string longOptName; // may be null if no long option
  std::string argName;     // argument name for help message
  std::string detail;      // option description for help message

  OptParseOption() = default;

  explicit OptParseOption(T kind) : kind(kind) {}

  OptParseOption(T kind, char s, const char *l, OptParseOp op, const char *arg, const char *detail)
      : kind(kind), op(op), shortOptName(s), longOptName(l), argName(arg), detail(detail) {}

  OptParseOption(T kind, char s, const char *l, OptParseOp op, const char *detail) // NOLINT
      : OptParseOption(kind, s, l, op, "arg", detail) {}

  unsigned int getUsageLen() const {
    unsigned int ret = 0;
    switch (this->op) {
    case OptParseOp::NO_ARG:
    case OptParseOp::HAS_ARG:
      if (this->shortOptName) {
        ret += 2; // -s
      }
      if (!this->longOptName.empty()) {
        if (ret) { // ', '
          ret += 2;
        }
        ret += 2; // --long
        ret += this->longOptName.size();
      }
      if (this->op == OptParseOp::HAS_ARG) { // -v arg
        ret++;
        ret += this->argName.size();
      }
      break;
    case OptParseOp::OPT_ARG: { // -s[arg], --long[=arg]
      const auto len = this->argName.size();
      if (this->shortOptName) { // -s[arg]
        ret += 4;
        ret += len;
      }
      if (!this->longOptName.empty()) { // --long[=arg]
        if (ret) {                      // ', '
          ret += 2;
        }
        ret += 2;
        ret += this->longOptName.size();
        ret += 3;
        ret += len;
      }
      break;
    }
    }
    return ret;
  }

  std::string getUsage() const {
    std::string ret;
    switch (this->op) {
    case OptParseOp::NO_ARG:
    case OptParseOp::HAS_ARG:
      if (this->shortOptName) {
        ret += '-'; // -s
        ret += this->shortOptName;
      }
      if (!this->longOptName.empty()) {
        if (!ret.empty()) { // ', '
          ret += ", ";
        }
        ret += "--"; // --long
        ret += this->longOptName;
      }
      if (this->op == OptParseOp::HAS_ARG) { // -v arg
        ret += ' ';
        ret += this->argName;
      }
      break;
    case OptParseOp::OPT_ARG:
      if (this->shortOptName) { // -s[arg]
        ret += '-';
        ret += this->shortOptName;
        ret += '[';
        ret += this->argName;
        ret += ']';
      }
      if (!this->longOptName.empty()) { // --long[=arg]
        if (!ret.empty()) {             // ', '
          ret += ", ";
        }
        ret += "--";
        ret += this->longOptName;
        ret += "[=";
        ret += this->argName;
        ret += ']';
      }
      break;
    }
    return ret;
  }

  void splitDetails(std::vector<StringRef> &out) const {
    out.clear();
    StringRef ref = this->detail;
    if (!ref.empty()) {
      splitByDelim(ref, '\n', [&out](StringRef sub, bool) {
        out.push_back(sub);
        return true;
      });
    }
  }
};

template <typename T, typename U = OptParseOption<T>>
class OptParser {
public:
  static_assert(std::is_enum_v<T>, "must be enum type");

  using Option = U;
  using Result = OptParseResult<T>;

private:
  const size_t size;
  const Option *const options;
  StringRef remain; // for short option

public:
  OptParser(size_t size, const Option *const options) : size(size), options(options) {}

  template <typename Iter>
  OptParseResult<T> operator()(Iter &begin, Iter end);

  void formatOptions(std::string &out) const;

  std::string formatOptions() const {
    std::string value;
    this->formatOptions(value);
    return value;
  }

  StringRef getRemain() const { return this->remain; }

  void reset() { this->remain = ""; }

private:
  template <typename Iter>
  OptParseResult<T> matchLongOption(Iter &begin, Iter end);

  template <typename Iter>
  OptParseResult<T> matchShortOption(Iter &begin, Iter end);
};

template <typename T, size_t N>
auto createOptParser(const T (&options)[N]) {
  return OptParser<typename T::type>(N, options);
}

// #######################
// ##     OptParser     ##
// #######################

template <typename T, typename U>
template <typename Iter>
OptParseResult<T> OptParser<T, U>::operator()(Iter &begin, Iter end) {
  if (this->remain.empty()) {
    if (begin == end) {
      return OptParseResult<T>();
    }

    StringRef arg = *begin;
    if (arg.empty() || arg[0] != '-' || arg == "-") {
      return OptParseResult<T>();
    } else if (arg.startsWith("--")) {
      if (arg.size() == 2) { // --
        ++begin;
        return OptParseResult<T>();
      } else {
        return this->matchLongOption(begin, end);
      }
    }
    assert(arg[0] == '-' && arg.size() > 1);
    this->remain = arg;
    this->remain.removePrefix(1);
    ++begin;
  }
  return this->matchShortOption(begin, end);
}

template <typename T, typename U>
template <typename Iter>
OptParseResult<T> OptParser<T, U>::matchLongOption(Iter &begin, Iter end) {
  StringRef longName = *begin;
  assert(longName.size() > 2);
  longName.removePrefix(2);
  for (unsigned int i = 0; i < this->size; i++) {
    const auto &option = this->options[i];
    if (option.longOptName.empty()) {
      continue;
    }
    switch (option.op) {
    case OptParseOp::NO_ARG:
      if (option.longOptName == longName) {
        ++begin;
        return OptParseResult<T>::ok(option.kind);
      }
      continue;
    case OptParseOp::HAS_ARG:
    case OptParseOp::OPT_ARG:
      if (longName.startsWith(option.longOptName)) {
        ++begin;
        StringRef v = longName;
        v.removePrefix(option.longOptName.size());
        if (v.empty()) { // --long arg
          if (option.op == OptParseOp::OPT_ARG) {
            return OptParseResult<T>::ok(option.kind);
          } else if (begin != end) {
            StringRef next = *begin;
            ++begin;
            return OptParseResult<T>::ok(option.kind, next);
          } else {
            return OptParseResult<T>::needArg(option.kind, longName);
          }
        } else if (v[0] == '=') { // --long=arg
          v.removePrefix(1);
          return OptParseResult<T>::ok(option.kind, v);
        } else { // no match
          --begin;
        }
      }
      continue;
    }
  }
  auto pos = longName.find('=');
  longName = longName.slice(0, pos);
  return OptParseResult<T>::undef(longName);
}

template <typename T, typename U>
template <typename Iter>
OptParseResult<T> OptParser<T, U>::matchShortOption(Iter &begin, Iter end) {
  assert(!this->remain.empty());
  char s = this->remain[0];
  const StringRef shortName = this->remain.slice(0, 1);
  for (unsigned int i = 0; i < this->size; i++) {
    const auto &option = this->options[i];
    if (!option.shortOptName || s != option.shortOptName) {
      continue;
    }
    this->remain.removePrefix(1);
    switch (option.op) {
    case OptParseOp::NO_ARG:
      return OptParseResult<T>::ok(option.kind);
    case OptParseOp::HAS_ARG: // -s arg
      if (begin != end) {
        StringRef next = *begin;
        ++begin;
        return OptParseResult<T>::ok(option.kind, next);
      } else {
        return OptParseResult<T>::needArg(option.kind, shortName);
      }
    case OptParseOp::OPT_ARG: // -sarg
      if (this->remain.empty()) {
        return OptParseResult<T>::ok(option.kind);
      } else {
        StringRef next = this->remain;
        this->remain = "";
        return OptParseResult<T>::ok(option.kind, next);
      }
    }
  }
  return OptParseResult<T>::undef(shortName);
}

template <typename T, typename U>
void OptParser<T, U>::formatOptions(std::string &value) const {
  unsigned int maxLenOfUsage = 0;

  // compute usage len
  for (unsigned int i = 0; i < this->size; i++) {
    const auto &option = this->options[i];
    unsigned int len = option.getUsageLen();
    if (len > maxLenOfUsage) {
      maxLenOfUsage = len;
    }
  }
  std::string spaces;
  spaces.resize(maxLenOfUsage, ' ');

  // format option list message
  std::vector<StringRef> details;
  value += "Options:";
  for (unsigned int i = 0; i < this->size; i++) {
    const auto &option = this->options[i];
    value += "\n  ";
    auto usage = option.getUsage();
    value += usage;

    option.splitDetails(details);
    if (!details.empty()) {
      value.append(maxLenOfUsage - usage.size(), ' ');
    }
    unsigned int count = 0;
    for (auto &detail : details) {
      if (count++ > 0) {
        value += "\n";
        value += spaces;
        value += "  ";
      }
      value += "  ";
      value += detail;
    }
  }
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_OPT_PARSER_HPP
