/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef MISC_LIB_OPT_HPP
#define MISC_LIB_OPT_HPP

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>

#include "flag_util.hpp"
#include "string_ref.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

namespace opt {

enum OptionFlag : unsigned char {
  NO_ARG,
  HAS_ARG, // require additional argument
  OPT_ARG, // may have additional argument
};

constexpr const char *getOptSuffix(OptionFlag flag) {
  return flag == NO_ARG ? "" : flag == HAS_ARG ? " arg" : "[=arg]";
}

template <typename T>
struct Option {
  static_assert(std::is_enum<T>::value, "must be enum type");

  T kind;
  const char *optionName;
  OptionFlag flag;
  const char *detail;

  unsigned int getUsageSize() const {
    return strlen(this->optionName) + strlen(getOptSuffix(this->flag));
  }

  std::vector<std::string> getDetails() const;
};

template <typename T>
std::vector<std::string> Option<T>::getDetails() const {
  std::vector<std::string> bufs;
  bufs.emplace_back();
  for (unsigned int i = 0; this->detail[i] != '\0'; i++) {
    int ch = this->detail[i];
    if (ch == '\n') {
      if (!bufs.back().empty()) {
        bufs.emplace_back();
      }
    } else {
      bufs.back() += static_cast<char>(ch);
    }
  }
  return bufs;
}

enum Error : unsigned char {
  UNRECOG,
  NEED_ARG,
  END,
};

template <typename T>
class Parser;

template <typename T>
class Result {
private:
  bool ok;
  union {
    Error error_;
    T value_;
  };

  const char *recog_;
  const char *arg_;

  friend class Parser<T>;

public:
  Result(T value, const char *recog) : ok(true), value_(value), recog_(recog), arg_(nullptr) {}

  Result(Error error, const char *unrecog)
      : ok(false), error_(error), recog_(unrecog), arg_(nullptr) {}

  Result() : Result(END, nullptr) {}

  static Result unrecog(const char *unrecog) { return Result(UNRECOG, unrecog); }

  static Result needArg(const char *recog) { return Result(NEED_ARG, recog); }

  static Result end(const char *opt = nullptr) { return Result(END, opt); }

  const char *recog() const { return this->recog_; }

  const char *arg() const { return this->arg_; }

  explicit operator bool() const { return this->ok; }

  Error error() const { return this->error_; }

  T value() const { return this->value_; }

  std::string formatError() const {
    std::string str;
    if (!(*this)) {
      if (this->error() == UNRECOG) {
        str += "invalid option: ";
      } else if (this->error() == NEED_ARG) {
        str += "need argument: ";
      }
    }
    str += this->recog();
    return str;
  }
};

template <typename T>
class Parser {
private:
  std::vector<Option<T>> options;

public:
  Parser(std::initializer_list<Option<T>> list);
  ~Parser() = default;

  std::string formatOption() const;

  void printOption(FILE *fp) const {
    std::string value = this->formatOption();
    fprintf(fp, "%s\n", value.c_str());
    fflush(fp);
  }

  std::ostream &printOption(std::ostream &stream) const {
    return stream << this->formatOption() << std::endl;
  }

  template <typename Iter>
  Result<T> operator()(Iter &begin, Iter end) const;
};

// ####################
// ##     Parser     ##
// ####################

template <typename T>
Parser<T>::Parser(std::initializer_list<Option<T>> list) : options(list.size()) {
  // init options
  unsigned int count = 0;
  for (auto &e : list) {
    this->options[count++] = e;
  }
  std::sort(this->options.begin(), this->options.end(), [](const Option<T> &x, const Option<T> &y) {
    return strcmp(x.optionName, y.optionName) < 0;
  });
}

template <typename T>
std::string Parser<T>::formatOption() const {
  std::string value;
  unsigned int maxSizeOfUsage = 0;

  // compute usage size
  for (auto &option : this->options) {
    unsigned int size = option.getUsageSize();
    if (size > maxSizeOfUsage) {
      maxSizeOfUsage = size;
    }
  }

  std::string spaces;
  for (unsigned int i = 0; i < maxSizeOfUsage; i++) {
    spaces += ' ';
  }

  // print help message
  value += "Options:";
  for (auto &option : this->options) {
    value += "\n";
    unsigned int size = option.getUsageSize();
    value += "    ";
    value += option.optionName;
    value += getOptSuffix(option.flag);
    for (unsigned int i = 0; i < maxSizeOfUsage - size; i++) {
      value += " ";
    }

    unsigned int count = 0;
    for (auto &detail : option.getDetails()) {
      if (count++ > 0) {
        value += "\n";
        value += spaces;
        value += "    ";
      }
      value += "    ";
      value += detail;
    }
  }
  return value;
}

template <typename T>
template <typename Iter>
Result<T> Parser<T>::operator()(Iter &begin, Iter end) const {
  if (begin == end) {
    return Result<T>::end();
  }

  const char *haystack = *begin;
  if (haystack[0] != '-' || strcmp(haystack, "-") == 0) {
    return Result<T>::end(haystack);
  }
  if (strcmp(haystack, "--") == 0) {
    ++begin;
    return Result<T>::end(haystack);
  }

  // check options
  unsigned int haystackSize = strlen(haystack);
  for (auto &option : this->options) {
    const char *needle = option.optionName;
    unsigned int needleSize = strlen(needle);
    if (needleSize > haystackSize || memcmp(needle, haystack, needleSize) != 0) {
      continue;
    }
    const char *cursor = haystack + needleSize;
    auto result = Result<T>(option.kind, haystack);
    if (*cursor == '\0') {
      if (option.flag == OptionFlag::HAS_ARG) {
        if (begin + 1 == end) {
          return Result<T>::needArg(haystack);
        }
        ++begin;
        result.arg_ = *begin;
      }
      ++begin;
      return result;
    } else if (option.flag == OptionFlag::OPT_ARG && *cursor == '=') {
      result.arg_ = ++cursor;
      ++begin;
      return result;
    }
  }
  return Result<T>::unrecog(haystack);
}

// getopts like command line option parser
struct GetOptState {
  /**
   * currently processed argument.
   */
  StringRef nextChar{nullptr};

  /**
   * may be null, if has no optional argument.
   */
  StringRef optArg{nullptr};

  /**
   * unrecognized option.
   */
  int optOpt{0};

  /**
   * for `--help` option recognition
   */
  bool remapHelp{false};

  bool foundLongOption{false};

  void reset() {
    this->nextChar = nullptr;
    this->optArg = nullptr;
    this->optOpt = 0;
    this->remapHelp = false;
    this->foundLongOption = false;
  }

  /**
   *
   * @tparam Iter
   * @param begin
   * after succeed, may indicate next option.
   * @param end
   * @param optStr
   * @return
   * recognized option.
   * if reach `end' or not match any option (not starts with '-'), return -1.
   * if match '--', increment `begin' and return -1.
   * if not match additional argument, return ':' and `begin' may indicates next option.
   * if match unrecognized option, return '?' and `begin' indicate current option (no increment).
   */
  template <typename Iter>
  int operator()(Iter &begin, Iter end, const char *optStr);
};

template <typename Iter>
int GetOptState::operator()(Iter &begin, Iter end, const char *optStr) {
  // reset previous state
  this->optArg = nullptr;
  this->optOpt = 0;

  if (begin == end) {
    this->nextChar = nullptr;
    return -1;
  }

  StringRef arg = *begin;
  if (!arg.startsWith("-") || arg == "-") {
    this->nextChar = nullptr;
    return -1;
  }

  if (arg.startsWith("--")) {
    if (arg.size() == 2) {
      this->nextChar = nullptr;
      ++begin;
      return -1;
    } else if (arg == "--help" && this->remapHelp) {
      arg = "-h";
    } else {
      this->nextChar = arg;
      this->optOpt = '-';
      this->foundLongOption = true;
      return '?';
    }
  }

  if (this->nextChar.empty()) {
    this->nextChar = arg;
    this->nextChar.removePrefix(1);
  }

  auto pos = StringRef(optStr).find(this->nextChar[0]);
  const char *ptr = pos == StringRef::npos ? nullptr : optStr + pos;
  if (ptr != nullptr && *ptr != ':') {
    if (*(ptr + 1) == ':') {
      this->nextChar.removePrefix(1);
      this->optArg = this->nextChar;
      if (this->optArg.empty()) {
        if (*(ptr + 2) != ':') {
          if (++begin == end) {
            this->optArg = nullptr;
            this->optOpt = static_cast<unsigned char>(*ptr);
            return *optStr == ':' ? ':' : '?';
          }
          this->optArg = *begin;
        } else {
          this->optArg = nullptr;
        }
      }
      this->nextChar = nullptr;
    }

    if (this->nextChar.empty()) {
      ++begin;
    } else {
      this->nextChar.removePrefix(1);
      if (this->nextChar.empty()) {
        ++begin;
      }
    }
    return *ptr;
  }
  this->optOpt = static_cast<unsigned char>(this->nextChar[0]);
  return '?';
}

} // namespace opt

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_OPT_HPP
