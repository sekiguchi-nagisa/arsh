/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <memory>

#include <config.h>

#ifdef USE_PCRE

#define PCRE2_CODE_UNIT_WIDTH 8

#include <pcre2.h>

#endif

#include "misc/flag_util.hpp"
#include "misc/num_util.hpp"
#include "regex_wrapper.h"

namespace ydsh {

PCRE::~PCRE() {
#ifdef USE_PCRE
  pcre2_code_free(static_cast<pcre2_code *>(this->code));
  pcre2_match_data_free(static_cast<pcre2_match_data *>(this->data));
#endif
}

/**
 * convert flag character to regex flag (option)
 * @param ch
 * @return
 * if specified unsupported flag character, return 0
 */
static uint32_t toRegexFlag(char ch) {
  switch (ch) {
#ifdef USE_PCRE
  case 'i':
    return PCRE2_CASELESS;
  case 'm':
    return PCRE2_MULTILINE;
  case 's':
    return PCRE2_DOTALL;
#endif
  default:
    return 0;
  }
}

#ifdef USE_PCRE

static auto createCompileCtx() {
  struct Deleter {
    void operator()(pcre2_compile_context *ctx) const { pcre2_compile_context_free(ctx); }
  };

  auto ctx = std::unique_ptr<pcre2_compile_context, Deleter>(pcre2_compile_context_create(nullptr));
  assert(ctx);

#ifndef PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK
#define PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK 0x00000040u
#endif

  auto version = PCRE::version();
  if (version.major >= 10 && version.minor >= 38) { // for backward-compatibility
    pcre2_set_compile_extra_options(ctx.get(), PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK);
  }
  return ctx;
}

#endif

PCRE PCRE::compile(StringRef pattern, StringRef flag, std::string &errorStr) {
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

#ifdef USE_PCRE
  static auto compileCtx = createCompileCtx();

  option |= PCRE2_ALT_BSUX | PCRE2_MATCH_UNSET_BACKREF | PCRE2_UTF | PCRE2_UCP;
  int errcode;
  PCRE2_SIZE erroffset;
  pcre2_code *code = pcre2_compile((PCRE2_SPTR)pattern.data(), PCRE2_ZERO_TERMINATED, option,
                                   &errcode, &erroffset, compileCtx.get());
  pcre2_match_data *data = nullptr;
  if (code) {
    data = pcre2_match_data_create_from_pattern(code, nullptr);
  } else {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(errcode, buffer, sizeof(buffer));
    errorStr = reinterpret_cast<const char *>(buffer);
  }
  return PCRE(code, data);
#else
  errorStr = "regex is not supported";
  return PCRE();
#endif
}

static PCREVersion getVersion() {
  StringRef ref = "0.0 2999-12-31";

#ifdef USE_PCRE
  char data[64];
  pcre2_config(PCRE2_CONFIG_VERSION, data);
  ref = data;
#endif

  auto pos = ref.indexOf(" ");
  ref = ref.slice(0, pos);
  pos = ref.indexOf(".");
  StringRef vv1 = ref.slice(0, pos);
  StringRef vv2 = ref.slice(pos + 1, ref.size());

  auto pair = convertToNum<unsigned int>(vv1.begin(), vv1.end());
  assert(pair.second);
  unsigned int major = pair.first;

  pair = convertToNum<unsigned int>(vv2.begin(), vv2.end());
  assert(pair.second);
  unsigned int minor = pair.first;

  return PCREVersion{
      .major = major,
      .minor = minor,
  };
}

PCREVersion PCRE::version() {
  static PCREVersion v = getVersion();
  return v;
}

int PCRE::match(StringRef ref, std::string &errorStr) {
#ifdef USE_PCRE
  int matchCount =
      pcre2_match(static_cast<pcre2_code *>(this->code), (PCRE2_SPTR)ref.data(), ref.size(), 0, 0,
                  static_cast<pcre2_match_data *>(this->data), nullptr);
  if (matchCount < 0 && matchCount != PCRE2_ERROR_NOMATCH) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(matchCount, buffer, sizeof(buffer));
    errorStr = reinterpret_cast<const char *>(buffer);
  }
  return matchCount;
#else
  (void)ref;
  errorStr = "regex is not supported";
  return -999;
#endif
}

bool PCRE::getCaptureAt(unsigned int index, PCRECapture &capture) {
#ifdef USE_PCRE
  PCRE2_SIZE *ovec = pcre2_get_ovector_pointer(static_cast<pcre2_match_data *>(this->data));
  size_t begin = ovec[index * 2];
  size_t end = ovec[index * 2 + 1];
  if (begin == PCRE2_UNSET || end == PCRE2_UNSET) {
    return false;
  }
  capture = {
      .begin = begin,
      .end = end,
  };
  return true;
#else
  (void)index;
  (void)capture;
  return false;
#endif
}

} // namespace ydsh