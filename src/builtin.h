/*
 * Copyright (C) 2015-2018 Nagisa Sekiguchi
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

#ifndef YDSH_BUILTIN_H
#define YDSH_BUILTIN_H

#include <fcntl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include "misc/files.h"
#include "misc/num_util.hpp"
#include "misc/word.hpp"
#include "signals.h"
#include "vm.h"
#include <ydsh/ydsh.h>

// helper macro
#define LOCAL(index) (ctx.getLocal(index))
#define EXTRACT_LOCAL(index) (ctx.moveLocal(index))
#define RET(value) return value
#define RET_BOOL(value) return DSValue::createBool(value)
#define RET_VOID return DSValue()
#define RET_ERROR return DSValue()

#define SUPPRESS_WARNING(a) (void)a

#define YDSH_METHOD static DSValue
#define YDSH_METHOD_DECL DSValue

/**
 *   //!bind: function <method name>($this : <receiver type>, $param1 : <type1>, $param2? : <type2>, ...) : <return type>
 *   //!bind: constructor <type name>($param1 : <type1>, ....)
 *   $<param name>?  has default value
 */

namespace ydsh {

using RuntimeContext = DSState;

// #################
// ##     Any     ##
// #################

//!bind: function $OP_STR($this : Any) : String
YDSH_METHOD to_str(RuntimeContext &ctx) {
  SUPPRESS_WARNING(to_str);
  StrBuilder builder(ctx);
  if (!LOCAL(0).opStr(builder)) {
    RET_ERROR;
  }
  RET(std::move(builder).take());
}

//!bind: function $OP_INTERP($this : Any) : String
YDSH_METHOD to_interp(RuntimeContext &ctx) {
  SUPPRESS_WARNING(to_interp);
  StrBuilder builder(ctx);
  if (!LOCAL(0).opInterp(builder)) {
    RET_ERROR;
  }
  RET(std::move(builder).take());
}

// ###################
// ##     Int     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Int) : Int
YDSH_METHOD int_plus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_plus);
  RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int) : Int
YDSH_METHOD int_minus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_minus);
  int64_t v = LOCAL(0).asInt();
  if (v == INT64_MIN) {
    raiseError(ctx, TYPE::ArithmeticError, "negative value of INT_MIN is not defined");
    RET_ERROR;
  }
  RET(DSValue::createInt(-v));
}

//!bind: function $OP_NOT($this : Int) : Int
YDSH_METHOD int_not(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_not);
  uint64_t v = ~static_cast<uint64_t>(LOCAL(0).asInt());
  RET(DSValue::createInt(static_cast<int64_t>(v)));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_add);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  int64_t ret;
  if (sadd_overflow(left, right, ret)) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(DSValue::createInt(ret));
}

//!bind: function $OP_SUB($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_sub(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_sub);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  int64_t ret;
  if (ssub_overflow(left, right, ret)) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(DSValue::createInt(ret));
}

//!bind: function $OP_MUL($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_mul(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_mul);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  int64_t ret;
  if (smul_overflow(left, right, ret)) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(DSValue::createInt(ret));
}

//!bind: function $OP_DIV($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_div(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_div);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  if (right == 0) {
    raiseError(ctx, TYPE::ArithmeticError, "zero division");
    RET_ERROR;
  }
  RET(DSValue::createInt(left / right));
}

//!bind: function $OP_MOD($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_mod(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_mod);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  if (right == 0) {
    raiseError(ctx, TYPE::ArithmeticError, "zero modulo");
    RET_ERROR;
  }
  RET(DSValue::createInt(left % right));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int, $target : Int) : Boolean
YDSH_METHOD int_2_int_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_eq);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : Int, $target : Int) : Boolean
YDSH_METHOD int_2_int_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_ne);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left != right);
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int, $target : Int) : Boolean
YDSH_METHOD int_2_int_lt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_lt);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : Int, $target : Int) : Boolean
YDSH_METHOD int_2_int_gt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_gt);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : Int, $target : Int) : Boolean
YDSH_METHOD int_2_int_le(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_le);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : Int, $target : Int) : Boolean
YDSH_METHOD int_2_int_ge(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_ge);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left >= right);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_and(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_and);
  auto left = static_cast<uint64_t>(LOCAL(0).asInt());
  auto right = static_cast<uint64_t>(LOCAL(1).asInt());
  RET(DSValue::createInt(left & right));
}

//!bind: function $OP_OR($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_or(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_or);
  auto left = static_cast<uint64_t>(LOCAL(0).asInt());
  auto right = static_cast<uint64_t>(LOCAL(1).asInt());
  RET(DSValue::createInt(left | right));
}

//!bind: function $OP_XOR($this : Int, $target : Int) : Int
YDSH_METHOD int_2_int_xor(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_xor);
  auto left = static_cast<uint64_t>(LOCAL(0).asInt());
  auto right = static_cast<uint64_t>(LOCAL(1).asInt());
  RET(DSValue::createInt(left ^ right));
}

//!bind: function abs($this : Int) : Int
YDSH_METHOD int_abs(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_abs);
  int64_t value = LOCAL(0).asInt();
  if (value == INT64_MIN) {
    raiseError(ctx, TYPE::ArithmeticError, "absolute value of INT_MIN is not defined");
    RET_ERROR;
  }
  RET(DSValue::createInt(std::abs(value)));
}

//!bind: function $OP_TO_FLOAT($this : Int) : Float
YDSH_METHOD int_toFloat(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_toFloat);
  int64_t v = LOCAL(0).asInt();
  auto d = static_cast<double>(v);
  RET(DSValue::createFloat(d));
}

// ###################
// ##     Float     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Float) : Float
YDSH_METHOD float_plus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_plus);
  RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Float) : Float
YDSH_METHOD float_minus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_minus);
  RET(DSValue::createFloat(-LOCAL(0).asFloat()));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_add);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET(DSValue::createFloat(left + right));
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_sub(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_sub);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET(DSValue::createFloat(left - right));
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_mul(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_mul);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET(DSValue::createFloat(left * right));
}

//!bind: function $OP_DIV($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_div(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_div);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  double value = left / right;
  RET(DSValue::createFloat(value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_eq);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_ne);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left != right);
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_lt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_lt);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_gt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_gt);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_le(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_le);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_ge(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_ge);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left >= right);
}

// =====  additional float op  ======

//!bind: function isNan($this : Float): Boolean
YDSH_METHOD float_isNan(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isNan);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isnan(value));
}

//!bind: function isInf($this : Float): Boolean
YDSH_METHOD float_isInf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isInf);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isinf(value));
}

//!bind: function isFinite($this : Float): Boolean
YDSH_METHOD float_isFinite(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isFinite);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isfinite(value));
}

//!bind: function isNormal($this : Float) : Boolean
YDSH_METHOD float_isNormal(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isNormal);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isnormal(value));
}

//!bind: function round($this : Float) : Float
YDSH_METHOD float_round(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_round);
  double value = LOCAL(0).asFloat();
  RET(DSValue::createFloat(std::round(value)));
}

//!bind: function trunc($this : Float) : Float
YDSH_METHOD float_trunc(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_trunc);
  double value = LOCAL(0).asFloat();
  RET(DSValue::createFloat(std::trunc(value)));
}

//!bind: function floor($this : Float) : Float
YDSH_METHOD float_floor(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_floor);
  double value = LOCAL(0).asFloat();
  RET(DSValue::createFloat(std::floor(value)));
}

//!bind: function ceil($this : Float) : Float
YDSH_METHOD float_ceil(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_ceil);
  double value = LOCAL(0).asFloat();
  RET(DSValue::createFloat(std::ceil(value)));
}

//!bind: function abs($this : Float) : Float
YDSH_METHOD float_abs(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_abs);
  double value = LOCAL(0).asFloat();
  RET(DSValue::createFloat(std::fabs(value)));
}

//!bind: function $OP_TO_INT($this : Float): Int
YDSH_METHOD float_toInt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_toInt);
  double d = LOCAL(0).asFloat();

  /**
   * convert double to int64 in the same way as java
   * see. (https://docs.oracle.com/javase/specs/jvms/se7/html/jvms-6.html#jvms-6.5.d2l)
   */
  int64_t v = 0;
  if (std::isnan(d)) { // if the value is NaN, convert to 0
    v = 0;
  } else if (!std::isinf(d) && d <= static_cast<double>(INT64_MAX) &&
             d >= static_cast<double>(INT64_MIN)) {
    v = static_cast<int64_t>(d);
  } else if (d < static_cast<double>(INT64_MIN)) {
    v = INT64_MIN;
  } else if (d > static_cast<double>(INT64_MAX)) {
    v = INT64_MAX;
  } else {
    fatal("unreachable\n");
  }
  RET(DSValue::createInt(v));
}

//!bind: function compare($this : Float, $target : Float) : Int
YDSH_METHOD float_compare(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_compare);
  double x = LOCAL(0).asFloat();
  double y = LOCAL(1).asFloat();
  int ret = compareByTotalOrder(x, y);
  RET(DSValue::createInt(ret));
}

// #####################
// ##     Boolean     ##
// #####################

//!bind: function $OP_NOT($this : Boolean) : Boolean
YDSH_METHOD boolean_not(RuntimeContext &ctx) {
  SUPPRESS_WARNING(boolean_not);
  RET_BOOL(!LOCAL(0).asBool());
}

//!bind: function $OP_EQ($this : Boolean, $target : Boolean) : Boolean
YDSH_METHOD boolean_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(boolean_eq);
  RET_BOOL(LOCAL(0).asBool() == LOCAL(1).asBool());
}

//!bind: function $OP_NE($this : Boolean, $target : Boolean) : Boolean
YDSH_METHOD boolean_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(boolean_ne);
  RET_BOOL(LOCAL(0).asBool() != LOCAL(1).asBool());
}

// ####################
// ##     String     ##
// ####################

//!bind: function $OP_EQ($this : String, $target : String) : Boolean
YDSH_METHOD string_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_eq);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : String, $target : String) : Boolean
YDSH_METHOD string_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_ne);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left != right);
}

//!bind: function $OP_LT($this : String, $target : String) : Boolean
YDSH_METHOD string_lt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_lt);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : String, $target : String) : Boolean
YDSH_METHOD string_gt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_gt);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : String, $target : String) : Boolean
YDSH_METHOD string_le(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_le);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : String, $target : String) : Boolean
YDSH_METHOD string_ge(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_ge);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left >= right);
}

//!bind: function size($this : String) : Int
YDSH_METHOD string_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_size);
  size_t size = LOCAL(0).asStrRef().size();
  assert(size <= StringObject::MAX_SIZE);
  RET(DSValue::createInt(size));
}

//!bind: function empty($this : String) : Boolean
YDSH_METHOD string_empty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_empty);
  bool empty = LOCAL(0).asStrRef().empty();
  RET_BOOL(empty);
}

//!bind: function count($this : String) : Int
YDSH_METHOD string_count(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_count);
  GraphemeScanner scanner(LOCAL(0).asStrRef());
  GraphemeScanner::Result ret;
  size_t count = 0;
  while (scanner.next(ret)) {
    count++;
  }
  assert(count <= StringObject::MAX_SIZE);
  RET(DSValue::createInt(count));
}

//!bind: function chars($this : String) : Array<String>
YDSH_METHOD string_chars(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_chars);
  auto ref = LOCAL(0).asStrRef();
  GraphemeScanner scanner(ref);
  GraphemeScanner::Result ret;
  auto value = DSValue::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(value);
  while (scanner.next(ret)) {
    array.append(DSValue::createStr(ret));
  }
  ASSERT_ARRAY_SIZE(array);
  RET(value);
}

//!bind: function words($this : String) : Array<String>
YDSH_METHOD string_words(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_words);
  auto ref = LOCAL(0).asStrRef();
  auto value = DSValue::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(value);
  Utf8WordStream stream(ref.begin(), ref.end());
  Utf8WordScanner scanner(stream);
  while (scanner.hasNext()) {
    ref = scanner.next();
    std::string word;
    const auto *end = ref.end();
    for (auto *iter = ref.begin(); iter != end;) {
      unsigned int size = UnicodeUtil::utf8ValidateChar(iter, end);
      if (size < 1) {
        word += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
        iter++;
      } else {
        word += StringRef(iter, size);
        iter += size;
      }
    }
    array.append(DSValue::createStr(std::move(word)));
  }
  ASSERT_ARRAY_SIZE(array);
  RET(value);
}

//!bind: function width($this : String, $eaw : Option<Int>) : Int
YDSH_METHOD string_width(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_width);
  auto ref = LOCAL(0).asStrRef();
  auto &v = LOCAL(1);
  GraphemeScanner scanner(ref);
  GraphemeScanner::Result ret;
  int64_t value = 0;

  // resolve east-asian width option
  auto n = v.isInvalid() ? 0 : v.asInt();
  auto eaw = n == 2                       ? UnicodeUtil::FULL_WIDTH
             : n == 1                     ? UnicodeUtil::HALF_WIDTH
             : UnicodeUtil::isCJKLocale() ? UnicodeUtil::AmbiguousCharWidth::FULL_WIDTH
                                          : UnicodeUtil::AmbiguousCharWidth::HALF_WIDTH;

  while (scanner.next(ret)) {
    int width = 0;
    for (unsigned int i = 0; i < ret.codePointCount; i++) {
      auto codePoint = ret.codePoints[i];
      codePoint = codePoint < 0 ? UnicodeUtil::REPLACEMENT_CHAR_CODE : codePoint;
      int w = UnicodeUtil::width(codePoint, eaw);
      if (w > 0) {
        width += w;
      }
    }
    value += (width <= 2 ? width : 2);
  }
  RET(DSValue::createInt(value));
}

/**
 * return always false.
 */
static void raiseOutOfRangeError(RuntimeContext &ctx, std::string &&message) {
  raiseError(ctx, TYPE::OutOfRangeError, std::move(message));
}

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __value = E;                                                                              \
    if (ctx.hasError()) {                                                                          \
      RET_ERROR;                                                                                   \
    }                                                                                              \
    std::forward<decltype(__value)>(__value);                                                      \
  })

struct ArrayIndex {
  size_t index;
  bool s;

  explicit operator bool() const { return this->s; }
};

// check index range and get resolved index
static ArrayIndex resolveIndex(int64_t index, size_t size, bool allowSize) {
  assert(ArrayObject::MAX_SIZE == StringObject::MAX_SIZE);
  assert(size <= ArrayObject::MAX_SIZE);
  index += (index < 0 ? size : 0);
  bool s = (index > -1 && static_cast<size_t>(index) < size) ||
           (allowSize && static_cast<size_t>(index) == size);
  return {static_cast<size_t>(index), s};
}

static ArrayIndex resolveIndex(RuntimeContext &ctx, int64_t index, size_t size,
                               bool allowSize = false) {
  auto ret = resolveIndex(index, size, allowSize);
  if (!ret) {
    std::string message("size is ");
    message += std::to_string(size);
    message += ", but index is ";
    message += std::to_string(index);
    raiseOutOfRangeError(ctx, std::move(message));
  }
  return ret;
}

//!bind: function $OP_GET($this : String, $index : Int) : String
YDSH_METHOD string_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_get);
  auto ref = LOCAL(0).asStrRef();
  const size_t size = ref.size();
  const auto index = LOCAL(1).asInt();

  auto value = TRY(resolveIndex(ctx, index, size));
  RET(DSValue::createStr(ref.substr(value.index, 1)));
}

//!bind: function charAt($this : String, $index : Int) : String
YDSH_METHOD string_charAt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_charAt);

  GraphemeScanner scanner(LOCAL(0).asStrRef());
  GraphemeScanner::Result ret;
  const auto pos = LOCAL(1).asInt();

  ssize_t count = 0;
  for (; scanner.next(ret); count++) {
    if (count == pos) {
      RET(DSValue::createStr(ret));
    }
  }
  std::string msg = "character count is ";
  msg += std::to_string(count);
  msg += ", but character position is ";
  msg += std::to_string(pos);
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

static DSValue sliceImpl(const ArrayObject &obj, size_t begin, size_t end) {
  auto b = obj.getValues().begin() + begin;
  auto e = obj.getValues().begin() + end;
  return DSValue::create<ArrayObject>(obj.getTypeID(), std::vector<DSValue>(b, e));
}

static DSValue sliceImpl(const StringRef &ref, size_t begin, size_t end) {
  return DSValue::createStr(ref.slice(begin, end));
}

/**
 *
 * @tparam T
 * @param obj
 * @param startIndex
 * inclusive
 * @param stopIndex
 * exclusive
 * @return
 */
template <typename T>
static auto slice(const T &obj, int64_t startIndex, int64_t stopIndex) {
  const int64_t size = ({
    uint64_t s = obj.size();
    assert(s <= INT64_MAX);
    static_cast<int64_t>(s);
  });

  // resolve actual index
  startIndex = (startIndex < 0 ? size : 0) + startIndex;
  if (startIndex < 0) {
    startIndex = 0;
  } else if (startIndex > size) {
    startIndex = size;
  }

  stopIndex = (stopIndex < 0 ? size : 0) + stopIndex;
  if (stopIndex < 0) {
    stopIndex = 0;
  } else if (stopIndex > size) {
    stopIndex = size;
  }
  if (stopIndex < startIndex) {
    stopIndex = startIndex;
  }
  return sliceImpl(obj, static_cast<uint64_t>(startIndex), static_cast<uint64_t>(stopIndex));
}

//!bind: function slice($this : String, $start : Int, $stop : Option<Int>) : String
YDSH_METHOD string_slice(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_slice);
  auto ref = LOCAL(0).asStrRef();
  auto start = LOCAL(1).asInt();
  auto &v = LOCAL(2);
  assert(ref.size() <= StringObject::MAX_SIZE);
  auto stop = v.isInvalid() ? static_cast<int64_t>(ref.size()) : v.asInt();
  RET(slice(ref, start, stop));
}

//!bind: function startsWith($this : String, $target : String) : Boolean
YDSH_METHOD string_startsWith(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_startsWith);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left.startsWith(right));
}

//!bind: function endsWith($this : String, $target : String) : Boolean
YDSH_METHOD string_endsWith(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_endsWith);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left.endsWith(right));
}

//!bind: function indexOf($this : String, $target : String, $index : Option<Int>) : Int
YDSH_METHOD string_indexOf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_indexOf);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  auto v = LOCAL(2);
  auto offset = TRY(resolveIndex(ctx, v.isInvalid() ? 0 : v.asInt(), left.size(), true));
  auto index = left.find(right, offset.index);
  assert(index == StringRef::npos || index <= StringObject::MAX_SIZE);
  auto actual = static_cast<ssize_t>(index); // for integer promotion
  RET(DSValue::createInt(static_cast<int64_t>(actual)));
}

//!bind: function lastIndexOf($this : String, $target : String) : Int
YDSH_METHOD string_lastIndexOf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_lastIndexOf);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  auto index = left.lastIndexOf(right);
  assert(index == StringRef::npos || index <= StringObject::MAX_SIZE);
  auto actual = static_cast<ssize_t>(index); // for integer promotion
  RET(DSValue::createInt(static_cast<int64_t>(actual)));
}

//!bind: function contains($this : String, $target : String) : Boolean
YDSH_METHOD string_contains(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_contains);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left.contains(right));
}

//!bind: function split($this : String, $delim : String) : Array<String>
YDSH_METHOD string_split(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_split);
  auto results = DSValue::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &ptr = typeAs<ArrayObject>(results);

  auto thisStr = LOCAL(0).asStrRef();
  auto delimStr = LOCAL(1).asStrRef();

  if (delimStr.empty()) {
    ptr.append(LOCAL(0));
  } else {
    for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
      auto ret = thisStr.find(delimStr, pos);
      ptr.append(DSValue::createStr(thisStr.slice(pos, ret)));
      pos = ret != StringRef::npos ? ret + delimStr.size() : ret;
    }
  }
  ASSERT_ARRAY_SIZE(ptr);
  RET(results);
}

//!bind: function replace($this : String, $target : String, $rep : String) : String
YDSH_METHOD string_replace(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_replace);

  auto delimStr = LOCAL(1).asStrRef();
  if (delimStr.empty()) {
    RET(LOCAL(0));
  }

  auto thisStr = LOCAL(0).asStrRef();
  auto repStr = LOCAL(2).asStrRef();
  auto buf = DSValue::createStr();

  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto ret = thisStr.find(delimStr, pos);
    auto value = thisStr.slice(pos, ret);
    if (!buf.appendAsStr(ctx, value)) {
      RET_ERROR;
    }
    if (ret != StringRef::npos) {
      if (!buf.appendAsStr(ctx, repStr)) {
        RET_ERROR;
      }
      pos = ret + delimStr.size();
    } else {
      pos = ret;
    }
  }
  RET(buf);
}

//!bind: function sanitize($this : String, $repl : Option<String>) : String
YDSH_METHOD string_sanitize(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_sanitize);
  auto ref = LOCAL(0).asStrRef();
  auto v = LOCAL(1);
  auto repl = v.isInvalid() ? "" : v.asStrRef();

  auto ret = DSValue::createStr();
  auto begin = ref.begin();
  auto old = begin;
  for (const auto end = ref.end(); begin != end;) {
    int codePoint = 0;
    unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(begin, end, codePoint);
    if (byteSize == 0 || codePoint == 0) { // replace invalid utf8 byte or nul char
      if (!ret.appendAsStr(ctx, StringRef(old, begin - old)) || !ret.appendAsStr(ctx, repl)) {
        RET_ERROR;
      }
      begin++;
      old = begin;
    } else {
      begin += byteSize;
    }
  }

  if (old == ref.begin()) {
    RET(LOCAL(0));
  } else {
    if (!ret.appendAsStr(ctx, StringRef(old, begin - old))) {
      RET_ERROR;
    }
    RET(ret);
  }
}

//!bind: function toInt($this : String, $radix : Option<Int>) : Option<Int>
YDSH_METHOD string_toInt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_toInt);
  auto ref = LOCAL(0).asStrRef();
  auto v = LOCAL(1);
  if (auto radix = v.isInvalid() ? 0 : v.asInt(); radix >= 0 && radix <= UINT32_MAX) {
    auto ret = convertToNum<int64_t>(ref.begin(), ref.end(), static_cast<unsigned int>(radix));
    if (ret.second) {
      RET(DSValue::createInt(ret.first));
    }
  }
  RET(DSValue::createInvalid());
}

//!bind: function toFloat($this : String) : Option<Float>
YDSH_METHOD string_toFloat(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_toFloat);
  auto ref = LOCAL(0).asStrRef();
  if (!ref.hasNullChar()) {
    if (auto ret = convertToDouble(ref.data(), false); ret.second == 0) {
      RET(DSValue::createFloat(ret.first));
    }
  }
  RET(DSValue::createInvalid());
}

static GraphemeScanner asGraphemeScanner(StringRef ref, const uint32_t (&values)[3]) {
  return GraphemeScanner(ref, values[0], values[1],
                         GraphemeBoundary(static_cast<GraphemeBoundary::BreakProperty>(values[2])));
}

static DSValue asDSValue(const GraphemeScanner &scanner) {
  return DSValue::createNumList(scanner.getPrevPos(), scanner.getCurPos(),
                                static_cast<uint32_t>(scanner.getBoundary().getState()));
}

//!bind: function $OP_ITER($this : String) : StringIter
YDSH_METHOD string_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_iter);

  /**
   * record StringIter {
   *      var ref : String
   *      var scanner : {
   *         prevPos : int32_t
   *         curPos : int32_t
   *         boundary : int32_t
   *      }
   * }
   *
   */
  auto value = DSValue::create<BaseObject>(ctx.typePool.get(TYPE::StringIter), 2);
  auto &obj = typeAs<BaseObject>(value);
  GraphemeScanner scanner(LOCAL(0).asStrRef());
  obj[0] = LOCAL(0);
  obj[1] = asDSValue(scanner);
  RET(value);
}

//!bind: function $OP_MATCH($this : String, $re : Regex) : Boolean
YDSH_METHOD string_match(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_match);
  auto str = LOCAL(0).asStrRef();
  auto &re = typeAs<RegexObject>(LOCAL(1));
  bool r = TRY(re.search(ctx, str));
  RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : String, $re : Regex) : Boolean
YDSH_METHOD string_unmatch(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_unmatch);
  auto str = LOCAL(0).asStrRef();
  auto &re = typeAs<RegexObject>(LOCAL(1));
  bool r = !TRY(re.search(ctx, str));
  RET_BOOL(r);
}

//!bind: function realpath($this : String) : Option<String>
YDSH_METHOD string_realpath(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_realpath);
  auto ref = LOCAL(0).asStrRef();
  std::string str = ref.toString();
  expandTilde(str);
  auto buf = getRealpath(str.c_str());
  if (buf == nullptr) {
    RET(DSValue::createInvalid());
  }
  RET(DSValue::createStr(buf.get()));
}

//!bind: function lower($this : String) : String
YDSH_METHOD string_lower(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_lower);
  auto ret = DSValue::createStr(LOCAL(0).asStrRef());
  auto ref = ret.asStrRef();
  std::transform(ref.begin(), ref.end(), const_cast<char *>(ref.begin()), ::tolower);
  RET(ret);
}

//!bind: function upper($this : String) : String
YDSH_METHOD string_upper(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_upper);
  auto ret = DSValue::createStr(LOCAL(0).asStrRef());
  auto ref = ret.asStrRef();
  std::transform(ref.begin(), ref.end(), const_cast<char *>(ref.begin()), ::toupper);
  RET(ret);
}

// ########################
// ##     StringIter     ##
// ########################

//!bind: function $OP_NEXT($this : StringIter) : String
YDSH_METHOD stringIter_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(stringIter_next);
  auto &iter = typeAs<BaseObject>(LOCAL(0));
  auto scanner = asGraphemeScanner(iter[0].asStrRef(), iter[1].asNumList());
  GraphemeScanner::Result ret;
  bool r = scanner.next(ret);
  (void)r;
  assert(r);
  iter[1] = asDSValue(scanner);
  RET(DSValue::createStr(ret));
}

//!bind: function $OP_HAS_NEXT($this : StringIter) : Boolean
YDSH_METHOD stringIter_hasNext(RuntimeContext &ctx) {
  SUPPRESS_WARNING(stringIter_hasNext);
  auto &obj = typeAs<BaseObject>(LOCAL(0));
  auto scanner = asGraphemeScanner(obj[0].asStrRef(), obj[1].asNumList());
  RET_BOOL(scanner.hasNext());
}

// ###################
// ##     Regex     ##
// ###################

//!bind: function $OP_INIT($this : Regex, $str : String, $flag : Option<String>) : Regex
YDSH_METHOD regex_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_init);
  auto pattern = LOCAL(1).asStrRef();
  auto v = LOCAL(2);
  auto flag = v.isInvalid() ? "" : v.asStrRef();
  std::string errorStr;
  auto re = PCRE::compile(pattern, flag, errorStr);
  if (!re) {
    raiseError(ctx, TYPE::RegexSyntaxError, std::move(errorStr));
    RET_ERROR;
  }
  RET(DSValue::create<RegexObject>(std::move(re)));
}

//!bind: function $OP_MATCH($this : Regex, $target : String) : Boolean
YDSH_METHOD regex_search(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_search);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto ref = LOCAL(1).asStrRef();
  bool r = TRY(re.search(ctx, ref));
  RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : Regex, $target : String) : Boolean
YDSH_METHOD regex_unmatch(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_unmatch);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto ref = LOCAL(1).asStrRef();
  bool r = !TRY(re.search(ctx, ref));
  RET_BOOL(r);
}

//!bind: function match($this: Regex, $target : String) : Array<Option<String>>
YDSH_METHOD regex_match(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_match);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto ref = LOCAL(1).asStrRef();

  auto ret = DSValue::create<ArrayObject>(
      *ctx.typePool
           .createArrayType(*ctx.typePool.createOptionType(ctx.typePool.get(TYPE::String)).take())
           .take());
  TRY(re.match(ctx, ref, &typeAs<ArrayObject>(ret)));
  RET(ret);
}

//!bind: function replace($this: Regex, $target : String, $repl : String) : String
YDSH_METHOD regex_replace(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_replace);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  std::string out;
  if (re.replace(LOCAL(1).asStrRef(), LOCAL(2).asStrRef(), out)) {
    assert(out.size() <= StringObject::MAX_SIZE);
    RET(DSValue::createStr(std::move(out)));
  } else {
    raiseError(ctx, TYPE::RegexMatchError, std::move(out));
    RET_ERROR;
  }
}

// ####################
// ##     Signal     ##
// ####################

//!bind: function name($this : Signal) : String
YDSH_METHOD signal_name(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_name);
  const char *name = getSignalName(LOCAL(0).asSig());
  assert(name != nullptr);
  RET(DSValue::createStr(name));
}

//!bind: function value($this : Signal) : Int
YDSH_METHOD signal_value(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_value);
  RET(DSValue::createInt(LOCAL(0).asSig()));
}

//!bind: function message($this : Signal) : String
YDSH_METHOD signal_message(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_message);
  const char *value = strsignal(LOCAL(0).asSig());
  RET(DSValue::createStr(value));
}

static bool checkPidLimit(int64_t value) {
  if (value <= std::numeric_limits<pid_t>::max()) {
    return true;
  }
  errno = ESRCH;
  return false;
}

//!bind: function kill($this : Signal, $pid : Int) : Void
YDSH_METHOD signal_kill(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_kill);
  int sigNum = LOCAL(0).asSig();
  int64_t pid = LOCAL(1).asInt();
  if (checkPidLimit(pid) && kill(static_cast<pid_t>(pid), sigNum) == 0) {
    ctx.jobTable.waitForAny();
    RET_VOID;
  }
  int num = errno;
  std::string str = "failed to send ";
  str += getSignalName(sigNum);
  str += " to pid(";
  str += std::to_string(static_cast<pid_t>(pid));
  str += ")";
  raiseSystemError(ctx, num, std::move(str));
  RET_ERROR;
}

//!bind: function trap($this: Signal, $handler: Option<Func<Void,[Signal]>>): Func<Void,[Signal]>
YDSH_METHOD signal_trap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_trap);
  int sigNum = LOCAL(0).asSig();
  auto value = LOCAL(1);
  ObjPtr<FuncObject> handler;
  if (!value.isInvalid()) {
    handler = toObjPtr<FuncObject>(value);
  }
  auto old = installSignalHandler(ctx, sigNum, std::move(handler));
  assert(old);
  RET(old);
}

//!bind: function $OP_EQ($this : Signal, $target : Signal) : Boolean
YDSH_METHOD signal_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_eq);
  RET_BOOL(LOCAL(0).asSig() == LOCAL(1).asSig());
}

//!bind: function $OP_NE($this : Signal, $target : Signal) : Boolean
YDSH_METHOD signal_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_ne);
  RET_BOOL(LOCAL(0).asSig() != LOCAL(1).asSig());
}

// #####################
// ##     Signals     ##
// #####################

//!bind: function $OP_GET($this : Signals, $key : String) : Signal
YDSH_METHOD signals_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signals_get);
  auto key = LOCAL(1).asStrRef();
  int sigNum = getSignalNum(key);
  if (sigNum < 0) {
    std::string msg = "undefined signal: ";
    msg += toPrintable(key);
    raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
    RET_ERROR;
  }
  RET(DSValue::createSig(sigNum));
}

//!bind: function get($this : Signals, $key : String) : Option<Signal>
YDSH_METHOD signals_signal(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signals_signal);
  auto key = LOCAL(1).asStrRef();
  int sigNum = getSignalNum(key);
  if (sigNum < 0) {
    RET(DSValue::createInvalid());
  }
  RET(DSValue::createSig(sigNum));
}

//!bind: function list($this : Signals) : Array<Signal>
YDSH_METHOD signals_list(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signals_list);

  auto ret = ctx.typePool.createArrayType(ctx.typePool.get(TYPE::Signal));
  assert(ret);
  auto type = std::move(ret).take();
  auto v = DSValue::create<ArrayObject>(*type);
  auto &array = typeAs<ArrayObject>(v);
  for (auto &e : getUniqueSignalList()) {
    array.append(DSValue::createSig(e));
  }
  ASSERT_ARRAY_SIZE(array);
  RET(v);
}

// ####################
// ##     Module     ##
// ####################

static bool checkModLayout(DSState &state, const DSValue &value) {
  if (value.isObject() && isa<FuncObject>(value.get())) {
    return true;
  }
  raiseError(state, TYPE::InvalidOperationError, "cannot call method on temporary module object");
  return false;
}

#define CHECK_MOD_LAYOUT(obj)                                                                      \
  do {                                                                                             \
    if (!checkModLayout(ctx, obj)) {                                                               \
      RET_ERROR;                                                                                   \
    }                                                                                              \
  } while (false)

//!bind: function _scriptName($this : Module) : String
YDSH_METHOD module_name(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_name);

  CHECK_MOD_LAYOUT(LOCAL(0));
  auto &obj = typeAs<FuncObject>(LOCAL(0));
  RET(obj.getCode().getConstPool()[CVAR_OFFSET_SCRIPT_NAME]);
}

//!bind: function _scriptDir($this : Module) : String
YDSH_METHOD module_dir(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_dir);

  CHECK_MOD_LAYOUT(LOCAL(0));
  auto &obj = typeAs<FuncObject>(LOCAL(0));
  RET(obj.getCode().getConstPool()[CVAR_OFFSET_SCRIPT_DIR]);
}

//!bind: function _func($this : Module, $expr : String) : Func<Option<Any>>
YDSH_METHOD module_func(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_func);

  if (!ctx.tempModScope.empty()) {
    raiseError(ctx, TYPE::InvalidOperationError,
               "cannot call method within user-defined completer");
    RET_ERROR;
  }
  assert(LOCAL(0).isObject());
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  auto ref = LOCAL(1).asStrRef();
  assert(type.isModType());
  auto &modType = cast<ModType>(type);
  auto ret = loadExprAsFunc(ctx, ref, modType);
  if (ret) {
    RET(DSValue(ret.asOk()));
  } else {
    ctx.throwObject(DSValue(ret.asErr()), 1);
    RET_ERROR;
  }
}

//!bind: function _fullname($this : Module, $name : String) : Option<String>
YDSH_METHOD module_fullname(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_fullname);

  CHECK_MOD_LAYOUT(LOCAL(0));
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  auto ref = LOCAL(1).asStrRef();
  assert(type.isModType());
  auto &modType = cast<ModType>(type);
  auto path = resolveFullCommandName(ctx, ref, modType);
  if (path.empty()) {
    RET(DSValue::createInvalid());
  } else {
    RET(DSValue::createStr(std::move(path)));
  }
}

// ###################
// ##     Array     ##
// ###################

//!bind: function $OP_GET($this : Array<T0>, $index : Int) : T0
YDSH_METHOD array_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_get);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, size));
  RET(obj.getValues()[ret.index]);
}

//!bind: function get($this : Array<T0>, $index : Int) : Option<T0>
YDSH_METHOD array_get2(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_get2);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = resolveIndex(index, size, false);
  if (!ret) {
    RET(DSValue::createInvalid());
  }
  RET(obj.getValues()[ret.index]);
}

//!bind: function $OP_SET($this : Array<T0>, $index : Int, $value : T0) : Void
YDSH_METHOD array_set(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_set);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, size));
  obj.refValues()[ret.index] = EXTRACT_LOCAL(2);
  RET_VOID;
}

//!bind: function remove($this : Array<T0>, $index : Int) : T0
YDSH_METHOD array_remove(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_remove);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, size));
  auto v = obj.getValues()[ret.index];
  obj.refValues().erase(obj.refValues().begin() + ret.index);
  RET(v);
}

static bool array_fetch(RuntimeContext &ctx, DSValue &value, bool fetchLast = true) {
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  if (obj.getValues().empty()) {
    raiseOutOfRangeError(ctx, std::string("Array size is 0"));
    return false;
  }
  value = fetchLast ? obj.getValues().back() : obj.getValues().front();
  return true;
}

//!bind: function peek($this : Array<T0>) : T0
YDSH_METHOD array_peek(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_peek);
  DSValue value;
  array_fetch(ctx, value);
  return value;
}

static bool array_insertImpl(DSState &ctx, int64_t index, const DSValue &v) {
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  if (size == ArrayObject::MAX_SIZE) {
    raiseOutOfRangeError(ctx, std::string("reach Array size limit"));
    return false;
  }

  auto ret = resolveIndex(ctx, index, size, true);
  if (!ret) {
    return false;
  }
  obj.refValues().insert(obj.refValues().begin() + ret.index, v);
  return true;
}

static bool array_pushImpl(RuntimeContext &ctx) {
  size_t index = typeAs<ArrayObject>(LOCAL(0)).getValues().size();
  return array_insertImpl(ctx, index, LOCAL(1));
}

//!bind: function push($this : Array<T0>, $value : T0) : Void
YDSH_METHOD array_push(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_push);
  TRY(array_pushImpl(ctx));
  RET_VOID;
}

//!bind: function pop($this : Array<T0>) : T0
YDSH_METHOD array_pop(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_pop);
  DSValue v;
  TRY(array_fetch(ctx, v));
  typeAs<ArrayObject>(LOCAL(0)).refValues().pop_back();
  RET(v);
}

//!bind: function shift($this : Array<T0>) : T0
YDSH_METHOD array_shift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_shift);
  DSValue v;
  TRY(array_fetch(ctx, v, false));
  auto &values = typeAs<ArrayObject>(LOCAL(0)).refValues();
  values.erase(values.begin());
  RET(v);
}

//!bind: function unshift($this : Array<T0>, $value : T0) : Void
YDSH_METHOD array_unshift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_unshift);
  TRY(array_insertImpl(ctx, 0, LOCAL(1)));
  RET_VOID;
}

//!bind: function insert($this : Array<T0>, $index : Int, $value : T0) : Void
YDSH_METHOD array_insert(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_insert);
  TRY(array_insertImpl(ctx, LOCAL(1).asInt(), LOCAL(2)));
  RET_VOID;
}

//!bind: function add($this : Array<T0>, $value : T0) : Array<T0>
YDSH_METHOD array_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_add);
  TRY(array_pushImpl(ctx));
  RET(LOCAL(0));
}

//!bind: function addAll($this : Array<T0>, $value : Array<T0>) : Array<T0>
YDSH_METHOD array_addAll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_addAll);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  auto &value = typeAs<ArrayObject>(LOCAL(1));
  if (&obj != &value) {
    size_t valueSize = value.getValues().size();
    for (size_t i = 0; i < valueSize; i++) {
      TRY(obj.append(ctx, DSValue(value.getValues()[i])));
    }
  }
  RET(LOCAL(0));
}

//!bind: function swap($this : Array<T0>, $index : Int, $value : T0) : T0
YDSH_METHOD array_swap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_swap);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, obj.getValues().size()));
  DSValue value = LOCAL(2);
  std::swap(obj.refValues()[ret.index], value);
  RET(value);
}

//!bind: function slice($this : Array<T0>, $from : Int, $to : Option<Int>) : Array<T0>
YDSH_METHOD array_slice(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_slice);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  auto start = LOCAL(1).asInt();
  auto &v = LOCAL(2);
  assert(obj.size() <= ArrayObject::MAX_SIZE);
  auto stop = v.isInvalid() ? static_cast<int64_t>(obj.size()) : v.asInt();
  RET(slice(obj, start, stop));
}

//!bind: function copy($this : Array<T0>) : Array<T0>
YDSH_METHOD array_copy(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_copy);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  std::vector<DSValue> values = obj.getValues();
  RET(DSValue::create<ArrayObject>(obj.getTypeID(), std::move(values)));
}

//!bind: function reverse($this : Array<T0>) : Array<T0>
YDSH_METHOD array_reverse(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_reverse);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  std::reverse(obj.refValues().begin(), obj.refValues().end());
  RET(LOCAL(0));
}

//!bind: function sort($this : Array<T0>) : Array<T0> where T0 : Value_
YDSH_METHOD array_sort(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_sort);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  std::sort(obj.refValues().begin(), obj.refValues().end(),
            [](const DSValue &x, const DSValue &y) { return x.compare(y) < 0; });
  RET(LOCAL(0));
}

//!bind: function sortWith($this : Array<T0>, $comp : Func<Boolean, [T0, T0]>) : Array<T0>
YDSH_METHOD array_sortWith(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_sortWith);
  auto &arrayObj = typeAs<ArrayObject>(LOCAL(0));
  auto &comp = LOCAL(1);
  if (mergeSort(ctx, arrayObj, comp)) {
    RET(LOCAL(0));
  } else {
    RET_ERROR;
  }
}

//!bind: function join($this : Array<T0>, $delim : Option<String>) : String
YDSH_METHOD array_join(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_join);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  auto v = LOCAL(1);
  auto delim = v.isInvalid() ? "" : v.asStrRef();

  StrBuilder builder(ctx);
  size_t count = 0;
  for (auto &e : obj.getValues()) {
    if (count++ > 0) {
      TRY(builder.add(delim));
    }
    TRY(e.opStr(builder));
  }
  RET(std::move(builder).take());
}

/*//!bind: function map($this : Array<T0>, $mapper : Func<T1,[T0]>) : Array<T1>
YDSH_METHOD array_map(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_map);  //FIXME: need to fix method handle instantiation bug
  auto &arrayObj = typeAs<ArrayObject>(LOCAL(0));
  auto &mapper = LOCAL(1);

  auto ret = ({
    auto &funcType = ctx.typePool.get(mapper.getTypeID());
    assert(funcType.isFuncType());
    auto newType = ctx.typePool.createArrayType(cast<FunctionType>(funcType).getReturnType());
    assert(newType);
    DSValue::create<ArrayObject>(*newType.asOk());
  });
  auto &retArray = typeAs<ArrayObject>(ret);
  size_t size = arrayObj.size();
  retArray.refValues().resize(size);
  for (size_t i = 0; i < size; i++) {
    auto e = arrayObj.getValues()[i];
    auto value = VM::callFunction(ctx, DSValue(mapper), makeArgs(std::move(e)));
    if (ctx.hasError()) {
      RET_ERROR;
    }
    retArray.refValues()[i] = std::move(value);
  }
  RET(ret);
}*/

//!bind: function indexOf($this : Array<T0>, $target : T0, $index : Option<Int>) : Int where T0 : Value_
YDSH_METHOD array_indexOf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_indexOf);
  auto &arrayObj = typeAs<ArrayObject>(LOCAL(0));
  auto &value = LOCAL(1);
  auto v = LOCAL(2);
  size_t size = arrayObj.size();
  assert(size <= ArrayObject::MAX_SIZE);
  auto offset = TRY(resolveIndex(ctx, v.isInvalid() ? 0 : v.asInt(), size, true));

  int64_t index = -1;
  for (size_t i = offset.index; i < size; i++) {
    if (arrayObj.getValues()[i].equals(value)) {
      index = static_cast<int64_t>(i);
      break;
    }
  }
  RET(DSValue::createInt(index));
}

//!bind: function lastIndexOf($this : Array<T0>, $target : T0) : Int where T0 : Value_
YDSH_METHOD array_lastIndexOf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_lastIndexOf);
  auto &arrayObj = typeAs<ArrayObject>(LOCAL(0));
  auto &value = LOCAL(1);
  size_t size = arrayObj.size();
  assert(size <= ArrayObject::MAX_SIZE);
  int64_t index = -1;
  for (int64_t i = static_cast<int64_t>(size) - 1; i > -1; i--) {
    if (arrayObj.getValues()[i].equals(value)) {
      index = i;
      break;
    }
  }
  RET(DSValue::createInt(index));
}

//!bind: function contains($this : Array<T0>, $target : T0) : Boolean where T0 : Value_
YDSH_METHOD array_contains(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_contains);
  auto &arrayObj = typeAs<ArrayObject>(LOCAL(0));
  auto &value = LOCAL(1);
  size_t size = arrayObj.size();
  assert(size <= ArrayObject::MAX_SIZE);
  bool found = false;
  for (size_t i = 0; i < size; i++) {
    if (arrayObj.getValues()[i].equals(value)) {
      found = true;
      break;
    }
  }
  RET_BOOL(found);
}

//!bind: function size($this : Array<T0>) : Int
YDSH_METHOD array_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_size);
  size_t size = typeAs<ArrayObject>(LOCAL(0)).getValues().size();
  assert(size <= ArrayObject::MAX_SIZE);
  RET(DSValue::createInt(size));
}

//!bind: function empty($this : Array<T0>) : Boolean
YDSH_METHOD array_empty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_empty);
  bool empty = typeAs<ArrayObject>(LOCAL(0)).getValues().empty();
  RET_BOOL(empty);
}

//!bind: function clear($this : Array<T0>) : Void
YDSH_METHOD array_clear(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_clear);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  obj.refValues().clear();
  RET_VOID;
}

//!bind: function $OP_ITER($this : Array<T0>) : Array<T0>
YDSH_METHOD array_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_iter);

  /**
   * record ArrayIter<T0> {
   *      var ref : Array<T0>
   *      var index : Int
   * }
   *
   * FIXME: object layout and type is mismatched
   */
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  auto value = DSValue::create<BaseObject>(type, 2);
  auto &obj = typeAs<BaseObject>(value);
  obj[0] = LOCAL(0);
  obj[1] = DSValue::createInt(0);
  RET(value);
}

//!bind: function $OP_NEXT($this : Array<T0>) : T0
YDSH_METHOD array_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_next);
  auto &iterObj = typeAs<BaseObject>(LOCAL(0));
  auto &obj = typeAs<ArrayObject>(iterObj[0]);
  assert(iterObj[1].asInt() > -1);
  size_t index = iterObj[1].asInt();
  assert(index < obj.size());
  auto value = obj.getValues()[index++];
  iterObj[1] = DSValue::createInt(index);
  RET(value);
}

//!bind: function $OP_HAS_NEXT($this : Array<T0>) : Boolean
YDSH_METHOD array_hasNext(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_hasNext);
  auto &iterObj = typeAs<BaseObject>(LOCAL(0));
  auto &obj = typeAs<ArrayObject>(iterObj[0]);
  assert(iterObj[1].asInt() > -1);
  size_t index = iterObj[1].asInt();
  RET_BOOL(index < obj.size());
}

// #################
// ##     Map     ##
// #################

static void raiseIterInvalid(DSState &st) {
  std::string msg = "cannot modify map object during iteration";
  raiseError(st, TYPE::InvalidOperationError, std::move(msg));
}

#define CHECK_ITER_INVALIDATION(obj)                                                               \
  do {                                                                                             \
    if (obj.locked()) {                                                                            \
      raiseIterInvalid(ctx);                                                                       \
      RET_ERROR;                                                                                   \
    }                                                                                              \
  } while (false)

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
YDSH_METHOD map_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_get);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  auto iter = obj.getValueMap().find(LOCAL(1));
  if (iter == obj.getValueMap().end()) {
    std::string msg("not found key: ");
    msg += LOCAL(1).toString();
    raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
    RET_ERROR;
  }
  RET(iter->second);
}

//!bind: function $OP_SET($this : Map<T0, T1>, $key : T0, $value : T1) : Void
YDSH_METHOD map_set(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_set);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  obj.set(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
  RET_VOID;
}

//!bind: function put($this : Map<T0, T1>, $key : T0, $value : T1) : Option<T1>
YDSH_METHOD map_put(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_put);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto v = obj.set(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
  RET(v);
}

//!bind: function putIfAbsent($this : Map<T0, T1>, $key : T0, $value : T1) : T1
YDSH_METHOD map_putIfAbsent(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_putIfAbsent);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto v = obj.setIfNotFound(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
  RET(v);
}

//!bind: function size($this : Map<T0, T1>) : Int
YDSH_METHOD map_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_size);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  size_t value = obj.getValueMap().size();
  RET(DSValue::createInt(value));
}

//!bind: function empty($this : Map<T0, T1>) : Boolean
YDSH_METHOD map_empty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_empty);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  bool value = obj.getValueMap().empty();
  RET_BOOL(value);
}

//!bind: function get($this : Map<T0, T1>, $key : T0) : Option<T1>
YDSH_METHOD map_find(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_find);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  auto iter = obj.getValueMap().find(LOCAL(1));
  RET(iter != obj.getValueMap().end() ? iter->second : DSValue::createInvalid());
}

//!bind: function remove($this : Map<T0, T1>, $key : T0) : Boolean
YDSH_METHOD map_remove(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_remove);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  bool r = obj.remove(LOCAL(1));
  RET_BOOL(r);
}

//!bind: function swap($this : Map<T0, T1>, $key : T0, $value : T1) : T1
YDSH_METHOD map_swap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_swap);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  DSValue value = LOCAL(2);
  if (!obj.trySwap(LOCAL(1), value)) {
    std::string msg("not found key: ");
    msg += LOCAL(1).toString();
    raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
    RET_ERROR;
  }
  RET(value);
}

//!bind: function addAll($this : Map<T0, T1>, $value : Map<T0, T1>) : Map<T0, T1>
YDSH_METHOD map_addAll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_addAll);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  auto &value = typeAs<MapObject>(LOCAL(1));
  if (&obj != &value) {
    CHECK_ITER_INVALIDATION(obj);
    for (auto &e : value.getValueMap()) {
      obj.set(DSValue(e.first), DSValue(e.second));
    }
  }
  RET(LOCAL(0));
}

//!bind: function copy($this : Map<T0, T1>) : Map<T0, T1>
YDSH_METHOD map_copy(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_copy);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  HashMap map(obj.getValueMap());
  RET(DSValue::create<MapObject>(obj.getTypeID(), std::move(map)));
}

//!bind: function clear($this : Map<T0, T1>) : Void
YDSH_METHOD map_clear(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_clear);
  auto &obj = typeAs<MapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  obj.clear();
  RET_VOID;
}

//!bind: function $OP_ITER($this : Map<T0, T1>) : Map<T0, T1>
YDSH_METHOD map_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_iter);
  RET(typeAs<MapObject>(LOCAL(0)).iter());
}

//!bind: function $OP_NEXT($this : Map<T0, T1>) : Tuple<T0, T1>
YDSH_METHOD map_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_next);
  auto &obj = typeAs<MapIterObject>(LOCAL(0));
  assert(obj.hasNext());
  RET(obj.next(ctx.typePool));
}

//!bind: function $OP_HAS_NEXT($this : Map<T0, T1>) : Boolean
YDSH_METHOD map_hasNext(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_hasNext);
  RET_BOOL(typeAs<MapIterObject>(LOCAL(0)).hasNext());
}

// ###################
// ##     Error     ##
// ###################

//!bind: function $OP_INIT($this : Error, $message : String) : Error
YDSH_METHOD error_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_init);
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  RET(DSValue(ErrorObject::newError(ctx, type, LOCAL(1))));
}

//!bind: function message($this : Error) : String
YDSH_METHOD error_message(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_message);
  RET(typeAs<ErrorObject>(LOCAL(0)).getMessage());
}

//!bind: function show($this : Error) : Void
YDSH_METHOD error_show(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_show);
  typeAs<ErrorObject>(LOCAL(0)).printStackTrace(ctx);
  RET_VOID;
}

//!bind: function name($this : Error) : String
YDSH_METHOD error_name(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_name);
  RET(typeAs<ErrorObject>(LOCAL(0)).getName());
}

//!bind: function lineno($this : Error) : Int
YDSH_METHOD error_lineno(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_lineno);
  auto &stackTraces = typeAs<ErrorObject>(LOCAL(0)).getStackTrace();
  unsigned int lineNum = getOccurredLineNum(stackTraces);
  RET(DSValue::createInt(lineNum));
}

//!bind: function source($this : Error) : String
YDSH_METHOD error_source(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_source);
  auto &stackTraces = typeAs<ErrorObject>(LOCAL(0)).getStackTrace();
  const char *source = getOccurredSourceName(stackTraces);
  RET(DSValue::createStr(source));
}

// ####################
// ##     UnixFD     ##
// ####################

//!bind: function $OP_INIT($this : UnixFD, $path : String) : UnixFD
YDSH_METHOD fd_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_init);
  auto ref = LOCAL(1).asStrRef();
  errno = EINVAL;
  if (!ref.hasNullChar()) {
    errno = 0;
    int fd = open(ref.data(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (fd != -1) {
      RET(DSValue::create<UnixFdObject>(fd));
    }
  }
  int e = errno;
  std::string msg = "open failed: ";
  msg += toPrintable(ref);
  raiseSystemError(ctx, e, std::move(msg));
  RET_ERROR;
}

//!bind: function close($this : UnixFD) : Void
YDSH_METHOD fd_close(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_close);
  auto &fdObj = typeAs<UnixFdObject>(LOCAL(0));
  int fd = fdObj.getValue();
  if (fdObj.tryToClose(true) < 0) {
    int e = errno;
    raiseSystemError(ctx, e, std::to_string(fd));
    RET_ERROR;
  }
  RET_VOID;
}

//!bind: function dup($this : UnixFD) : UnixFD
YDSH_METHOD fd_dup(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_dup);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getValue();
  int newfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if (newfd < 0) {
    int e = errno;
    raiseSystemError(ctx, e, std::to_string(fd));
    RET_ERROR;
  }
  RET(DSValue::create<UnixFdObject>(newfd));
}

//!bind: function $OP_BOOL($this : UnixFD) : Boolean
YDSH_METHOD fd_bool(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_bool);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getValue();
  RET_BOOL(fd != -1);
}

//!bind: function $OP_NOT($this : UnixFD) : Boolean
YDSH_METHOD fd_not(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_not);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getValue();
  RET_BOOL(fd == -1);
}

//!bind: function $OP_ITER($this : UnixFD) : Reader
YDSH_METHOD fd_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_iter);
  auto &v = LOCAL(0);
  RET(DSValue::create<ReaderObject>(toObjPtr<UnixFdObject>(v)));
}

// ####################
// ##     Reader     ##
// ####################

//!bind: function $OP_NEXT($this : Reader) : String
YDSH_METHOD reader_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(reader_next);
  auto &reader = typeAs<ReaderObject>(LOCAL(0));
  RET(reader.takeLine());
}

//!bind: function $OP_HAS_NEXT($this : Reader) : Boolean
YDSH_METHOD reader_hasNext(RuntimeContext &ctx) {
  SUPPRESS_WARNING(reader_hasNext);
  auto &reader = typeAs<ReaderObject>(LOCAL(0));
  RET_BOOL(reader.nextLine());
}

// #################
// ##     Job     ##
// #################

//!bind: function in($this : Job) : UnixFD
YDSH_METHOD job_in(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_in);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  RET(obj.getInObj());
}

//!bind: function out($this : Job) : UnixFD
YDSH_METHOD job_out(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_out);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  RET(obj.getOutObj());
}

//!bind: function $OP_GET($this : Job, $index : Int) : UnixFD
YDSH_METHOD job_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_get);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  auto index = LOCAL(1).asInt();
  if (index == 0) {
    RET(obj.getInObj());
  }
  if (index == 1) {
    RET(obj.getOutObj());
  }
  std::string msg = "invalid fd number";
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

//!bind: function poll($this : Job) : Boolean
YDSH_METHOD job_poll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_poll);
  auto job = toObjPtr<JobObject>(LOCAL(0));
  ctx.jobTable.waitForJob(job, WaitOp::NONBLOCKING);
  RET_BOOL(job->isRunning());
}

//!bind: function wait($this : Job) : Int
YDSH_METHOD job_wait(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_wait);
  auto job = toObjPtr<JobObject>(LOCAL(0));
  int s =
      ctx.jobTable.waitForJob(job, ctx.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING);
  int errNum = errno;
  ctx.jobTable.waitForAny();
  if (errNum != 0) {
    raiseSystemError(ctx, errNum, "wait failed");
    RET_ERROR;
  }
  RET(DSValue::createInt(s));
}

//!bind: function raise($this : Job, $s : Signal) : Void
YDSH_METHOD job_raise(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_raise);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  obj.send(LOCAL(1).asSig());
  ctx.jobTable.waitForAny(); // update state of killed processes
  RET_VOID;
}

//!bind: function detach($this : Job) : Void
YDSH_METHOD job_detach(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_detach);
  auto job = toObjPtr<JobObject>(LOCAL(0));
  job->disown();
  RET_VOID;
}

//!bind: function size($this : Job) : Int
YDSH_METHOD job_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_size);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  RET(DSValue::createInt(obj.getProcSize()));
}

//!bind: function pid($this : Job, $index : Int) : Option<Int>
YDSH_METHOD job_pid(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_pid);
  auto &job = typeAs<JobObject>(LOCAL(0));
  auto index = LOCAL(1).asInt();

  if (index > -1 && static_cast<size_t>(index) < job.getProcSize()) {
    int pid = job.getValidPid(index);
    if (pid < 0 || !job.isRunning()) {
      RET(DSValue::createInvalid());
    }
    RET(DSValue::createInt(pid));
  }
  std::string msg = "number of processes is: ";
  msg += std::to_string(job.getProcSize());
  msg += ", but index is: ";
  msg += std::to_string(index);
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

//!bind: function status($this : Job, $index : Int) : Option<Int>
YDSH_METHOD job_status(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_status);
  auto &job = typeAs<JobObject>(LOCAL(0));
  auto index = LOCAL(1).asInt();

  if (index > -1 && static_cast<size_t>(index) < job.getProcSize()) {
    auto &proc = job.getProcs()[index];
    if (!proc.is(Proc::State::RUNNING)) {
      RET(DSValue::createInt(proc.exitStatus()));
    } else {
      RET(DSValue::createInvalid());
    }
  }
  std::string msg = "number of processes is: ";
  msg += std::to_string(job.getProcSize());
  msg += ", but index is: ";
  msg += std::to_string(index);
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

} // namespace ydsh

#endif // YDSH_BUILTIN_H
