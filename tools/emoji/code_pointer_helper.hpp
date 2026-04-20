
#ifndef ARSH_CODE_POINTER_HELPER_HPP
#define ARSH_CODE_POINTER_HELPER_HPP

#include <array>

#include <misc/unicode.hpp>
#include <unicode/case_fold.h>

namespace arsh {

constexpr unsigned int MAX_TEST_CODEPOINT_SIZE = 16;

template <typename... T>
constexpr std::array<int, MAX_TEST_CODEPOINT_SIZE> toArray(T... arg) {
  static_assert(sizeof...(T) <= MAX_TEST_CODEPOINT_SIZE);
  std::array<int, MAX_TEST_CODEPOINT_SIZE> ret = {arg...};
  return ret;
}

struct CodePointArray {
  std::array<int, MAX_TEST_CODEPOINT_SIZE> codePoints;
  unsigned int usedSize;

  template <typename... T>
  constexpr CodePointArray(int first, T &&...remain) // NOLINT
      : codePoints(toArray(first, std::forward<T>(remain)...)), usedSize(sizeof...(T) + 1) {}

  std::string toUTF8() const {
    std::string str;
    for (unsigned int i = 0; i < this->usedSize; i++) {
      int code = this->codePoints[i];
      char buf[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(code, buf);
      str += StringRef(buf, len);
    }
    return str;
  }

  std::string toUTF8CaseFold() const {
    std::string str;
    for (unsigned int i = 0; i < this->usedSize; i++) {
      int code = doSimpleCaseFolding(this->codePoints[i]);
      char buf[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(code, buf);
      str += StringRef(buf, len);
    }
    return str;
  }
};

} // namespace arsh

#endif // ARSH_CODE_POINTER_HELPER_HPP
