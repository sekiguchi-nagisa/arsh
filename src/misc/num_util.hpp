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

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "detect.hpp"
#include "locale.hpp"

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
      default:
        break;
      }
    }
    return 8;
  }
  return 10;
}

template <typename T>
inline auto dropSign(T v) {
  static_assert(std::is_signed_v<T>, "must be signed type");
  using UT = std::make_unsigned_t<T>;
  return static_cast<UT>(~static_cast<UT>(v) + 1);
}

template <typename UT>
inline auto putSign(UT v) {
  static_assert(std::is_unsigned_v<UT>, "must be unsigned type");
  using T = std::make_signed_t<UT>;
  return static_cast<T>(~v + 1);
}

enum class IntConversionStatus : unsigned char {
  OK,
  ILLEGAL_CHAR,
  ILLEGAL_RADIX,
  RADIX_OVERFLOW,
  OUT_OF_RANGE,
};

template <typename T>
struct IntConversionResult {
  static_assert(std::is_integral_v<T> &&
                (sizeof(T) == sizeof(int32_t) || sizeof(T) == sizeof(int64_t)));

  IntConversionStatus kind;

  unsigned int consumedSize;

  T value;

  explicit operator bool() const { return this->kind == IntConversionStatus::OK; }
};

template <typename U>
inline std::pair<std::make_signed_t<U>, bool> makeSigned(U v, bool negate) {
  static_assert(std::is_unsigned_v<U>, "must be unsigned type");

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
          enable_when<std::is_unsigned_v<T> &&
                      (sizeof(T) == sizeof(int32_t) || sizeof(T) == sizeof(int64_t))> = nullptr>
inline IntConversionResult<T> parseInteger(const char *&begin, const char *end, unsigned int base) {
  const auto oldBegin = begin;

  if (begin != end && *begin == '+') {
    ++begin;
  }

  if (base == 0) {
    base = parseBase(begin, end);
  }

  if (begin == end) {
    return {
        .kind = IntConversionStatus::ILLEGAL_CHAR,
        .consumedSize = static_cast<unsigned int>(begin - oldBegin),
        .value = 0,
    };
  }
  if (base < 2 || base > 36) {
    return {
        .kind = IntConversionStatus::ILLEGAL_RADIX,
        .consumedSize = static_cast<unsigned int>(begin - oldBegin),
        .value = 0,
    };
  }

  T radix = static_cast<T>(base);
  IntConversionResult<T> ret = {
      .kind = IntConversionStatus::OK,
      .consumedSize = 0,
      .value = 0,
  };
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
      ret.kind = IntConversionStatus::ILLEGAL_CHAR;
      break;
    }

    if (v >= radix) {
      ret.kind = IntConversionStatus::RADIX_OVERFLOW;
      break;
    }

    if (mul_overflow(ret.value, radix, ret.value) || // ret = ret * radix
        add_overflow(ret.value, v, ret.value)) {     // ret = ret + v
      ret.kind = IntConversionStatus::OUT_OF_RANGE;
      break;
    }
  } while (++begin != end);

  ret.consumedSize = static_cast<unsigned int>(begin - oldBegin);
  return ret;
}

template <typename T, enable_when<std::is_signed_v<T> && (sizeof(T) == sizeof(int32_t) ||
                                                          sizeof(T) == sizeof(int64_t))> = nullptr>
inline IntConversionResult<T> parseInteger(const char *&begin, const char *end, unsigned int base) {
  const auto oldBegin = begin;
  bool sign = false;
  if (begin != end && *begin == '-' && *(begin + 1) != '+') {
    sign = true;
    ++begin;
  }

  using UT = std::make_unsigned_t<T>;
  auto ret = parseInteger<UT>(begin, end, base);
  IntConversionResult<T> ret2 = {
      .kind = ret.kind,
      .consumedSize = static_cast<unsigned int>(begin - oldBegin),
      .value = /*ret ? makeSigned(ret.value, sign) :*/ static_cast<T>(ret.value),
  };
  if (ret) {
    auto pair = makeSigned(ret.value, sign);
    ret2.value = pair.first;
    ret2.kind = pair.second ? IntConversionStatus::OK : IntConversionStatus::OUT_OF_RANGE;
  }
  return ret2;
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
template <typename T, enable_when<std::is_integral_v<T>> = nullptr>
inline IntConversionResult<T> convertToNum(const char *begin, const char *end, unsigned int base) {
  return parseInteger<T>(begin, end, base);
}

template <typename T, enable_when<std::is_integral_v<T>> = nullptr>
inline IntConversionResult<T> convertToDecimal(const char *begin, const char *end) {
  return convertToNum<T>(begin, end, 10);
}

template <typename T, enable_when<std::is_integral_v<T>> = nullptr>
inline IntConversionResult<T> convertToDecimal(const char *str) {
  return convertToDecimal<T>(str, str + strlen(str));
}

struct DoubleConversionResult {
  enum Kind : unsigned char {
    OK,
    ILLEGAL_CHAR,
    OUT_OF_RANGE,
  } kind;

  unsigned int consumedSize;

  double value;

  explicit operator bool() const { return this->kind == OK; }
};

/**
 * parse double with locale independent way
 * @param str
 * @param skipIllegalChar
 * @return
 */
inline DoubleConversionResult convertToDouble(const char *str, bool skipIllegalChar = false) {
  errno = 0;

  // convert to double
  switch (*str) {
  case ' ':
  case '\t':
  case '\n':
  case '\r':
  case '\f':
  case '\v':
    return {
        .kind = DoubleConversionResult::ILLEGAL_CHAR,
        .consumedSize = 0,
        .value = 0,
    };
  default:
    break;
  }
  char *end;
  DoubleConversionResult ret = {
      .kind = DoubleConversionResult::OK,
      .consumedSize = 0,
      .value = strtod_l(str, &end, POSIX_LOCALE_C.get()),
  };

  // check error
  if ((ret.value == 0 && end == str) || (*end != '\0' && !skipIllegalChar)) {
    ret.kind = DoubleConversionResult::ILLEGAL_CHAR;
  } else if ((ret.value == 0 || ret.value == HUGE_VAL || ret.value == -HUGE_VAL) &&
             errno == ERANGE) {
    ret.kind = DoubleConversionResult::OUT_OF_RANGE;
  }
  ret.consumedSize = end - str;
  return ret;
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
