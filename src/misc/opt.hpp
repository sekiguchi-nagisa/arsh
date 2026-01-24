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

#include "string_ref.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

namespace opt {

// getopts like command line option parser
struct GetOptState {
  const char *optStr{nullptr};

  /**
   * currently processed argument.
   */
  StringRef nextChar;

  /**
   * may be null, if has no option argument.
   */
  StringRef optArg;

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

private:
  template <typename Iter>
  int matchShortOption(Iter &begin, Iter end);
};

template <typename Iter>
int GetOptState::operator()(Iter &begin, Iter end) {
  // reset previous state
  this->optArg = nullptr;
  this->optOpt = 0;
  this->foundLongOption = false;

  if (this->nextChar.empty()) {
    if (begin == end) {
      return -1;
    }

    StringRef arg = *begin;
    if (arg.empty() || arg[0] != '-' || arg == "-") {
      return -1;
    } else if (arg.startsWith("--")) {
      if (arg.size() == 2) { // --
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
    assert(arg[0] == '-' && arg.size() > 1);
    this->nextChar = arg;
    this->nextChar.removePrefix(1);
  }
  return this->matchShortOption(begin, end);
}

template <typename Iter>
int GetOptState::matchShortOption(Iter &begin, Iter end) {
  assert(!this->nextChar.empty());
  const char s = this->nextChar[0];
  auto retPos = StringRef(this->optStr).find(s);
  if (retPos == StringRef::npos || *(this->optStr + retPos) == ':') { // not found
    this->optOpt = static_cast<unsigned char>(s);
    return '?';
  }

  this->nextChar.removePrefix(1);
  if (this->nextChar.empty()) {
    ++begin;
  }
  const char *next = this->optStr + retPos + 1;
  if (*next != ':') { // no arg
    return s;
  } else {                        // has arg
    if (this->nextChar.empty()) { // -s arg
      if (*(next + 1) == ':') {   // opt arg
        return s;
      } else if (begin != end) {
        this->optArg = *begin;
        ++begin;
        return s;
      } else {
        this->optOpt = static_cast<unsigned char>(s);
        return *this->optStr == ':' ? ':' : '?';
      }
    } else { // -sarg
      this->optArg = this->nextChar;
      this->nextChar = nullptr;
      ++begin;
      return s;
    }
  }
}

} // namespace opt

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_OPT_HPP
