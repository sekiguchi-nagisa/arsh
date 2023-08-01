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

enum class OptArgOp : unsigned char {
  NO_ARG,
  HAS_ARG, // require additional argument
  OPT_ARG, // may have additional argument
};

constexpr const char *getOptSuffix(OptArgOp flag) {
  return flag == OptArgOp::NO_ARG ? "" : flag == OptArgOp::HAS_ARG ? " arg" : "[=arg]";
}

template <typename T>
struct Option {
  static_assert(std::is_enum_v<T>, "must be enum type");

  T kind;
  const char *optionName;
  OptArgOp flag;
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

enum class OptParseError : unsigned char {
  UNRECOG,
  NEED_ARG,
  END,
};

template <typename T>
class OptParser;

template <typename T>
class OptParseResult {
private:
  bool ok;
  union {
    OptParseError error_;
    T value_;
  };

  const char *recog_;
  const char *arg_;

  friend class OptParser<T>;

public:
  OptParseResult(T value, const char *recog)
      : ok(true), value_(value), recog_(recog), arg_(nullptr) {}

  OptParseResult(OptParseError error, const char *unrecog)
      : ok(false), error_(error), recog_(unrecog), arg_(nullptr) {}

  OptParseResult() : OptParseResult(OptParseError::END, nullptr) {}

  static OptParseResult unrecog(const char *unrecog) {
    return OptParseResult(OptParseError::UNRECOG, unrecog);
  }

  static OptParseResult needArg(const char *recog) {
    return OptParseResult(OptParseError::NEED_ARG, recog);
  }

  static OptParseResult end(const char *opt = nullptr) {
    return OptParseResult(OptParseError::END, opt);
  }

  const char *recog() const { return this->recog_; }

  const char *arg() const { return this->arg_; }

  explicit operator bool() const { return this->ok; }

  OptParseError error() const { return this->error_; }

  T value() const { return this->value_; }

  std::string formatError() const {
    std::string str;
    if (!(*this)) {
      if (this->error() == OptParseError::UNRECOG) {
        str += "invalid option: ";
      } else if (this->error() == OptParseError::NEED_ARG) {
        str += "need argument: ";
      }
    }
    str += this->recog();
    return str;
  }
};

template <typename T>
class OptParser {
private:
  std::vector<Option<T>> options;

public:
  OptParser(std::initializer_list<Option<T>> list);
  ~OptParser() = default;

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
  OptParseResult<T> operator()(Iter &begin, Iter end) const;
};

// #######################
// ##     OptParser     ##
// #######################

template <typename T>
OptParser<T>::OptParser(std::initializer_list<Option<T>> list) : options(list.size()) {
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
std::string OptParser<T>::formatOption() const {
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
OptParseResult<T> OptParser<T>::operator()(Iter &begin, Iter end) const {
  if (begin == end) {
    return OptParseResult<T>::end();
  }

  const char *haystack = *begin;
  if (haystack[0] != '-' || strcmp(haystack, "-") == 0) {
    return OptParseResult<T>::end(haystack);
  }
  if (strcmp(haystack, "--") == 0) {
    ++begin;
    return OptParseResult<T>::end(haystack);
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
    auto result = OptParseResult<T>(option.kind, haystack);
    if (*cursor == '\0') {
      if (option.flag == OptArgOp::HAS_ARG) {
        if (begin + 1 == end) {
          return OptParseResult<T>::needArg(haystack);
        }
        ++begin;
        result.arg_ = *begin;
      }
      ++begin;
      return result;
    } else if (option.flag == OptArgOp::OPT_ARG && *cursor == '=') {
      result.arg_ = ++cursor;
      ++begin;
      return result;
    }
  }
  return OptParseResult<T>::unrecog(haystack);
}

namespace opt {

// getopts like command line option parser
struct GetOptState {
  const char *optStr{nullptr};

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

  explicit GetOptState(const char *optStr) : optStr(optStr) {}

  void reset(const char *str) {
    this->optStr = str;
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
   * @return
   * recognized option.
   * if reach `end' or not match any option (not starts with '-'), return -1.
   * if match '--', increment `begin' and return -1.
   * if not match additional argument, return ':' and `begin' may indicates next option.
   * if match unrecognized option, return '?' and `begin' indicate current option (no increment).
   */
  template <typename Iter>
  int operator()(Iter &begin, Iter end);
};

template <typename Iter>
int GetOptState::operator()(Iter &begin, Iter end) {
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

  auto pos = StringRef(this->optStr).find(this->nextChar[0]);
  const char *ptr = pos == StringRef::npos ? nullptr : this->optStr + pos;
  if (ptr != nullptr && *ptr != ':') {
    if (*(ptr + 1) == ':') {
      this->nextChar.removePrefix(1);
      this->optArg = this->nextChar;
      if (this->optArg.empty()) {
        if (*(ptr + 2) != ':') {
          if (++begin == end) {
            this->optArg = nullptr;
            this->optOpt = static_cast<unsigned char>(*ptr);
            return *this->optStr == ':' ? ':' : '?';
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
