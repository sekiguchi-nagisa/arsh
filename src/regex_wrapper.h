/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef YDSH_REGEX_WRAPPER_H
#define YDSH_REGEX_WRAPPER_H

#include "misc/noncopyable.h"
#include "misc/string_ref.hpp"

namespace ydsh {

struct PCRECapture {
  size_t begin;
  size_t end;
};

struct PCREVersion {
  unsigned int major;
  unsigned int minor;

  explicit operator bool() const { return !(this->major == 0 && this->minor == 0); }
};

class PCRE {
private:
  char *pattern; // original pattern string
  void *code;    // pcre2_code
  void *data;    // pcre2_match_data

public:
  NON_COPYABLE(PCRE);

  PCRE() : pattern(nullptr), code(nullptr), data(nullptr) {}

  explicit PCRE(char *pattern, void *code, void *data) : pattern(pattern), code(code), data(data) {}

  PCRE(PCRE &&re) noexcept : pattern(re.pattern), code(re.code), data(re.data) {
    re.pattern = nullptr;
    re.code = nullptr;
    re.data = nullptr;
  }

  ~PCRE();

  static PCRE compile(StringRef pattern, StringRef flag, std::string &errorStr);

  static PCREVersion version();

  PCRE &operator=(PCRE &&re) noexcept {
    if (this != std::addressof(re)) {
      this->~PCRE();
      new (this) PCRE(std::move(re));
    }
    return *this;
  }

  explicit operator bool() const { return this->code != nullptr; }

  const char *getPattern() const { return this->pattern; }

  int match(StringRef ref, std::string &errorStr);

  /**
   *
   * @param index
   * @param capture
   * @return
   * if not set, return false
   */
  bool getCaptureAt(unsigned int index, PCRECapture &capture);
};

} // namespace ydsh

#endif // YDSH_REGEX_WRAPPER_H
