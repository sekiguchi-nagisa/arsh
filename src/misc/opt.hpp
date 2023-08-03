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
