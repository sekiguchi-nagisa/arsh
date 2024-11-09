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

#ifndef ARSH_BUILTIN_H
#define ARSH_BUILTIN_H

#include <sys/file.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include "arg_parser.h"
#include "candidates.h"
#include "case_fold.h"
#include "line_editor.h"
#include "misc/files.hpp"
#include "misc/num_util.hpp"
#include "misc/word.hpp"
#include "ordered_map.h"
#include "signals.h"
#include "vm.h"

#include <arsh/arsh.h>

// helper macro
#define LOCAL(index) (ctx.getLocal(index))
#define EXTRACT_LOCAL(index) (ctx.moveLocal(index))
#define RET(value) return value
#define RET_BOOL(value) return Value::createBool(value)
#define RET_VOID return Value()
#define RET_ERROR return Value()

#define SUPPRESS_WARNING(a) (void)a

#define ARSH_METHOD static Value
#define ARSH_METHOD_DECL static Value

/**
 *   //!bind: function <method name>($this : <receiver type>, $param1 : <type1>, $param2? : <type2>, ...) : <return type>
 *   //!bind: constructor <type name>($param1 : <type1>, ....)
 *   $<param name>?  has default value
 */

namespace arsh {

using RuntimeContext = ARState;

// #################
// ##     Any     ##
// #################

//!bind: function $OP_STR($this : Any) : String
ARSH_METHOD to_str(RuntimeContext &ctx) {
  SUPPRESS_WARNING(to_str);
  StrBuilder builder(ctx);
  if (!LOCAL(0).opStr(builder)) {
    RET_ERROR;
  }
  RET(std::move(builder).take());
}

//!bind: function $OP_INTERP($this : Any) : String
ARSH_METHOD to_interp(RuntimeContext &ctx) {
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
ARSH_METHOD int_plus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_plus);
  RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int) : Int
ARSH_METHOD int_minus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_minus);
  int64_t v = LOCAL(0).asInt();
  if (v == INT64_MIN) {
    raiseError(ctx, TYPE::ArithmeticError, "negative value of INT_MIN is not defined");
    RET_ERROR;
  }
  RET(Value::createInt(-v));
}

//!bind: function $OP_NOT($this : Int) : Int
ARSH_METHOD int_not(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_not);
  uint64_t v = ~static_cast<uint64_t>(LOCAL(0).asInt());
  RET(Value::createInt(static_cast<int64_t>(v)));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_add);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  int64_t ret;
  if (unlikely(sadd_overflow(left, right, ret))) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(Value::createInt(ret));
}

//!bind: function $OP_SUB($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_sub(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_sub);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  int64_t ret;
  if (unlikely(ssub_overflow(left, right, ret))) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(Value::createInt(ret));
}

//!bind: function $OP_MUL($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_mul(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_mul);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  int64_t ret;
  if (unlikely(smul_overflow(left, right, ret))) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(Value::createInt(ret));
}

//!bind: function $OP_DIV($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_div(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_div);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  if (right == 0) {
    raiseError(ctx, TYPE::ArithmeticError, "zero division");
    RET_ERROR;
  } else if (left == INT64_MIN && right == -1) {
    raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
    RET_ERROR;
  }
  RET(Value::createInt(left / right));
}

//!bind: function $OP_MOD($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_mod(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_mod);
  int64_t left = LOCAL(0).asInt();
  int64_t right = LOCAL(1).asInt();

  if (unlikely(right == 0)) {
    raiseError(ctx, TYPE::ArithmeticError, "zero modulo");
    RET_ERROR;
  }
  RET(Value::createInt(left % right));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int, $target : Int) : Bool
ARSH_METHOD int_2_int_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_eq);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : Int, $target : Int) : Bool
ARSH_METHOD int_2_int_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_ne);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left != right);
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int, $target : Int) : Bool
ARSH_METHOD int_2_int_lt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_lt);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : Int, $target : Int) : Bool
ARSH_METHOD int_2_int_gt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_gt);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : Int, $target : Int) : Bool
ARSH_METHOD int_2_int_le(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_le);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : Int, $target : Int) : Bool
ARSH_METHOD int_2_int_ge(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_ge);
  auto left = LOCAL(0).asInt();
  auto right = LOCAL(1).asInt();
  RET_BOOL(left >= right);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_and(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_and);
  auto left = static_cast<uint64_t>(LOCAL(0).asInt());
  auto right = static_cast<uint64_t>(LOCAL(1).asInt());
  RET(Value::createInt(left & right));
}

//!bind: function $OP_OR($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_or(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_or);
  auto left = static_cast<uint64_t>(LOCAL(0).asInt());
  auto right = static_cast<uint64_t>(LOCAL(1).asInt());
  RET(Value::createInt(left | right));
}

//!bind: function $OP_XOR($this : Int, $target : Int) : Int
ARSH_METHOD int_2_int_xor(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_xor);
  auto left = static_cast<uint64_t>(LOCAL(0).asInt());
  auto right = static_cast<uint64_t>(LOCAL(1).asInt());
  RET(Value::createInt(left ^ right));
}

//!bind: function $OP_LSHIFT($this: Int, $target: Int): Int
ARSH_METHOD int_2_int_lshift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_lshift);
  const auto left = LOCAL(0).asInt();
  const auto right = LOCAL(1).asInt();
  RET(Value::createInt(leftShift(left, right)));
}

//!bind: function $OP_RSHIFT($this: Int, $target: Int): Int
ARSH_METHOD int_2_int_rshift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_rshift);
  const auto left = LOCAL(0).asInt();
  const auto right = LOCAL(1).asInt();
  RET(Value::createInt(rightShift(left, right)));
}

//!bind: function $OP_URSHIFT($this: Int, $target: Int): Int
ARSH_METHOD int_2_int_urshift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_2_int_urshift);
  const auto left = LOCAL(0).asInt();
  const auto right = LOCAL(1).asInt();
  RET(Value::createInt(unsignedRightShift(left, right)));
}

//!bind: function abs($this : Int) : Int
ARSH_METHOD int_abs(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_abs);
  int64_t value = LOCAL(0).asInt();
  if (unlikely(value == INT64_MIN)) {
    raiseError(ctx, TYPE::ArithmeticError, "absolute value of INT_MIN is not defined");
    RET_ERROR;
  }
  RET(Value::createInt(std::abs(value)));
}

//!bind: function $OP_TO_FLOAT($this : Int) : Float
ARSH_METHOD int_toFloat(RuntimeContext &ctx) {
  SUPPRESS_WARNING(int_toFloat);
  int64_t v = LOCAL(0).asInt();
  auto d = static_cast<double>(v);
  RET(Value::createFloat(d));
}

// ###################
// ##     Float     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Float) : Float
ARSH_METHOD float_plus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_plus);
  RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Float) : Float
ARSH_METHOD float_minus(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_minus);
  RET(Value::createFloat(-LOCAL(0).asFloat()));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
ARSH_METHOD float_2_float_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_add);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET(Value::createFloat(left + right));
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
ARSH_METHOD float_2_float_sub(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_sub);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET(Value::createFloat(left - right));
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
ARSH_METHOD float_2_float_mul(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_mul);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET(Value::createFloat(left * right));
}

//!bind: function $OP_DIV($this : Float, $target : Float) : Float
ARSH_METHOD float_2_float_div(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_div);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  double value = left / right;
  RET(Value::createFloat(value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Bool
ARSH_METHOD float_2_float_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_eq);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : Float, $target : Float) : Bool
ARSH_METHOD float_2_float_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_ne);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left != right);
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Bool
ARSH_METHOD float_2_float_lt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_lt);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : Float, $target : Float) : Bool
ARSH_METHOD float_2_float_gt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_gt);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : Float, $target : Float) : Bool
ARSH_METHOD float_2_float_le(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_le);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : Float, $target : Float) : Bool
ARSH_METHOD float_2_float_ge(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_2_float_ge);
  double left = LOCAL(0).asFloat();
  double right = LOCAL(1).asFloat();
  RET_BOOL(left >= right);
}

// =====  additional float op  ======

//!bind: function isNaN($this : Float): Bool
ARSH_METHOD float_isNaN(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isNaN);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isnan(value));
}

//!bind: function isInf($this : Float): Bool
ARSH_METHOD float_isInf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isInf);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isinf(value));
}

//!bind: function isFinite($this : Float): Bool
ARSH_METHOD float_isFinite(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isFinite);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isfinite(value));
}

//!bind: function isNormal($this : Float) : Bool
ARSH_METHOD float_isNormal(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_isNormal);
  double value = LOCAL(0).asFloat();
  RET_BOOL(std::isnormal(value));
}

//!bind: function round($this : Float) : Float
ARSH_METHOD float_round(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_round);
  double value = LOCAL(0).asFloat();
  RET(Value::createFloat(std::round(value)));
}

//!bind: function trunc($this : Float) : Float
ARSH_METHOD float_trunc(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_trunc);
  double value = LOCAL(0).asFloat();
  RET(Value::createFloat(std::trunc(value)));
}

//!bind: function floor($this : Float) : Float
ARSH_METHOD float_floor(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_floor);
  double value = LOCAL(0).asFloat();
  RET(Value::createFloat(std::floor(value)));
}

//!bind: function ceil($this : Float) : Float
ARSH_METHOD float_ceil(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_ceil);
  double value = LOCAL(0).asFloat();
  RET(Value::createFloat(std::ceil(value)));
}

//!bind: function abs($this : Float) : Float
ARSH_METHOD float_abs(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_abs);
  double value = LOCAL(0).asFloat();
  RET(Value::createFloat(std::fabs(value)));
}

//!bind: function $OP_TO_INT($this : Float): Int
ARSH_METHOD float_toInt(RuntimeContext &ctx) {
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
  RET(Value::createInt(v));
}

//!bind: function compare($this : Float, $target : Float) : Int
ARSH_METHOD float_compare(RuntimeContext &ctx) {
  SUPPRESS_WARNING(float_compare);
  double x = LOCAL(0).asFloat();
  double y = LOCAL(1).asFloat();
  int ret = compareByTotalOrder(x, y);
  RET(Value::createInt(ret));
}

// ##################
// ##     Bool     ##
// ##################

//!bind: function $OP_NOT($this : Bool) : Bool
ARSH_METHOD boolean_not(RuntimeContext &ctx) {
  SUPPRESS_WARNING(boolean_not);
  RET_BOOL(!LOCAL(0).asBool());
}

//!bind: function $OP_EQ($this : Bool, $target : Bool) : Bool
ARSH_METHOD boolean_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(boolean_eq);
  RET_BOOL(LOCAL(0).asBool() == LOCAL(1).asBool());
}

//!bind: function $OP_NE($this : Bool, $target : Bool) : Bool
ARSH_METHOD boolean_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(boolean_ne);
  RET_BOOL(LOCAL(0).asBool() != LOCAL(1).asBool());
}

// ####################
// ##     String     ##
// ####################

//!bind: function $OP_EQ($this : String, $target : String) : Bool
ARSH_METHOD string_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_eq);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : String, $target : String) : Bool
ARSH_METHOD string_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_ne);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left != right);
}

//!bind: function $OP_LT($this : String, $target : String) : Bool
ARSH_METHOD string_lt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_lt);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : String, $target : String) : Bool
ARSH_METHOD string_gt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_gt);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : String, $target : String) : Bool
ARSH_METHOD string_le(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_le);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : String, $target : String) : Bool
ARSH_METHOD string_ge(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_ge);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left >= right);
}

//!bind: function size($this : String) : Int
ARSH_METHOD string_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_size);
  size_t size = LOCAL(0).asStrRef().size();
  assert(size <= StringObject::MAX_SIZE);
  RET(Value::createInt(size));
}

//!bind: function empty($this : String) : Bool
ARSH_METHOD string_empty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_empty);
  bool empty = LOCAL(0).asStrRef().empty();
  RET_BOOL(empty);
}

//!bind: function ifEmpty($this : String, $default: Option<String>) : Option<String>
ARSH_METHOD string_ifEmpty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_ifEmpty);
  if (LOCAL(0).asStrRef().empty()) {
    RET(EXTRACT_LOCAL(1));
  }
  RET(EXTRACT_LOCAL(0));
}

//!bind: function count($this : String) : Int
ARSH_METHOD string_count(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_count);
  size_t count = iterateGrapheme(LOCAL(0).asStrRef(), [](const GraphemeCluster &) {});
  assert(count <= StringObject::MAX_SIZE);
  RET(Value::createInt(count));
}

//!bind: function chars($this : String) : Array<String>
ARSH_METHOD string_chars(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_chars);
  auto ref = LOCAL(0).asStrRef();
  auto value = Value::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(value);

  iterateGrapheme(ref, [&array](const GraphemeCluster &ret) {
    array.append(Value::createStr(ret)); // not check iterator invalidation
  });
  ASSERT_ARRAY_SIZE(array);
  RET(value);
}

//!bind: function words($this : String) : Array<String>
ARSH_METHOD string_words(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_words);
  auto ref = LOCAL(0).asStrRef();
  auto value = Value::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(value);
  iterateWord(ref, [&array](StringRef wordRef) {
    std::string word;
    const auto *end = wordRef.end();
    for (auto *iter = wordRef.begin(); iter != end;) {
      unsigned int size = UnicodeUtil::utf8ValidateChar(iter, end);
      if (size < 1) {
        word += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
        iter++;
      } else {
        word += StringRef(iter, size);
        iter += size;
      }
    }
    array.append(Value::createStr(std::move(word))); // not check iterator invalidation
  });
  ASSERT_ARRAY_SIZE(array);
  RET(value);
}

//!bind: function width($this : String, $eaw : Option<Int>) : Int
ARSH_METHOD string_width(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_width);
  auto ref = LOCAL(0).asStrRef();
  auto &v = LOCAL(1);

  // resolve east-asian width option
  auto n = v.isInvalid() ? 0 : v.asInt();
  auto eaw = n == 2                       ? AmbiguousCharWidth::FULL
             : n == 1                     ? AmbiguousCharWidth::HALF
             : UnicodeUtil::isCJKLocale() ? AmbiguousCharWidth::FULL
                                          : AmbiguousCharWidth::HALF;

  CharWidthProperties ps = {
      .eaw = eaw,
      .flagSeqWidth = 2,
      .zwjSeqFallback = false,
      .replaceInvalid = true,
  };
  int64_t value = 0;
  iterateGrapheme(
      ref, [&value, &ps](const GraphemeCluster &ret) { value += getGraphemeWidth(ps, ret); });

  RET(Value::createInt(value));
}

#define TRY(E)                                                                                     \
  ({                                                                                               \
    auto __value = E;                                                                              \
    if (unlikely(ctx.hasError())) {                                                                \
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
  index += static_cast<int64_t>(index < 0 ? size : 0);
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
ARSH_METHOD string_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_get);
  auto ref = LOCAL(0).asStrRef();
  const size_t size = ref.size();
  const auto index = LOCAL(1).asInt();

  auto value = TRY(resolveIndex(ctx, index, size));
  RET(Value::createStr(ref.substr(value.index, 1)));
}

//!bind: function charAt($this : String, $index : Int) : String
ARSH_METHOD string_charAt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_charAt);

  StringRef ref = LOCAL(0).asStrRef();
  Utf8GraphemeScanner scanner(Utf8Stream(ref.begin(), ref.end()));
  const auto pos = LOCAL(1).asInt();

  ssize_t count = 0;
  for (; scanner.hasNext(); count++) {
    GraphemeCluster ret = scanner.next();
    if (count == pos) {
      RET(Value::createStr(ret));
    }
  }
  std::string msg = "character count is ";
  msg += std::to_string(count);
  msg += ", but character position is ";
  msg += std::to_string(pos);
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

static Value sliceImpl(const ArrayObject &obj, size_t begin, size_t end) {
  auto b = obj.getValues().begin() + begin;
  auto e = obj.getValues().begin() + end;
  return Value::create<ArrayObject>(obj.getTypeID(), std::vector<Value>(b, e));
}

static Value sliceImpl(const StringRef &ref, size_t begin, size_t end) {
  return Value::createStr(ref.slice(begin, end));
}

static std::pair<uint64_t, uint64_t> resolveSliceRange(uint64_t objSize, int64_t startIndex,
                                                       int64_t stopIndex) {
  assert(objSize <= INT64_MAX);
  const auto size = static_cast<int64_t>(objSize);

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
  return {static_cast<uint64_t>(startIndex), static_cast<uint64_t>(stopIndex)};
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
  auto [start, stop] = resolveSliceRange(obj.size(), startIndex, stopIndex);
  return sliceImpl(obj, start, stop);
}

//!bind: function slice($this : String, $start : Int, $stop : Option<Int>) : String
ARSH_METHOD string_slice(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_slice);
  auto ref = LOCAL(0).asStrRef();
  auto start = LOCAL(1).asInt();
  auto &v = LOCAL(2);
  assert(ref.size() <= StringObject::MAX_SIZE);
  auto stop = v.isInvalid() ? static_cast<int64_t>(ref.size()) : v.asInt();
  RET(slice(ref, start, stop));
}

//!bind: function startsWith($this : String, $target : String) : Bool
ARSH_METHOD string_startsWith(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_startsWith);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left.startsWith(right));
}

//!bind: function endsWith($this : String, $target : String) : Bool
ARSH_METHOD string_endsWith(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_endsWith);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left.endsWith(right));
}

//!bind: function indexOf($this : String, $target : String, $index : Option<Int>) : Int
ARSH_METHOD string_indexOf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_indexOf);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  auto v = LOCAL(2);
  auto offset = TRY(resolveIndex(ctx, v.isInvalid() ? 0 : v.asInt(), left.size(), true));
  auto index = left.find(right, offset.index);
  assert(index == StringRef::npos || index <= StringObject::MAX_SIZE);
  auto actual = static_cast<ssize_t>(index); // for integer promotion
  RET(Value::createInt(static_cast<int64_t>(actual)));
}

//!bind: function lastIndexOf($this : String, $target : String) : Int
ARSH_METHOD string_lastIndexOf(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_lastIndexOf);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  auto index = left.lastIndexOf(right);
  assert(index == StringRef::npos || index <= StringObject::MAX_SIZE);
  auto actual = static_cast<ssize_t>(index); // for integer promotion
  RET(Value::createInt(static_cast<int64_t>(actual)));
}

//!bind: function contains($this : String, $target : String) : Bool
ARSH_METHOD string_contains(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_contains);
  auto left = LOCAL(0).asStrRef();
  auto right = LOCAL(1).asStrRef();
  RET_BOOL(left.contains(right));
}

//!bind: function split($this : String, $delim : String) : Array<String>
ARSH_METHOD string_split(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_split);
  auto results = Value::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &ptr = typeAs<ArrayObject>(results);

  auto thisStr = LOCAL(0).asStrRef();
  auto delimStr = LOCAL(1).asStrRef();

  if (delimStr.empty()) {
    ptr.append(LOCAL(0)); // not check iterator invalidation
  } else {
    for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
      auto ret = thisStr.find(delimStr, pos);
      ptr.append(Value::createStr(thisStr.slice(pos, ret))); // not check iterator invalidation
      pos = ret != StringRef::npos ? ret + delimStr.size() : ret;
    }
  }
  ASSERT_ARRAY_SIZE(ptr);
  RET(results);
}

//!bind: function replace($this : String, $target : String, $rep : String, $once: Option<Bool>) : String
ARSH_METHOD string_replace(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_replace);

  auto delimStr = LOCAL(1).asStrRef();
  if (delimStr.empty()) {
    RET(LOCAL(0));
  }

  const auto thisStr = LOCAL(0).asStrRef();
  const auto repStr = LOCAL(2).asStrRef();
  const bool once = LOCAL(3).isInvalid() ? false : LOCAL(3).asBool();
  auto buf = Value::createStr();

  StringRef::size_type pos = 0;
  if (auto ret = thisStr.find(delimStr); ret != StringRef::npos) {
    if (!buf.appendAsStr(ctx, thisStr.slice(pos, ret)) || !buf.appendAsStr(ctx, repStr)) {
      RET_ERROR;
    }
    pos = ret + delimStr.size();
    if (once) {
      if (!buf.appendAsStr(ctx, thisStr.substr(pos))) {
        RET_ERROR;
      }
      pos = StringRef::npos;
    }
  } else { // no delim
    RET(LOCAL(0));
  }

  while (pos != StringRef::npos) {
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

//!bind: function validate($this: String): Bool
ARSH_METHOD string_validate(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_validate);
  const auto ref = LOCAL(0).asStrRef();
  const auto begin = ref.begin();
  auto iter = begin;
  for (const auto end = ref.end(); iter != end;) {
    if (unsigned int byteSize = UnicodeUtil::utf8ValidateChar(iter, end); byteSize != 0) {
      iter += byteSize;
    } else {
      RET_BOOL(false);
    }
  }
  RET_BOOL(true);
}

//!bind: function sanitize($this : String, $repl : Option<String>) : String
ARSH_METHOD string_sanitize(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_sanitize);
  auto ref = LOCAL(0).asStrRef();
  auto v = LOCAL(1);
  auto repl = v.isInvalid() ? "" : v.asStrRef();

  auto ret = Value::createStr();
  auto begin = ref.begin();
  auto old = begin;
  for (const auto end = ref.end(); begin != end;) {
    int codePoint = 0;
    unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(begin, end, codePoint);
    if (byteSize == 0) { // replace invalid utf8 bytes
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
ARSH_METHOD string_toInt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_toInt);
  auto ref = LOCAL(0).asStrRef();
  auto &v = LOCAL(1);
  if (auto radix = v.isInvalid() ? 0 : v.asInt(); radix >= 0 && radix <= UINT32_MAX) {
    auto ret = convertToNum<int64_t>(ref.begin(), ref.end(), static_cast<unsigned int>(radix));
    if (ret) {
      RET(Value::createInt(ret.value));
    }
  }
  RET(Value::createInvalid());
}

//!bind: function toFloat($this : String) : Option<Float>
ARSH_METHOD string_toFloat(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_toFloat);
  auto ref = LOCAL(0).asStrRef();
  if (!ref.hasNullChar()) {
    if (auto ret = convertToDouble(ref.data(), false)) {
      RET(Value::createFloat(ret.value));
    }
  }
  RET(Value::createInvalid());
}

static Utf8GraphemeScanner asGraphemeScanner(StringRef ref, const uint32_t (&values)[3]) {
  const char *charBegin = ref.begin() + values[0];
  const char *cur = ref.begin() + values[1];
  auto vv = CodePointWithMeta::from(values[2]);
  return Utf8GraphemeScanner(Utf8Stream(cur, ref.end()), charBegin, vv.codePoint(),
                             static_cast<GraphemeBoundary::BreakProperty>(vv.getMeta()));
}

static Value asDSValue(StringRef ref, const Utf8GraphemeScanner &scanner) {
  unsigned int prevPos = scanner.getCharBegin() - ref.begin();
  unsigned int curPos = scanner.getStream().iter - ref.begin();
  unsigned int v =
      CodePointWithMeta(scanner.getCodePoint(), toUnderlying(scanner.getProperty())).getValue();

  return Value::createNumList(prevPos, curPos, v);
}

//!bind: function $OP_ITER($this : String) : StringIter
ARSH_METHOD string_iter(RuntimeContext &ctx) {
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
  auto value = Value::create<BaseObject>(ctx.typePool.get(TYPE::StringIter), 2);
  auto &obj = typeAs<BaseObject>(value);
  StringRef ref = LOCAL(0).asStrRef();
  Utf8GraphemeScanner scanner(Utf8Stream(ref.begin(), ref.end()));
  obj[0] = LOCAL(0);
  obj[1] = asDSValue(ref, scanner);
  RET(value);
}

//!bind: function $OP_MATCH($this : String, $re : Regex) : Bool
ARSH_METHOD string_match(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_match);
  auto str = LOCAL(0).asStrRef();
  auto &re = typeAs<RegexObject>(LOCAL(1));
  bool r = TRY(re.search(ctx, str));
  RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : String, $re : Regex) : Bool
ARSH_METHOD string_unmatch(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_unmatch);
  auto str = LOCAL(0).asStrRef();
  auto &re = typeAs<RegexObject>(LOCAL(1));
  bool r = !TRY(re.search(ctx, str));
  RET_BOOL(r);
}

//!bind: function realpath($this : String) : String
ARSH_METHOD string_realpath(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_realpath);
  auto ref = LOCAL(0).asStrRef();
  if (ref.hasNullChar()) {
    raiseError(ctx, TYPE::ArgumentError, ERROR_NULL_CHAR_PATH);
    RET_ERROR;
  }

  errno = 0;
  auto buf = getRealpath(ref.data());
  if (buf) {
    RET(Value::createStr(buf.get()));
  } else {
    int errNum = errno;
    std::string value = "cannot resolve realpath of `";
    appendAsPrintable(ref, SYS_LIMIT_ERROR_MSG_MAX - 1, value);
    value += "'"; // no check size
    raiseSystemError(ctx, errNum, std::move(value));
    RET_ERROR;
  }
}

//!bind: function basename($this : String) : String
ARSH_METHOD string_basename(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_basename);
  auto ref = LOCAL(0).asStrRef();
  if (ref.hasNullChar()) {
    raiseError(ctx, TYPE::ArgumentError, ERROR_NULL_CHAR_PATH);
    RET_ERROR;
  }
  auto ret = getBasename(ref);
  RET(Value::createStr(ret));
}

//!bind: function dirname($this : String) : String
ARSH_METHOD string_dirname(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_dirname);
  auto ref = LOCAL(0).asStrRef();
  if (ref.hasNullChar()) {
    raiseError(ctx, TYPE::ArgumentError, ERROR_NULL_CHAR_PATH);
    RET_ERROR;
  }
  auto ret = getDirname(ref);
  RET(Value::createStr(ret));
}

//!bind: function lower($this : String) : String
ARSH_METHOD string_lower(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_lower);
  auto ret = Value::createStr(LOCAL(0).asStrRef());
  auto ref = ret.asStrRef();
  std::transform(ref.begin(), ref.end(), const_cast<char *>(ref.begin()), ::tolower);
  RET(ret);
}

//!bind: function upper($this : String) : String
ARSH_METHOD string_upper(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_upper);
  auto ret = Value::createStr(LOCAL(0).asStrRef());
  auto ref = ret.asStrRef();
  std::transform(ref.begin(), ref.end(), const_cast<char *>(ref.begin()), ::toupper);
  RET(ret);
}

//!bind: function foldCase($this: String, $full: Option<Bool>, $turkic: Option<Bool>): String
ARSH_METHOD string_foldCase(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_foldCase);
  const auto ref = LOCAL(0).asStrRef();
  auto &full = LOCAL(1);
  auto &turkic = LOCAL(2);
  CaseFoldOp op{};
  if (!full.isInvalid() && full.asBool()) {
    setFlag(op, CaseFoldOp::FULL_FOLD);
  }
  if (!turkic.isInvalid() && turkic.asBool()) {
    setFlag(op, CaseFoldOp::TURKIC);
  }

  auto ret = Value::createStr();
  auto old = ref.begin();
  auto iter = old;
  for (const auto end = ref.end(); iter != end;) {
    int codePoint = 0;
    const unsigned int byteSize = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint);
    if (byteSize) {
      auto foldResult = doCaseFolding(codePoint, op);
      if (foldResult.equals(codePoint)) {
        iter += byteSize;
        continue;
      }

      char buf[4 * (CaseFoldingResult::FULL_FOLD_ENTRY_SIZE + 1)];
      unsigned int usedSize = 0;
      if (foldResult.isFullFolding()) {
        auto &data = foldResult.getFullFolding();
        for (auto &e : data) {
          if (e == 0) {
            break;
          }
          usedSize += UnicodeUtil::codePointToUtf8(e, buf + usedSize);
        }
      } else {
        usedSize += UnicodeUtil::codePointToUtf8(foldResult.getSimpleFolding(), buf + usedSize);
      }
      if (!ret.appendAsStr(ctx, StringRef(old, iter - old)) ||
          !ret.appendAsStr(ctx, StringRef(buf, usedSize))) {
        RET_ERROR;
      }
      iter += byteSize;
      old = iter;
    } else {
      raiseError(ctx, TYPE::InvalidOperationError, "must be UTF-8 encoded");
      RET_ERROR;
    }
  }

  if (old == ref.begin()) {
    RET(LOCAL(0));
  }
  if (!ret.appendAsStr(ctx, StringRef(old, iter - old))) {
    RET_ERROR;
  }
  RET(ret);
}

//!bind: function quote($this : String) : String
ARSH_METHOD string_quote(RuntimeContext &ctx) {
  SUPPRESS_WARNING(string_quote);
  auto ref = LOCAL(0).asStrRef();
  auto ret = quoteAsShellArg(ref);
  if (ret.size() > StringObject::MAX_SIZE) {
    raiseStringLimit(ctx);
    RET_ERROR;
  }
  RET(Value::createStr(std::move(ret)));
}

// ########################
// ##     StringIter     ##
// ########################

//!bind: function $OP_NEXT($this : StringIter) : String
ARSH_METHOD stringIter_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(stringIter_next);
  auto &iter = typeAs<BaseObject>(LOCAL(0));
  auto scanner = asGraphemeScanner(iter[0].asStrRef(), iter[1].asNumList());
  if (scanner.hasNext()) {
    GraphemeCluster ret = scanner.next();
    iter[1] = asDSValue(iter[0].asStrRef(), scanner);
    RET(Value::createStr(ret));
  } else {
    RET_VOID;
  }
}

// ###################
// ##     Regex     ##
// ###################

//!bind: function $OP_INIT($this : Regex, $str : String, $flag : Option<String>) : Regex
ARSH_METHOD regex_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_init);
  auto pattern = LOCAL(1).asStrRef();
  auto v = LOCAL(2);
  auto flagStr = v.isInvalid() ? "" : v.asStrRef();
  std::string errorStr;
  if (auto flag = PCRE::parseCompileFlag(flagStr, errorStr); flag.hasValue()) {
    if (auto re = PCRE::compile(pattern, flag.unwrap(), errorStr)) {
      RET(Value::create<RegexObject>(std::move(re)));
    }
  }
  raiseError(ctx, TYPE::RegexSyntaxError, std::move(errorStr));
  RET_ERROR;
}

//!bind: function isCaseless($this : Regex) : Bool
ARSH_METHOD regex_isCaseless(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_isCaseless);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto flag = re.getRE().getCompileFlag();
  RET_BOOL(hasFlag(flag, PCRECompileFlag::CASELESS));
}

//!bind: function isMultiLine($this : Regex) : Bool
ARSH_METHOD regex_isMultiLine(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_isMultiLine);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto flag = re.getRE().getCompileFlag();
  RET_BOOL(hasFlag(flag, PCRECompileFlag::MULTILINE));
}

//!bind: function isDotAll($this : Regex) : Bool
ARSH_METHOD regex_isDotAll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_isDotAll);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto flag = re.getRE().getCompileFlag();
  RET_BOOL(hasFlag(flag, PCRECompileFlag::DOTALL));
}

//!bind: function $OP_MATCH($this : Regex, $target : String) : Bool
ARSH_METHOD regex_search(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_search);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto ref = LOCAL(1).asStrRef();
  bool r = TRY(re.search(ctx, ref));
  RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : Regex, $target : String) : Bool
ARSH_METHOD regex_unmatch(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_unmatch);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  auto ref = LOCAL(1).asStrRef();
  bool r = !TRY(re.search(ctx, ref));
  RET_BOOL(r);
}

//!bind: function match($this: Regex, $target : String) : Option<RegexMatch>
ARSH_METHOD regex_match(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_match);
  auto re = toObjPtr<RegexObject>(LOCAL(0));
  const auto ref = LOCAL(1).asStrRef();

  if (RegexObject::MatchResult result; TRY(re->match(ctx, ref, &result))) {
    auto ret = Value::create<RegexMatchObject>(std::move(re), std::move(result));
    RET(ret);
  }
  RET(Value::createInvalid());
}

//!bind: function replace($this: Regex, $target : String, $repl : String, $once: Option<Bool>) : String
ARSH_METHOD regex_replace(RuntimeContext &ctx) {
  SUPPRESS_WARNING(regex_replace);
  auto &re = typeAs<RegexObject>(LOCAL(0));
  const bool once = LOCAL(3).isInvalid() ? false : LOCAL(3).asBool();
  if (std::string out; re.replace(LOCAL(1).asStrRef(), LOCAL(2).asStrRef(), out, !once)) {
    assert(out.size() <= StringObject::MAX_SIZE);
    RET(Value::createStr(std::move(out)));
  } else {
    raiseError(ctx, TYPE::RegexMatchError, std::move(out));
    RET_ERROR;
  }
}

// ########################
// ##     RegexMatch     ##
// ########################

//!bind: function start($this : RegexMatch) : Int
ARSH_METHOD match_start(RuntimeContext &ctx) {
  SUPPRESS_WARNING(match_start);
  const auto &match = typeAs<RegexMatchObject>(LOCAL(0));
  RET(Value::createInt(match.getStartOffset()));
}

//!bind: function end($this : RegexMatch) : Int
ARSH_METHOD match_end(RuntimeContext &ctx) {
  SUPPRESS_WARNING(match_end);
  const auto &match = typeAs<RegexMatchObject>(LOCAL(0));
  RET(Value::createInt(match.getEndOffset()));
}

//!bind: function count($this : RegexMatch) : Int
ARSH_METHOD match_count(RuntimeContext &ctx) {
  SUPPRESS_WARNING(match_count);
  const auto &match = typeAs<RegexMatchObject>(LOCAL(0));
  RET(Value::createInt(static_cast<int64_t>(match.getGroups().size())));
}

//!bind: function group($this : RegexMatch, $index: Int) : Option<String>
ARSH_METHOD match_group(RuntimeContext &ctx) {
  SUPPRESS_WARNING(match_group);
  const auto &match = typeAs<RegexMatchObject>(LOCAL(0));
  if (int64_t index = LOCAL(1).asInt();
      index > -1 && static_cast<uint64_t>(index) < match.getGroups().size()) {
    RET(match.getGroups()[index]);
  }
  RET(Value::createInvalid());
}

//!bind: function named($this : RegexMatch, $name : String) : Option<String>
ARSH_METHOD match_named(RuntimeContext &ctx) {
  SUPPRESS_WARNING(match_named);
  const auto &obj = typeAs<RegexMatchObject>(LOCAL(0));
  const int index = obj.getRE().getGroupIndexByName(LOCAL(1).asStrRef());
  if (index > -1 && static_cast<uint64_t>(index) < obj.getGroups().size()) {
    RET(obj.getGroups()[index]);
  }
  RET(Value::createInvalid());
}

//!bind: function names($this : RegexMatch) : Array<String>
ARSH_METHOD match_names(RuntimeContext &ctx) {
  SUPPRESS_WARNING(match_names);
  const auto &obj = typeAs<RegexMatchObject>(LOCAL(0));
  const auto table = obj.getRE().getNamedGroupTable();
  std::vector<Value> values(table.size);
  for (unsigned int i = 0; i < table.size; i++) {
    values[i] = Value::createStr(table[i].second);
  }
  auto ret = Value::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray), std::move(values));
  RET(ret);
}

// ####################
// ##     Signal     ##
// ####################

//!bind: function name($this : Signal) : String
ARSH_METHOD signal_name(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_name);
  auto *e = findSignalEntryByNum(LOCAL(0).asSig());
  assert(e);
  RET(Value::createStr(e->abbrName));
}

//!bind: function value($this : Signal) : Int
ARSH_METHOD signal_value(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_value);
  RET(Value::createInt(LOCAL(0).asSig()));
}

//!bind: function message($this : Signal) : String
ARSH_METHOD signal_message(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_message);
  const char *value = strsignal(LOCAL(0).asSig());
  RET(Value::createStr(value));
}

static bool checkPidLimit(int64_t value) {
  if (value >= std::numeric_limits<pid_t>::min() && value <= std::numeric_limits<pid_t>::max()) {
    return true;
  }
  errno = ESRCH;
  return false;
}

//!bind: function kill($this : Signal, $pid : Int) : Void
ARSH_METHOD signal_kill(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_kill);
  int sigNum = LOCAL(0).asSig();
  int64_t pid = LOCAL(1).asInt();
  if (checkPidLimit(pid) && kill(static_cast<pid_t>(pid), sigNum) == 0) {
    ctx.jobTable.waitForAny();
    RET_VOID;
  }
  const int errNum = errno;
  std::string str = "failed to send ";
  str += findSignalEntryByNum(sigNum)->abbrName;
  str += " to pid(";
  str += std::to_string(static_cast<pid_t>(pid));
  str += ")";
  raiseSystemError(ctx, errNum, std::move(str));
  RET_ERROR;
}

//!bind: function trap($this: Signal, $handler: Option<Func<Void,[Signal]>>): Func<Void,[Signal]>
ARSH_METHOD signal_trap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_trap);
  int sigNum = LOCAL(0).asSig();
  auto value = LOCAL(1);
  ObjPtr<Object> handler;
  if (!value.isInvalid()) {
    handler = value.toPtr();
  }
  auto old = installSignalHandler(ctx, sigNum, std::move(handler));
  assert(old);
  RET(old);
}

//!bind: function $OP_EQ($this : Signal, $target : Signal) : Bool
ARSH_METHOD signal_eq(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_eq);
  RET_BOOL(LOCAL(0).asSig() == LOCAL(1).asSig());
}

//!bind: function $OP_NE($this : Signal, $target : Signal) : Bool
ARSH_METHOD signal_ne(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signal_ne);
  RET_BOOL(LOCAL(0).asSig() != LOCAL(1).asSig());
}

// #####################
// ##     Signals     ##
// #####################

//!bind: function $OP_GET($this : Signals, $key : String) : Signal
ARSH_METHOD signals_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signals_get);
  auto key = LOCAL(1).asStrRef();
  auto *e = findSignalEntryByName(key);
  if (!e) {
    std::string msg = "undefined signal: ";
    appendAsPrintable(key, SYS_LIMIT_ERROR_MSG_MAX, msg);
    raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
    RET_ERROR;
  }
  RET(Value::createSig(e->sigNum));
}

//!bind: function get($this : Signals, $key : String) : Option<Signal>
ARSH_METHOD signals_signal(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signals_signal);
  auto key = LOCAL(1).asStrRef();
  auto *e = findSignalEntryByName(key);
  if (!e) {
    RET(Value::createInvalid());
  }
  RET(Value::createSig(e->sigNum));
}

//!bind: function list($this : Signals) : Array<Signal>
ARSH_METHOD signals_list(RuntimeContext &ctx) {
  SUPPRESS_WARNING(signals_list);

  auto ret = ctx.typePool.createArrayType(ctx.typePool.get(TYPE::Signal));
  assert(ret);
  auto type = std::move(ret).take();
  auto v = Value::create<ArrayObject>(*type);
  auto &array = typeAs<ArrayObject>(v);
  auto list = toSortedUniqueSignalEntries();
  for (auto &e : list) {
    array.append(Value::createSig(e.sigNum)); // not check iterator invalidation
  }
  ASSERT_ARRAY_SIZE(array);
  RET(v);
}

// ####################
// ##     Module     ##
// ####################

static void raiseInvalidOperationError(ARState &st, std::string &&m) {
  raiseError(st, TYPE::InvalidOperationError, std::move(m));
}

static bool checkModLayout(ARState &state, const Value &value) {
  if (value.isObject() && isa<FuncObject>(value.get())) {
    return true;
  }
  raiseInvalidOperationError(state, "cannot call method on temporary module object");
  return false;
}

#define CHECK_MOD_LAYOUT(obj)                                                                      \
  do {                                                                                             \
    if (!checkModLayout(ctx, obj)) {                                                               \
      RET_ERROR;                                                                                   \
    }                                                                                              \
  } while (false)

//!bind: function _scriptName($this : Module) : String
ARSH_METHOD module_name(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_name);

  CHECK_MOD_LAYOUT(LOCAL(0));
  auto &obj = typeAs<FuncObject>(LOCAL(0));
  RET(obj.getCode().getConstPool()[CVAR_OFFSET_SCRIPT_NAME]);
}

//!bind: function _scriptDir($this : Module) : String
ARSH_METHOD module_dir(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_dir);

  CHECK_MOD_LAYOUT(LOCAL(0));
  auto &obj = typeAs<FuncObject>(LOCAL(0));
  RET(obj.getCode().getConstPool()[CVAR_OFFSET_SCRIPT_DIR]);
}

//!bind: function _func($this : Module, $expr : String) : Func<Option<Any>>
ARSH_METHOD module_func(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_func);

  if (!ctx.tempModScope.empty()) {
    raiseInvalidOperationError(ctx, "cannot call method within user-defined completer");
    RET_ERROR;
  }
  assert(LOCAL(0).isObject());
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  const auto ref = LOCAL(1).asStrRef();
  assert(type.isModType());
  auto &modType = cast<ModType>(type);
  if (auto ret = loadExprAsFunc(ctx, ref, modType)) {
    RET(ret);
  }
  RET_ERROR;
}

//!bind: function _fullname($this : Module, $name : String) : Option<String>
ARSH_METHOD module_fullname(RuntimeContext &ctx) {
  SUPPRESS_WARNING(module_fullname);

  CHECK_MOD_LAYOUT(LOCAL(0));
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  auto cmdName = LOCAL(1).asStrRef();
  assert(type.isModType());
  auto &modType = cast<ModType>(type);
  auto path = resolveFullCommandName(ctx, cmdName, modType);
  if (path.empty()) {
    RET(Value::createInvalid());
  } else {
    RET(Value::createStr(std::move(path)));
  }
}

// ###################
// ##     Array     ##
// ###################

#define CHECK_ITER_INVALIDATION(obj)                                                               \
  do {                                                                                             \
    if (unlikely(!((obj).checkIteratorInvalidation(ctx)))) {                                       \
      RET_ERROR;                                                                                   \
    }                                                                                              \
  } while (false)

//!bind: function $OP_GET($this : Array<T0>, $index : Int) : T0
ARSH_METHOD array_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_get);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, size));
  RET(obj.getValues()[ret.index]);
}

//!bind: function get($this : Array<T0>, $index : Int) : Option<T0>
ARSH_METHOD array_get2(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_get2);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = resolveIndex(index, size, false);
  if (!ret) {
    RET(Value::createInvalid());
  }
  RET(obj.getValues()[ret.index]);
}

//!bind: function $OP_SET($this : Array<T0>, $index : Int, $value : T0) : Void
ARSH_METHOD array_set(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_set);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, size));
  obj.refValues()[ret.index] = EXTRACT_LOCAL(2);
  RET_VOID;
}

//!bind: function remove($this : Array<T0>, $index : Int) : T0
ARSH_METHOD array_remove(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_remove);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  size_t size = obj.getValues().size();
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, size));
  auto v = obj.getValues()[ret.index];
  obj.refValues().erase(obj.refValues().begin() + ret.index);
  RET(v);
}

//!bind: function removeRange($this : Array<T0>, $from : Int, $to : Option<Int>) : Void
ARSH_METHOD array_removeRange(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_removeRange);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto from = LOCAL(1).asInt();
  auto &v = LOCAL(2);
  assert(obj.size() <= ArrayObject::MAX_SIZE);
  auto to = v.isInvalid() ? static_cast<int64_t>(obj.size()) : v.asInt();
  auto [start, stop] = resolveSliceRange(obj.size(), from, to);
  auto &values = obj.refValues();
  values.erase(values.begin() + static_cast<ssize_t>(start),
               values.begin() + static_cast<ssize_t>(stop));
  RET_VOID;
}

static bool array_fetch(RuntimeContext &ctx, Value &value, bool fetchLast = true) {
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  if (obj.getValues().empty()) {
    raiseOutOfRangeError(ctx, std::string("Array size is 0"));
    return false;
  }
  value = fetchLast ? obj.getValues().back() : obj.getValues().front();
  return true;
}

//!bind: function peek($this : Array<T0>) : T0
ARSH_METHOD array_peek(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_peek);
  Value value;
  array_fetch(ctx, value);
  return value;
}

static bool array_insertImpl(ARState &ctx, int64_t index, const Value &v) {
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  if (unlikely(!obj.checkIteratorInvalidation(ctx))) {
    return false;
  }
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
ARSH_METHOD array_push(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_push);
  TRY(array_pushImpl(ctx));
  RET_VOID;
}

//!bind: function pop($this : Array<T0>) : T0
ARSH_METHOD array_pop(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_pop);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  Value v;
  TRY(array_fetch(ctx, v));
  obj.refValues().pop_back();
  RET(v);
}

//!bind: function shift($this : Array<T0>) : T0
ARSH_METHOD array_shift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_shift);

  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  Value v;
  TRY(array_fetch(ctx, v, false));
  auto &values = obj.refValues();
  values.erase(values.begin());
  RET(v);
}

//!bind: function unshift($this : Array<T0>, $value : T0) : Void
ARSH_METHOD array_unshift(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_unshift);
  TRY(array_insertImpl(ctx, 0, LOCAL(1)));
  RET_VOID;
}

//!bind: function insert($this : Array<T0>, $index : Int, $value : T0) : Void
ARSH_METHOD array_insert(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_insert);
  TRY(array_insertImpl(ctx, LOCAL(1).asInt(), LOCAL(2)));
  RET_VOID;
}

//!bind: function add($this : Array<T0>, $value : T0) : Array<T0>
ARSH_METHOD array_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_add);
  TRY(array_pushImpl(ctx));
  RET(LOCAL(0));
}

//!bind: function addAll($this : Array<T0>, $value : Array<T0>) : Array<T0>
ARSH_METHOD array_addAll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_addAll);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto &value = typeAs<ArrayObject>(LOCAL(1));
  if (&obj != &value) {
    size_t valueSize = value.getValues().size();
    for (size_t i = 0; i < valueSize; i++) {
      TRY(obj.append(ctx, Value(value.getValues()[i])));
    }
  }
  RET(LOCAL(0));
}

//!bind: function swap($this : Array<T0>, $index : Int, $value : T0) : T0
ARSH_METHOD array_swap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_swap);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto index = LOCAL(1).asInt();
  auto ret = TRY(resolveIndex(ctx, index, obj.getValues().size()));
  Value value = LOCAL(2);
  std::swap(obj.refValues()[ret.index], value);
  RET(value);
}

//!bind: function slice($this : Array<T0>, $from : Int, $to : Option<Int>) : Array<T0>
ARSH_METHOD array_slice(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_slice);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  auto start = LOCAL(1).asInt();
  auto &v = LOCAL(2);
  assert(obj.size() <= ArrayObject::MAX_SIZE);
  auto stop = v.isInvalid() ? static_cast<int64_t>(obj.size()) : v.asInt();
  RET(slice(obj, start, stop));
}

//!bind: function copy($this : Array<T0>) : Array<T0>
ARSH_METHOD array_copy(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_copy);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  RET(obj.copy());
}

//!bind: function reverse($this : Array<T0>) : Array<T0>
ARSH_METHOD array_reverse(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_reverse);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  std::reverse(obj.refValues().begin(), obj.refValues().end());
  RET(LOCAL(0));
}

//!bind: function sort($this : Array<T0>) : Array<T0> where T0 : Value_
ARSH_METHOD array_sort(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_sort);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  std::sort(obj.refValues().begin(), obj.refValues().end(),
            [](const Value &x, const Value &y) { return x.compare(y) < 0; });
  RET(LOCAL(0));
}

//!bind: function sortWith($this : Array<T0>, $comp : Func<Bool, [T0, T0]>) : Array<T0>
ARSH_METHOD array_sortWith(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_sortWith);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto &comp = LOCAL(1);
  if (mergeSort(ctx, obj, comp)) {
    RET(LOCAL(0));
  } else {
    RET_ERROR;
  }
}

//!bind: function join($this : Array<T0>, $delim : Option<String>) : String
ARSH_METHOD array_join(RuntimeContext &ctx) {
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
ARSH_METHOD array_map(RuntimeContext &ctx) {
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
ARSH_METHOD array_indexOf(RuntimeContext &ctx) {
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
  RET(Value::createInt(index));
}

//!bind: function lastIndexOf($this : Array<T0>, $target : T0) : Int where T0 : Value_
ARSH_METHOD array_lastIndexOf(RuntimeContext &ctx) {
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
  RET(Value::createInt(index));
}

//!bind: function contains($this : Array<T0>, $target : T0) : Bool where T0 : Value_
ARSH_METHOD array_contains(RuntimeContext &ctx) {
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

//!bind: function trap($this : Array<T0>, $handler : Func<Void,[Signal]>) : Void where T0 : Signal
ARSH_METHOD array_trap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_trap);
  auto &arrayObj = typeAs<ArrayObject>(LOCAL(0));
  auto handler = LOCAL(1).toPtr();
  AtomicSigSet set;
  for (auto &e : arrayObj.getValues()) {
    set.add(e.asSig());
  }
  installSignalHandler(ctx, set, handler);
  RET_VOID;
}

//!bind: function size($this : Array<T0>) : Int
ARSH_METHOD array_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_size);
  size_t size = typeAs<ArrayObject>(LOCAL(0)).getValues().size();
  assert(size <= ArrayObject::MAX_SIZE);
  RET(Value::createInt(size));
}

//!bind: function empty($this : Array<T0>) : Bool
ARSH_METHOD array_empty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_empty);
  bool empty = typeAs<ArrayObject>(LOCAL(0)).getValues().empty();
  RET_BOOL(empty);
}

//!bind: function clear($this : Array<T0>) : Void
ARSH_METHOD array_clear(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_clear);
  auto &obj = typeAs<ArrayObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  obj.refValues().clear();
  RET_VOID;
}

//!bind: function $OP_ITER($this : Array<T0>) : Array<T0>
ARSH_METHOD array_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_iter);
  RET(Value::create<ArrayIterObject>(toObjPtr<ArrayObject>(LOCAL(0))));
}

//!bind: function $OP_NEXT($this : Array<T0>) : T0
ARSH_METHOD array_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(array_next);
  auto &obj = typeAs<ArrayIterObject>(LOCAL(0));
  if (obj.hasNext()) {
    RET(obj.next());
  } else {
    RET_VOID;
  }
}

// #################
// ##     Map     ##
// #################

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
ARSH_METHOD map_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_get);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  auto &key = LOCAL(1);
  auto retIndex = obj.lookup(key);
  if (retIndex == -1) {
    std::string msg = "not found key: ";
    appendAsPrintable(key.hasStrRef() ? key.asStrRef() : key.toString(), SYS_LIMIT_ERROR_MSG_MAX,
                      msg);
    raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
    RET_ERROR;
  }
  RET(obj[retIndex].getValue());
}

//!bind: function $OP_SET($this : Map<T0, T1>, $key : T0, $value : T1) : Void
ARSH_METHOD map_set(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_set);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  TRY(obj.put(ctx, EXTRACT_LOCAL(1), EXTRACT_LOCAL(2)));
  RET_VOID;
}

//!bind: function put($this : Map<T0, T1>, $key : T0, $value : T1) : Option<T1>
ARSH_METHOD map_put(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_put);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto v = TRY(obj.put(ctx, EXTRACT_LOCAL(1), EXTRACT_LOCAL(2)));
  RET(v);
}

//!bind: function putIfAbsent($this : Map<T0, T1>, $key : T0, $value : T1) : T1
ARSH_METHOD map_putIfAbsent(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_putIfAbsent);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto pair = obj.insert(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
  if (unlikely(pair.first == -1)) {
    raiseOutOfRangeError(ctx, ERROR_MAP_LIMIT);
    RET_ERROR;
  }
  RET(obj[pair.first].getValue());
}

//!bind: function size($this : Map<T0, T1>) : Int
ARSH_METHOD map_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_size);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  unsigned int value = obj.size();
  RET(Value::createInt(value));
}

//!bind: function empty($this : Map<T0, T1>) : Bool
ARSH_METHOD map_empty(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_empty);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  RET_BOOL(obj.size() == 0);
}

//!bind: function get($this : Map<T0, T1>, $key : T0) : Option<T1>
ARSH_METHOD map_find(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_find);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  auto retIndex = obj.lookup(LOCAL(1));
  RET(retIndex != -1 ? obj[retIndex].getValue() : Value::createInvalid());
}

//!bind: function remove($this : Map<T0, T1>, $key : T0) : Option<T1>
ARSH_METHOD map_remove(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_remove);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  auto e = obj.remove(LOCAL(1));
  RET(e ? std::move(e.refValue()) : Value::createInvalid());
}

//!bind: function swap($this : Map<T0, T1>, $key : T0, $value : T1) : T1
ARSH_METHOD map_swap(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_swap);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  Value value = LOCAL(2);
  auto &key = LOCAL(1);
  auto retIndex = obj.lookup(key);
  if (retIndex == -1) {
    std::string msg = "not found key: ";
    appendAsPrintable(key.hasStrRef() ? key.asStrRef() : key.toString(), SYS_LIMIT_ERROR_MSG_MAX,
                      msg);
    raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
    RET_ERROR;
  }
  std::swap(obj[retIndex].refValue(), value);
  RET(value);
}

//!bind: function addAll($this : Map<T0, T1>, $value : Map<T0, T1>) : Map<T0, T1>
ARSH_METHOD map_addAll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_addAll);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  auto &value = typeAs<OrderedMapObject>(LOCAL(1));
  if (&obj != &value) {
    CHECK_ITER_INVALIDATION(obj);
    for (auto &e : value.getEntries()) {
      TRY(obj.put(ctx, Value(e.getKey()), Value(e.getValue())));
    }
  }
  RET(LOCAL(0));
}

//!bind: function copy($this : Map<T0, T1>) : Map<T0, T1>
ARSH_METHOD map_copy(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_copy);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  const auto &type = ctx.typePool.get(obj.getTypeID());
  auto ret = Value::create<OrderedMapObject>(type, ctx.getRng().next());
  auto &newMap = typeAs<OrderedMapObject>(ret);
  for (auto &e : obj.getEntries()) {
    newMap.insert(e.getKey(), Value(e.getValue()));
  }
  RET(ret);
}

//!bind: function clear($this : Map<T0, T1>) : Void
ARSH_METHOD map_clear(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_clear);
  auto &obj = typeAs<OrderedMapObject>(LOCAL(0));
  CHECK_ITER_INVALIDATION(obj);
  obj.clear();
  RET_VOID;
}

//!bind: function $OP_ITER($this : Map<T0, T1>) : Map<T0, T1>
ARSH_METHOD map_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_iter);
  RET(Value::create<OrderedMapIterObject>(toObjPtr<OrderedMapObject>(LOCAL(0))));
}

//!bind: function $OP_NEXT($this : Map<T0, T1>) : Tuple<T0, T1>
ARSH_METHOD map_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(map_next);
  auto &obj = typeAs<OrderedMapIterObject>(LOCAL(0));
  if (obj.hasNext()) {
    RET(obj.next(ctx.typePool));
  } else {
    RET_VOID;
  }
}

// #######################
// ##     Throwable     ##
// #######################

//!bind: function $OP_INIT($this : Throwable, $message : String, $status : Option<Int>) : Throwable
ARSH_METHOD error_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_init);
  auto &type = ctx.typePool.get(LOCAL(0).getTypeID());
  auto &v = LOCAL(2);
  const int64_t status = v.isInvalid() ? 1 : v.asInt();
  RET(Value(ErrorObject::newError(ctx, type, EXTRACT_LOCAL(1), status)));
}

//!bind: function message($this : Throwable) : String
ARSH_METHOD error_message(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_message);
  RET(typeAs<ErrorObject>(LOCAL(0)).getMessage());
}

//!bind: function show($this : Throwable) : Void
ARSH_METHOD error_show(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_show);
  typeAs<ErrorObject>(LOCAL(0)).printStackTrace(ctx);
  RET_VOID;
}

//!bind: function name($this : Throwable) : String
ARSH_METHOD error_name(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_name);
  RET(typeAs<ErrorObject>(LOCAL(0)).getName());
}

//!bind: function status($this : Throwable) : Int
ARSH_METHOD error_status(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_status);
  auto status = typeAs<ErrorObject>(LOCAL(0)).getStatus();
  RET(Value::createInt(status));
}

//!bind: function lineno($this : Throwable) : Int
ARSH_METHOD error_lineno(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_lineno);
  auto &stackTraces = typeAs<ErrorObject>(LOCAL(0)).getStackTrace();
  unsigned int lineNum = getOccurredLineNum(stackTraces);
  RET(Value::createInt(lineNum));
}

//!bind: function source($this : Throwable) : String
ARSH_METHOD error_source(RuntimeContext &ctx) {
  SUPPRESS_WARNING(error_source);
  auto &stackTraces = typeAs<ErrorObject>(LOCAL(0)).getStackTrace();
  const char *source = getOccurredSourceName(stackTraces);
  RET(Value::createStr(source));
}

// ################
// ##     FD     ##
// ################

//!bind: function $OP_INIT($this : FD, $path : String) : FD
ARSH_METHOD fd_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_init);
  auto ref = LOCAL(1).asStrRef();
  if (ref.hasNullChar()) {
    raiseError(ctx, TYPE::ArgumentError, ERROR_NULL_CHAR_PATH);
    RET_ERROR;
  }

  errno = 0;
  int fd = open(ref.data(), O_CREAT | O_RDWR, 0666);
  if (fd != -1 && remapFDCloseOnExec(fd)) {
    RET(Value::create<UnixFdObject>(fd));
  }
  int e = errno;
  std::string msg = "open failed: ";
  appendAsPrintable(ref, SYS_LIMIT_ERROR_MSG_MAX, msg);
  raiseSystemError(ctx, e, std::move(msg));
  RET_ERROR;
}

//!bind: function close($this : FD) : Void
ARSH_METHOD fd_close(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_close);
  auto &fdObj = typeAs<UnixFdObject>(LOCAL(0));
  int fd = fdObj.getRawFd();
  if (unlikely(fdObj.tryToClose(true) < 0)) {
    int e = errno;
    raiseSystemError(ctx, e, std::to_string(fd));
    RET_ERROR;
  }
  RET_VOID;
}

//!bind: function dup($this : FD) : FD
ARSH_METHOD fd_dup(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_dup);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getRawFd();
  int newFd = dupFDCloseOnExec(fd);
  if (unlikely(newFd < 0)) {
    int e = errno;
    raiseSystemError(ctx, e, std::to_string(fd));
    RET_ERROR;
  }
  RET(Value::create<UnixFdObject>(newFd));
}

//!bind: function value($this : FD) : Int
ARSH_METHOD fd_value(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_value);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getRawFd();
  RET(Value::createInt(fd));
}

//!bind: function lock($this : FD) : FD
ARSH_METHOD fd_lock(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_lock);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getRawFd();
  if (unlikely(flock(fd, LOCK_EX) == -1)) {
    raiseSystemError(ctx, errno, "lock failed");
    RET_ERROR;
  }
  RET(LOCAL(0));
}

//!bind: function unlock($this : FD) : FD
ARSH_METHOD fd_unlock(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_unlock);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getRawFd();
  if (unlikely(flock(fd, LOCK_UN) == -1)) {
    raiseSystemError(ctx, errno, "unlock failed");
    RET_ERROR;
  }
  RET(LOCAL(0));
}

//!bind: function cloexec($this: FD, $set: Option<Bool>) : Void
ARSH_METHOD fd_cloexec(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_cloexec);
  auto &fdObj = typeAs<UnixFdObject>(LOCAL(0));
  const bool set = LOCAL(1).isInvalid() ? true : LOCAL(1).asBool();
  if (!fdObj.closeOnExec(set)) {
    raiseSystemError(ctx, errno, "change of close-on-exec flag failed");
    RET_ERROR;
  }
  RET_VOID;
}

//!bind: function $OP_BOOL($this : FD) : Bool
ARSH_METHOD fd_bool(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_bool);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getRawFd();
  RET_BOOL(fd != -1);
}

//!bind: function $OP_NOT($this : FD) : Bool
ARSH_METHOD fd_not(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_not);
  int fd = typeAs<UnixFdObject>(LOCAL(0)).getRawFd();
  RET_BOOL(fd == -1);
}

//!bind: function $OP_ITER($this : FD) : Reader
ARSH_METHOD fd_iter(RuntimeContext &ctx) {
  SUPPRESS_WARNING(fd_iter);
  auto &v = LOCAL(0);
  RET(Value::create<ReaderObject>(toObjPtr<UnixFdObject>(v)));
}

// ####################
// ##     Reader     ##
// ####################

//!bind: function $OP_NEXT($this : Reader) : String
ARSH_METHOD reader_next(RuntimeContext &ctx) {
  SUPPRESS_WARNING(reader_next);
  auto &reader = typeAs<ReaderObject>(LOCAL(0));
  if (reader.nextLine(ctx)) {
    RET(reader.takeLine());
  } else { // may have error
    RET_VOID;
  }
}

// #####################
// ##     Command     ##
// #####################

//!bind: function call($this : Command, $argv : Array<String>) : Bool
ARSH_METHOD cmd_call(RuntimeContext &ctx) {
  SUPPRESS_WARNING(cmd_call);
  (void)ctx;
  RET_VOID; // dummy
}

// ########################
// ##     LineEditor     ##
// ########################

#define CHECK_EDITOR_LOCK(editor)                                                                  \
  do {                                                                                             \
    if (unlikely((editor).locked())) {                                                             \
      raiseInvalidOperationError(ctx, "cannot modify LineEditor object during line editing");      \
      RET_ERROR;                                                                                   \
    }                                                                                              \
  } while (false)

//!bind: function $OP_INIT($this : LineEditor) : LineEditor
ARSH_METHOD edit_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_init);
  auto ret = Value::create<LineEditorObject>(ctx);
  (void)ctx;
  RET(ret);
}

//!bind: function readLine($this : LineEditor, $p : Option<String>) : Option<String>
ARSH_METHOD edit_read(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_read);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  auto &p = LOCAL(1);
  std::string buf;
  buf.resize(SYS_LIMIT_READLINE_INPUT_SIZE);
  auto readSize = editor.readline(ctx, p.isInvalid() ? "> " : p.asStrRef(), buf.data(), buf.size());
  if (readSize > -1) {
    buf.resize(readSize);
    buf.shrink_to_fit();
    auto ret = Value::createStr(std::move(buf));
    RET(ret);
  } else if (ctx.hasError()) {
    RET_ERROR;
  } else if (errno == EAGAIN || errno == 0) {
    RET(Value::createInvalid());
  } else {
    raiseSystemError(ctx, errno, ERROR_READLINE);
    RET_ERROR;
  }
}

//!bind: function setCompletion($this : LineEditor, $comp : Option<Func<Candidates,[Module,String]>>) : Void
ARSH_METHOD edit_comp(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_comp);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  ObjPtr<Object> callback;
  if (!LOCAL(1).isInvalid()) {
    callback = LOCAL(1).toPtr();
  }
  editor.setCompletionCallback(std::move(callback));
  RET_VOID;
}

//!bind: function setPrompt($this : LineEditor, $prompt : Option<Func<String,[String]>>) : Void
ARSH_METHOD edit_prompt(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_prompt);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  ObjPtr<Object> callback;
  if (!LOCAL(1).isInvalid()) {
    callback = LOCAL(1).toPtr();
  }
  editor.setPromptCallback(std::move(callback));
  RET_VOID;
}

//!bind: function setHistory($this : LineEditor, $hist : Option<Array<String>>) : Void
ARSH_METHOD edit_hist(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_hist);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  ObjPtr<ArrayObject> hist;
  if (!LOCAL(1).isInvalid()) {
    hist = toObjPtr<ArrayObject>(LOCAL(1));
  }
  editor.setHistory(std::move(hist));
  RET_VOID;
}

//!bind: function setHistSync($this : LineEditor, $sync : Option<Func<Void,[String,Array<String>]>>) : Void
ARSH_METHOD edit_histSync(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_histSync);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  ObjPtr<Object> callback;
  if (!LOCAL(1).isInvalid()) {
    callback = LOCAL(1).toPtr();
  }
  editor.setHistSyncCallback(std::move(callback));
  RET_VOID;
}

//!bind: function bind($this : LineEditor, $key : String, $action : Option<String>) : Void
ARSH_METHOD edit_bind(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_bind);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  auto key = LOCAL(1).asStrRef();
  auto v = LOCAL(2);
  editor.addKeyBind(ctx, key, v.isInvalid() ? "" : v.asStrRef());
  RET_VOID;
}

//!bind: function bindings($this : LineEditor) : Map<String,String>
ARSH_METHOD edit_bindings(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_bindings);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  auto &stringType = ctx.typePool.get(TYPE::String);
  auto ret = ctx.typePool.createMapType(stringType, stringType);
  assert(ret);
  auto &mapType = cast<MapType>(*ret.asOk());
  auto value = Value::create<OrderedMapObject>(mapType, ctx.getRng().next());
  editor.getKeyBindings().fillBindings([&value](StringRef key, StringRef action) {
    typeAs<OrderedMapObject>(value).insert(Value::createStr(key), Value::createStr(action));
  });
  RET(value);
}

//!bind: function action($this : LineEditor, $name : String, $type : String, $action : Option<Func<Option<String>,[String, Option<Array<String>>]>>) : Void
ARSH_METHOD edit_action(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_action);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  auto name = LOCAL(1).asStrRef();
  auto type = LOCAL(2).asStrRef();
  ObjPtr<Object> callback;
  if (!LOCAL(3).isInvalid()) {
    callback = LOCAL(3).toPtr();
  }
  editor.defineCustomAction(ctx, name, type, std::move(callback));
  RET_VOID;
}

//!bind: function actions($this : LineEditor) : Array<String>
ARSH_METHOD edit_actions(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_actions);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  auto value = Value::create<ArrayObject>(ctx.typePool.get(TYPE::StringArray));
  auto &array = typeAs<ArrayObject>(value);
  editor.getKeyBindings().fillActions([&array](StringRef action) {
    array.append(Value::createStr(action)); // not check iterator invalidation
  });
  array.sortAsStrArray(); // not check iterator invalidation
  ASSERT_ARRAY_SIZE(array);
  RET(value);
}

//!bind: function config($this : LineEditor, $name : String, $value : Any) : Void
ARSH_METHOD edit_config(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_config);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  CHECK_EDITOR_LOCK(editor);
  auto name = LOCAL(1).asStrRef();
  auto &value = LOCAL(2);
  editor.setConfig(ctx, name, value);
  RET_VOID;
}

//!bind: function configs($this : LineEditor) : Map<String, Any>
ARSH_METHOD edit_configs(RuntimeContext &ctx) {
  SUPPRESS_WARNING(edit_configs);
  auto &editor = typeAs<LineEditorObject>(LOCAL(0));
  RET(editor.getConfigs(ctx));
}

//!bind: function name($this : CLI) : String
ARSH_METHOD cli_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(cli_get);
  auto &obj = typeAs<BaseObject>(LOCAL(0));
  auto v = obj[0];
  RET(v);
}

//!bind: function setName($this : CLI, $arg0 : String) : Void
ARSH_METHOD cli_set(RuntimeContext &ctx) {
  SUPPRESS_WARNING(cli_set);
  auto &obj = typeAs<BaseObject>(LOCAL(0));
  obj[0] = LOCAL(1);
  RET_VOID;
}

//!bind: function parse($this : CLI, $args : Array<String>) : Int
ARSH_METHOD cli_parse(RuntimeContext &ctx) {
  SUPPRESS_WARNING(cli_parse);
  auto &obj = typeAs<BaseObject>(LOCAL(0));
  auto &args = typeAs<ArrayObject>(LOCAL(1));
  if (auto ret = parseCommandLine(ctx, args, obj)) {
    RET(Value::createInt(ret.index));
  }
  RET_ERROR;
}

//!bind: function usage($this : CLI, $message : Option<String>, $verbose : Option<Bool>) : String
ARSH_METHOD cli_usage(RuntimeContext &ctx) {
  SUPPRESS_WARNING(cli_usage);
  auto &obj = typeAs<BaseObject>(LOCAL(0));
  StringRef message = LOCAL(1).isInvalid() ? "" : LOCAL(1).asStrRef();
  const bool verbose = LOCAL(2).isInvalid() ? true : LOCAL(2).asBool();
  auto &type = cast<CLIRecordType>(ctx.typePool.get(obj.getTypeID()));
  const auto parser = createArgParser(obj[0].asStrRef(), type);
  if (auto value = parser.formatUsage(message, verbose); value.hasValue()) {
    RET(Value::createStr(std::move(value.unwrap())));
  }
  raiseStringLimit(ctx);
  RET_ERROR;
}

// #################
// ##     Job     ##
// #################

//!bind: function in($this : Job) : FD
ARSH_METHOD job_in(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_in);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  RET(obj.getInObj());
}

//!bind: function out($this : Job) : FD
ARSH_METHOD job_out(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_out);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  RET(obj.getOutObj());
}

//!bind: function $OP_GET($this : Job, $index : Int) : FD
ARSH_METHOD job_get(RuntimeContext &ctx) {
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

//!bind: function poll($this : Job) : Bool
ARSH_METHOD job_poll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_poll);
  auto job = toObjPtr<JobObject>(LOCAL(0));
  ctx.jobTable.waitForJob(job, WaitOp::NONBLOCKING);
  RET_BOOL(job->isAvailable());
}

//!bind: function wait($this : Job) : Int
ARSH_METHOD job_wait(RuntimeContext &ctx) {
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
  RET(Value::createInt(s));
}

//!bind: function kill($this : Job, $s : Signal) : Void
ARSH_METHOD job_kill(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_kill);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  const bool r = obj.send(LOCAL(1).asSig());
  if (!r) {
    raiseSystemError(ctx, errno, "signal sending failed");
  }
  ctx.jobTable.waitForAny(); // update state of killed processes
  if (!r) {
    RET_ERROR;
  }
  RET_VOID;
}

//!bind: function disown($this : Job) : Void
ARSH_METHOD job_disown(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_disown);
  auto job = toObjPtr<JobObject>(LOCAL(0));
  job->disown();
  RET_VOID;
}

//!bind: function size($this : Job) : Int
ARSH_METHOD job_size(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_size);
  auto &obj = typeAs<JobObject>(LOCAL(0));
  RET(Value::createInt(obj.getProcSize()));
}

//!bind: function pid($this : Job, $index : Int) : Option<Int>
ARSH_METHOD job_pid(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_pid);
  auto &job = typeAs<JobObject>(LOCAL(0));
  auto index = LOCAL(1).asInt();

  if (index > -1 && static_cast<size_t>(index) < job.getProcSize()) {
    int pid = job.getValidPid(index);
    if (pid < 0 || !job.isAvailable()) {
      RET(Value::createInvalid());
    }
    RET(Value::createInt(pid));
  }
  std::string msg = "number of processes is: ";
  msg += std::to_string(job.getProcSize());
  msg += ", but index is: ";
  msg += std::to_string(index);
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

//!bind: function status($this : Job, $index : Int) : Option<Int>
ARSH_METHOD job_status(RuntimeContext &ctx) {
  SUPPRESS_WARNING(job_status);
  auto &job = typeAs<JobObject>(LOCAL(0));
  auto index = LOCAL(1).asInt();

  if (index > -1 && static_cast<size_t>(index) < job.getProcSize()) {
    auto &proc = job.getProcs()[index];
    if (!proc.is(Proc::State::RUNNING)) {
      RET(Value::createInt(proc.exitStatus()));
    } else {
      RET(Value::createInvalid());
    }
  }
  std::string msg = "number of processes is: ";
  msg += std::to_string(job.getProcSize());
  msg += ", but index is: ";
  msg += std::to_string(index);
  raiseOutOfRangeError(ctx, std::move(msg));
  RET_ERROR;
}

// ########################
// ##     Candidates     ##
// ########################

//!bind: function $OP_INIT($this : Candidates, $values: Option<Array<String>>) : Candidates
ARSH_METHOD candidates_init(RuntimeContext &ctx) {
  SUPPRESS_WARNING(candidates_init);
  CandidatesWrapper wrapper(ctx.typePool);
  if (const auto v = LOCAL(1); !v.isInvalid()) {
    const auto &values = typeAs<ArrayObject>(v).getValues();
    for (auto &e : values) {
      if (unlikely(!wrapper.addAsCandidate(ctx, e, false))) { // not insert space
        RET_ERROR;
      }
    }
  }
  RET(std::move(wrapper).take());
}

//!bind: function size($this : Candidates) : Int
ARSH_METHOD_DECL array_size(RuntimeContext &ctx);

//!bind: function $OP_GET($this : Candidates, $index: Int) : String
ARSH_METHOD candidates_get(RuntimeContext &ctx) {
  SUPPRESS_WARNING(candidates_get);
  CandidatesWrapper wrapper(toObjPtr<ArrayObject>(LOCAL(0)));
  const size_t size = wrapper.size();
  const auto index = LOCAL(1).asInt();
  const auto value = TRY(resolveIndex(ctx, index, size));
  RET(Value::createStr(wrapper.getCandidateAt(value.index)));
}

//!bind: function hasSpace($this: Candidates, $index: Int) : Bool
ARSH_METHOD candidates_space(RuntimeContext &ctx) {
  SUPPRESS_WARNING(candidates_space);
  CandidatesWrapper wrapper(toObjPtr<ArrayObject>(LOCAL(0)));
  const size_t size = wrapper.size();
  const auto index = LOCAL(1).asInt();
  const auto value = TRY(resolveIndex(ctx, index, size));
  RET_BOOL(wrapper.getAttrAt(value.index).needSpace);
}

//!bind: function add($this : Candidates, $can : String, $desc : Option<String>, $space : Option<Int>) : Candidates
ARSH_METHOD candidates_add(RuntimeContext &ctx) {
  SUPPRESS_WARNING(candidates_add);
  CandidatesWrapper wrapper(toObjPtr<ArrayObject>(LOCAL(0)));
  auto candidate = LOCAL(1);
  auto description = LOCAL(2);
  const auto num = LOCAL(3).isInvalid() ? -1 : LOCAL(3).asInt();
  const bool needSpace = num < 0 ? CompCandidate::needSuffixSpace(candidate.asStrRef(),
                                                                  CompCandidateKind::USER_SPECIFIED)
                                 : num > 0;
  if (!wrapper.addNewCandidate(ctx, std::move(candidate), std::move(description), needSpace)) {
    RET_ERROR;
  }
  RET(LOCAL(0));
}

//!bind: function addAll($this: Candidates, $other: Candidates) : Candidates
ARSH_METHOD candidates_addAll(RuntimeContext &ctx) {
  SUPPRESS_WARNING(candidates_addAll);
  CandidatesWrapper wrapper(toObjPtr<ArrayObject>(LOCAL(0)));
  if (!wrapper.addAll(ctx, typeAs<ArrayObject>(LOCAL(1)))) {
    RET_ERROR;
  }
  RET(LOCAL(0));
}

} // namespace arsh

#endif // ARSH_BUILTIN_H
