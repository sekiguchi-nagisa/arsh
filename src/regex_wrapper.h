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

#define PCRE2_CODE_UNIT_WIDTH 8

#include <pcre2.h>

#include "misc/string_ref.hpp"

namespace ydsh {

struct PCRE {
  pcre2_code *code;
  pcre2_match_data *data;

  NON_COPYABLE(PCRE);

  PCRE() : code(nullptr), data(nullptr) {}

  explicit PCRE(pcre2_code *code) : code(code), data(nullptr) {
    if (this->code) {
      this->data = pcre2_match_data_create_from_pattern(this->code, nullptr);
    }
  }

  PCRE(PCRE &&re) noexcept : code(re.code), data(re.data) {
    re.code = nullptr;
    re.data = nullptr;
  }

  ~PCRE() {
    pcre2_code_free(this->code);
    pcre2_match_data_free(this->data);
  }

  PCRE &operator=(PCRE &&re) noexcept {
    if (this != std::addressof(re)) {
      this->~PCRE();
      new (this) PCRE(std::move(re));
    }
    return *this;
  }

  explicit operator bool() const { return this->code != nullptr; }
};

/**
 * convert flag character to regex flag (option)
 * @param ch
 * @return
 * if specified unsupported flag character, return 0
 */
inline uint32_t toRegexFlag(char ch) {
  switch (ch) {
  case 'i':
    return PCRE2_CASELESS;
  case 'm':
    return PCRE2_MULTILINE;
  case 's':
    return PCRE2_DOTALL;
  default:
    return 0;
  }
}

inline PCRE compileRegex(StringRef pattern, StringRef flag, std::string &errorStr) {
  if (pattern.hasNullChar()) {
    errorStr = "regex pattern contains null characters";
    return PCRE();
  }

  uint32_t option = 0;
  for (auto &e : flag) {
    unsigned int r = toRegexFlag(e);
    if (!r) {
      errorStr = "unsupported regex flag: `";
      errorStr += e;
      errorStr += "'";
      return PCRE();
    }
    setFlag(option, r);
  }

  option |= PCRE2_ALT_BSUX | PCRE2_MATCH_UNSET_BACKREF | PCRE2_UTF | PCRE2_UCP;
  int errcode;
  PCRE2_SIZE erroffset;
  pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern.data(), PCRE2_ZERO_TERMINATED, option,
                                   &errcode, &erroffset, nullptr);
  if (code == nullptr) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errcode, buffer, sizeof(buffer));
    errorStr = reinterpret_cast<const char *>(buffer);
  }
  return PCRE(code);
}

} // namespace ydsh

#endif // YDSH_REGEX_WRAPPER_H
