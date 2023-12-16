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

#ifndef ARSH_REGEX_WRAPPER_H
#define ARSH_REGEX_WRAPPER_H

#include "misc/flag_util.hpp"
#include "misc/noncopyable.h"
#include "misc/result.hpp"
#include "misc/string_ref.hpp"

namespace arsh {

struct PCRECapture {
  size_t begin;
  size_t end;
};

struct PCREVersion {
  unsigned int major;
  unsigned int minor;

  explicit operator bool() const { return !(this->major == 0 && this->minor == 0); }
};

#define EACH_PCRE_COMPILE_FLAG(OP)                                                                 \
  OP(CASELESS, (1u << 0u), 'i')                                                                    \
  OP(MULTILINE, (1u << 1u), 'm')                                                                   \
  OP(DOTALL, (1u << 2u), 's')

enum class PCRECompileFlag : unsigned int {
#define GEN_ENUM2(E, B, C) E = B,
  EACH_PCRE_COMPILE_FLAG(GEN_ENUM2)
#undef GEN_ENUN2
};

template <>
struct allow_enum_bitop<PCRECompileFlag> : std::true_type {};

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

  static Optional<PCRECompileFlag> parseCompileFlag(StringRef value, std::string &errorStr);

  static PCRE compile(StringRef pattern, PCRECompileFlag flag, std::string &errorStr);

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

  PCRECompileFlag getCompileFlag() const;

  /**
   *
   * @param ref
   * @param errorStr
   * @return
   * if success, return positive value
   * if 0, no match
   * if error, return negative value
   */
  int match(StringRef ref, std::string &errorStr);

  /**
   *
   * @param index
   * must be less than match count
   * @param capture
   * @return
   * if not set, return false
   */
  bool getCaptureAt(unsigned int index, PCRECapture &capture);

  /**
   *
   * @param target
   * @param replacement
   * @param global
   * if true, replace all matched string
   * if false, replace first matched string
   * @param bufLimit
   * limit of final replaced string length
   * @param output
   * if has error, write error message
   * if success, write replaced string
   * @return
   * if success, return replacement count (may be 0)
   * if has error, return negative value
   */
  int substitute(StringRef target, StringRef replacement, bool global, size_t bufLimit,
                 std::string &output);

private:
  int substituteImpl(StringRef target, StringRef replacement, unsigned int option, char *output,
                     size_t &outputLen);
};

} // namespace arsh

#endif // ARSH_REGEX_WRAPPER_H
