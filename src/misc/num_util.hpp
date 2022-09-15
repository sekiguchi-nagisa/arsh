/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#ifndef MISC_LIB_NUM_UTIL_HPP
#define MISC_LIB_NUM_UTIL_HPP

#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#ifdef __APPLE__
#include <xlocale.h>
#else
#include <locale.h>
#endif

#include "detect.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

// in clang 3.6/3.7, generic __builtin_mul_overflow and __builtin_add_overflow is not defined

// add
inline bool add_overflow(unsigned int x, unsigned int y, unsigned int &r) {
  return __builtin_uadd_overflow(x, y, &r);
}

inline bool add_overflow(unsigned long x, unsigned long y, unsigned long &r) {
  return __builtin_uaddl_overflow(x, y, &r);
}

inline bool add_overflow(unsigned long long x, unsigned long long y, unsigned long long &r) {
  return __builtin_uaddll_overflow(x, y, &r);
}

inline bool sadd_overflow(int x, int y, int &r) { return __builtin_sadd_overflow(x, y, &r); }

inline bool sadd_overflow(long x, long y, long &r) { return __builtin_saddl_overflow(x, y, &r); }

inline bool sadd_overflow(long long x, long long y, long long &r) {
  return __builtin_saddll_overflow(x, y, &r);
}

// sub
inline bool ssub_overflow(int x, int y, int &r) { return __builtin_ssub_overflow(x, y, &r); }

inline bool ssub_overflow(long x, long y, long &r) { return __builtin_ssubl_overflow(x, y, &r); }

inline bool ssub_overflow(long long x, long long y, long long &r) {
  return __builtin_ssubll_overflow(x, y, &r);
}

// mul
inline bool mul_overflow(unsigned int x, unsigned int y, unsigned int &r) {
  return __builtin_umul_overflow(x, y, &r);
}

inline bool mul_overflow(unsigned long x, unsigned long y, unsigned long &r) {
  return __builtin_umull_overflow(x, y, &r);
}

inline bool mul_overflow(unsigned long long x, unsigned long long y, unsigned long long &r) {
  return __builtin_umulll_overflow(x, y, &r);
}

inline bool smul_overflow(int x, int y, int &r) { return __builtin_smul_overflow(x, y, &r); }

inline bool smul_overflow(long x, long y, long &r) { return __builtin_smull_overflow(x, y, &r); }

inline bool smul_overflow(long long x, long long y, long long &r) {
  return __builtin_smulll_overflow(x, y, &r);
}

inline unsigned int parseBase(const char *&begin, const char *end) {
  if (begin == end) {
    return 0; // failed
  }

  if (*begin == '0') {
    if (begin + 1 != end) {
      switch (*++begin) {
      case 'x':
      case 'X':
        ++begin;
        return 16;
      case 'o':
      case 'O':
        ++begin;
        return 8;
      }
    }
    return 8;
  }
  return 10;
}

template <typename T>
inline auto dropSign(T v) {
  static_assert(std::is_signed<T>::value, "must be signed type");
  using UT = std::make_unsigned_t<T>;
  return static_cast<UT>(~static_cast<UT>(v) + 1);
}

template <typename UT>
inline auto putSign(UT v) {
  static_assert(std::is_unsigned<UT>::value, "must be unsigned type");
  using T = std::make_signed_t<UT>;
  return static_cast<T>(~v + 1);
}

template <typename U>
inline std::pair<std::make_signed_t<U>, bool> makeSigned(U v, bool negate) {
  static_assert(std::is_unsigned<U>::value, "must be unsigned type");

  using T = std::make_signed_t<U>;
  if (negate) {
    if (v <= dropSign(std::numeric_limits<T>::min())) {
      return {putSign(v), true};
    }
  } else {
    if (v <= static_cast<U>(std::numeric_limits<T>::max())) {
      return {static_cast<T>(v), true};
    }
  }
  return {static_cast<T>(v), false};
}

template <typename T,
          enable_when<std::is_unsigned<T>::value &&
                      (sizeof(T) == sizeof(int32_t) || sizeof(T) == sizeof(int64_t))> = nullptr>
inline std::pair<T, bool> parseInteger(const char *&begin, const char *end, unsigned int base) {
  if (begin != end && *begin == '+') {
    ++begin;
  }

  if (base == 0) {
    base = parseBase(begin, end);
  }

  errno = 0;
  if (begin == end || base < 2 || base > 36) {
    errno = EINVAL;
    return {0, false};
  }

  T radix = static_cast<T>(base);
  T ret = 0;
  bool status = true;
  do {
    char ch = *begin;
    T v;
    if (ch >= '0' && ch <= '9') {
      v = ch - '0';
    } else if (ch >= 'a' && ch <= 'z') {
      v = 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'Z') {
      v = 10 + (ch - 'A');
    } else {
      errno = EINVAL;
      status = false;
      break;
    }

    if (v >= radix) {
      errno = EINVAL;
      status = false;
      break;
    }

    if (mul_overflow(ret, radix, ret) || // ret = ret * radix
        add_overflow(ret, v, ret)) {     // ret = ret + v
      errno = ERANGE;
      status = false;
      break;
    }
  } while (++begin != end);

  return {ret, status};
}

template <typename T,
          enable_when<std::is_signed<T>::value &&
                      (sizeof(T) == sizeof(int32_t) || sizeof(T) == sizeof(int64_t))> = nullptr>
inline std::pair<T, bool> parseInteger(const char *&begin, const char *end, unsigned int base) {
  bool sign = false;
  if (begin != end && *begin == '-' && *(begin + 1) != '+') {
    sign = true;
    ++begin;
  }

  using UT = std::make_unsigned_t<T>;
  auto ret = parseInteger<UT>(begin, end, base);
  if (ret.second) {
    return makeSigned(ret.first, sign);
  }
  return {static_cast<T>(ret.first), false};
}

/**
 * for string to number conversion
 * @tparam T
 * @param begin
 * @param end
 * @param base
 * if 0, auto detect radix
 *   if starts with '0x', '0X', parse as hex number
 *   if starts with '0', '0o', '0O', parse as octal number
 * if 8, parse as octal number
 * if 10, parse as decimal number
 * if 16, parse as hex number
 *
 * if starts with '-' and T is signed type, negate number
 * @return
 * if detect overflow, return {0, false}
 */
template <typename T, enable_when<std::is_integral<T>::value> = nullptr>
inline std::pair<T, bool> convertToNum(const char *begin, const char *end, unsigned int base = 0) {
  return parseInteger<T>(begin, end, base);
}

/**
 *
 * @tparam T
 * @param str
 * must be null terminated string
 * @param base
 * @return
 */
template <typename T, enable_when<std::is_integral<T>::value> = nullptr>
inline std::pair<T, bool> convertToNum(const char *str, unsigned int base = 0) {
  return convertToNum<T>(str, str + strlen(str), base);
}

class Locale {
private:
  locale_t value;

public:
  Locale(int mask, const char *locale, locale_t base = nullptr)
      : value(newlocale(mask, locale, base)) {}

  ~Locale() { freelocale(this->value); }

  locale_t get() const { return this->value; }

  static locale_t C() {
    static Locale clocale(LC_ALL_MASK, "C");
    return clocale.get();
  }
};

/**
 * parse double with locale independent way
 * @param str
 * @param skipIllegalChar
 * @return (value, status)
 * if success, status is 0.
 * if out of range, status is 1.
 * if cannot convert, status is -1.
 * if found illegal character, status is -2.
 */
inline std::pair<double, int> convertToDouble(const char *str, bool skipIllegalChar = false) {
  errno = 0;

  // convert to double
  if (std::isspace(*str)) {
    return {0, -1};
  }
  char *end;
  const double value = strtod_l(str, &end, Locale::C());
  int status = 0;

  // check error
  if (value == 0 && end == str) {
    status = -1;
  } else if (*end != '\0' && !skipIllegalChar) {
    status = -2;
  } else if (value == 0 && errno == ERANGE) {
    status = 1;
  } else if ((value == HUGE_VAL || value == -HUGE_VAL) && errno == ERANGE) {
    status = 1;
  }
  return {value, status};
}

constexpr bool isDecimal(char ch) { return ch >= '0' && ch <= '9'; }

constexpr bool isOctal(char ch) { return ch >= '0' && ch < '8'; }

constexpr bool isHex(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

/**
 * convert hex character to number
 * @param ch
 * @return
 */
constexpr unsigned int hexToNum(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  } else if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  } else if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return 0;
}

inline int64_t doubleToBits(double d) {
  union {
    int64_t i64;
    double f64;
  } data;

  /**
   * https://docs.oracle.com/javase/jp/8/docs/api/java/lang/Double.html#compare-double-double-
   */
  if (std::isnan(d)) {
    return 0x7ff8000000000000L;
  }
  if (std::isinf(d)) {
    return d > 0 ? 0x7ff0000000000000L : 0xfff0000000000000L;
  }
  data.f64 = d;
  return data.i64;
}

inline int compareByTotalOrder(double x, double y) {
  if (x < y) {
    return -1;
  }
  if (x > y) {
    return 1;
  }

  int64_t xx = doubleToBits(x);
  int64_t yy = doubleToBits(y);
  if (xx == yy) {
    return 0;
  }
  return xx < yy ? -1 : 1;
}

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_NUM_UTIL_HPP
