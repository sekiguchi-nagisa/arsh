/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_REGEX_RADIX_HELPER_H
#define ARSH_REGEX_RADIX_HELPER_H

#include <string>
#include <utility>

#include "input.h"
#include "unicode/case_fold.h"
#include "unicode/radix_tree.h"

namespace arsh::regex {

inline std::pair<unsigned short, unsigned char> findLongestMatched(const PackedRadixTree tree,
                                                                   StringRef ref, std::string &foldBuf,
                                                                   bool caseFold) {
  const auto old = ref;
  if (caseFold) {
    foldBuf.clear();
    const char *iter = ref.begin();
    const char *end = ref.end();
    while (iter != end) {
      int codePoint = doSimpleCaseFolding(unsafeNextUtf8(iter));
      char data[4];
      const auto len = UnicodeUtil::codePointToUtf8(codePoint, data);
      assert(len);
      foldBuf.append(data, len);
    }
    ref = foldBuf;
  }
  auto [s, p] = tree.findLongestMatched(ref);
  if (caseFold && p) { // remap to original byte size
    ref = ref.substr(0, s);
    const char *begin = ref.begin();
    const char *const end = ref.end();
    const char *oldBegin = old.begin();
    const char *const oldEnd = old.end();
    while (begin != end && oldBegin != oldEnd) {
      unsafeNextUtf8Noreturn(begin);
      unsafeNextUtf8Noreturn(oldBegin);
    }
    s = oldBegin - old.begin();
  }
  return {s, p};
}

inline std::pair<unsigned short, unsigned char>
findBackwardLongestMatched(const PackedRadixTree tree, StringRef ref, std::string &foldBuf,
                           const bool caseFold) {
  auto old = ref;
  if (caseFold) {
    foldBuf.clear();
    const char *iter = ref.begin();
    const char *end = ref.end();
    while (iter != end) {
      int codePoint = doSimpleCaseFolding(unsafeNextUtf8(iter));
      char data[4];
      const auto len = UnicodeUtil::codePointToUtf8(codePoint, data);
      assert(len);
      foldBuf.append(data, len);
    }
    ref = foldBuf;
  }
  while (!ref.empty()) {
    auto [s, p] = tree.findLongestMatched(ref);
    if (caseFold && p) { // remap to original byte size
      StringRef sub = ref.substr(0, s);
      const char *begin = sub.begin();
      const char *const end = sub.end();
      const char *oldBegin = old.begin();
      const char *const oldEnd = old.end();
      while (begin != end && oldBegin != oldEnd) {
        unsafeNextUtf8Noreturn(begin);
        unsafeNextUtf8Noreturn(oldBegin);
      }
      s = oldBegin - old.begin();
    }
    if (s == old.size()) {
      return {s, p};
    }
    unsafeRemovePrefixUtf8(ref);
    if (caseFold) {
      unsafeRemovePrefixUtf8(old);
    } else {
      old = ref;
    }
  }
  return {0, 0};
}

} // namespace arsh::regex

#endif // ARSH_REGEX_RADIX_HELPER_H
