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

#include <cmath>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include <stdexcept>

#include <ydsh/ydsh.h>
#include "vm.h"
#include "signals.h"
#include "misc/unicode.hpp"
#include "misc/num_util.hpp"

// helper macro
#define LOCAL(index) (ctx.getLocal(index))
#define EXTRACT_LOCAL(index) (ctx.moveLocal(index))
#define RET(value) return value
#define RET_BOOL(value) return DSValue::createBool(value)
#define RET_VOID return DSValue()
#define RET_ERROR return DSValue()

#define SUPPRESS_WARNING(a) (void)a

#define YDSH_METHOD static inline DSValue
#define YDSH_METHOD_DECL DSValue

/**
 *   //!bind: function <method name>($this : <receiver type>, $param1 : <type1>, $param2? : <type2>, ...) : <return type>
 *   //!bind: constructor <type name>($param1 : <type1>, ....)
 *   $<param name>?  has default value
 */

namespace ydsh {

using RuntimeContext = DSState;

template <typename T>
using ObjTypeStub = typename std::conditional<
        std::is_same<double, T>::value,
        FloatObject,
        typename std::conditional<std::is_same<long, T>::value,
                LongObject, void>::type>::type;


// for binary op
#define EACH_BASIC_OP(OP) \
    OP(ADD, +) \
    OP(SUB, -) \
    OP(MUL, *) \
    OP(AND, &) \
    OP(OR,  |) \
    OP(XOR, ^)


#define EACH_UNARY_OP(OP) \
    OP(MINUS, -) \
    OP(NOT, ~)


#define EACH_COMPARE_OP(OP) \
    OP(EQ, ==) \
    OP(NE, !=) \
    OP(LT, <) \
    OP(GT, >) \
    OP(LE, <=) \
    OP(GE, >=)


#define GEN_BASIC_OP(NAME, OPERATOR) \
template <typename T> \
YDSH_METHOD basic_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue(); \
    T result = left OPERATOR right; \
    RET(DSValue::create<ObjType>(result)); \
}

#define GEN_UNARY_OP(NAME, OPERATOR) \
template <typename T> \
YDSH_METHOD unary_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto right = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    T result = OPERATOR right; \
    RET(DSValue::create<ObjType>(result)); \
}

#define GEN_COMPARE_OP(NAME, OPERATOR) \
template <typename T> \
bool compare_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue(); \
    return left OPERATOR right; \
}

EACH_BASIC_OP(GEN_BASIC_OP)

EACH_UNARY_OP(GEN_UNARY_OP)

EACH_COMPARE_OP(GEN_COMPARE_OP)


static inline bool checkZeroDiv(RuntimeContext &ctx, int right) {
    if(right == 0) {
        raiseError(ctx, TYPE::ArithmeticError, "zero division");
        return false;
    }
    return true;
}

static inline bool checkZeroMod(RuntimeContext &ctx, int right) {
    if(right == 0) {
        raiseError(ctx, TYPE::ArithmeticError, "zero modulo");
        return false;
    }
    return true;
}

template <typename T>
YDSH_METHOD basic_add(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();

    T ret;
    if(sadd_overflow(left, right, ret)) {
        raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
        RET_ERROR;
    }
    RET(DSValue::create<ObjType>(ret));
}

template <typename T>
YDSH_METHOD basic_sub(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();

    T ret;
    if(ssub_overflow(left, right, ret)) {
        raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
        RET_ERROR;
    }
    RET(DSValue::create<ObjType>(ret));
}

template <typename T>
YDSH_METHOD basic_mul(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();

    T ret;
    if(smul_overflow(left, right, ret)) {
        raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
        RET_ERROR;
    }
    RET(DSValue::create<ObjType>(ret));
}

template <typename T>
YDSH_METHOD basic_div(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();
    if(!checkZeroDiv(ctx, (int) right)) {
        RET_ERROR;
    }
    T result = left / right;
    RET(DSValue::create<ObjType>(result));
}

template <typename T>
YDSH_METHOD basic_mod(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();
    if(!checkZeroMod(ctx, (int) right)) {
        RET_ERROR;
    }
    T result = left % right;
    RET(DSValue::create<ObjType>(result));
}


// #################
// ##     Any     ##
// #################

//!bind: function $OP_STR($this : Any) : String
YDSH_METHOD to_str(RuntimeContext & ctx) {
    SUPPRESS_WARNING(to_str);
    bool hasRet = ctx.toStrBuf.empty();
    if(!LOCAL(0).opStr(ctx)) {
        ctx.toStrBuf.clear();
        RET_ERROR;
    }

    if(hasRet) {
        std::string value;
        std::swap(value, ctx.toStrBuf);
        RET(DSValue::createStr(std::move(value)));
    } else {
        RET(DSValue::createInvalid());  // dummy
    }
}

//!bind: function $OP_INTERP($this : Any) : String
YDSH_METHOD to_interp(RuntimeContext & ctx) {
    SUPPRESS_WARNING(to_interp);
    bool hasRet = ctx.toStrBuf.empty();
    if(!LOCAL(0).opInterp(ctx)) {
        ctx.toStrBuf.clear();
        RET_ERROR;
    }

    if(hasRet) {
        std::string value;
        std::swap(value, ctx.toStrBuf);
        RET(DSValue::createStr(std::move(value)));
    } else {
        RET(DSValue::createInvalid());  // dummy
    }
}

// ###################
// ##     Int32     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Int32) : Int32
YDSH_METHOD int_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int32) : Int32
YDSH_METHOD int_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_minus);
    int v = - LOCAL(0).asInt();
    RET(DSValue::createInt(v));
}

//!bind: function $OP_NOT($this : Int32) : Int32
YDSH_METHOD int_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_not);
    int v = ~ LOCAL(0).asInt();
    RET(DSValue::createInt(v));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_add);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();

    int ret;
    if(sadd_overflow(left, right, ret)) {
        raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
        RET_ERROR;
    }
    RET(DSValue::createInt(ret));
}

//!bind: function $OP_SUB($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_sub);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();

    int ret;
    if(ssub_overflow(left, right, ret)) {
        raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
        RET_ERROR;
    }
    RET(DSValue::createInt(ret));
}

//!bind: function $OP_MUL($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mul);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();

    int ret;
    if(smul_overflow(left, right, ret)) {
        raiseError(ctx, TYPE::ArithmeticError, "integer overflow");
        RET_ERROR;
    }
    RET(DSValue::createInt(ret));
}

//!bind: function $OP_DIV($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_div);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();

    if(!checkZeroDiv(ctx, right)) {
        RET_ERROR;
    }
    RET(DSValue::createInt(left / right));
}

//!bind: function $OP_MOD($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mod);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();

    if(!checkZeroMod(ctx, right)) {
        RET_ERROR;
    }
    RET(DSValue::createInt(left % right));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_eq);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET_BOOL(left == right);
}

//!bind: function $OP_NE($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ne);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET_BOOL(left != right);
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_lt);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET_BOOL(left < right);
}

//!bind: function $OP_GT($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_gt);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET_BOOL(left > right);
}

//!bind: function $OP_LE($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_le);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET_BOOL(left <= right);
}

//!bind: function $OP_GE($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ge);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET_BOOL(left >= right);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_and);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET(DSValue::createInt(left & right));
}

//!bind: function $OP_OR($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_or);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET(DSValue::createInt(left | right));
}

//!bind: function $OP_XOR($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_xor);
    int left = LOCAL(0).asInt();
    int right = LOCAL(1).asInt();
    RET(DSValue::createInt(left ^ right));
}


// ###################
// ##     Int64     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Int64) : Int64
YDSH_METHOD int64_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int64) : Int64
YDSH_METHOD int64_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_minus);
    RET(unary_MINUS<long>(ctx));
}

//!bind: function $OP_NOT($this : Int64) : Int64
YDSH_METHOD int64_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_not);
    RET(unary_NOT<long>(ctx));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_add);
    RET(basic_add<long>(ctx));
}

//!bind: function $OP_SUB($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_sub);
    RET(basic_sub<long>(ctx));
}

//!bind: function $OP_MUL($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mul);
    RET(basic_mul<long>(ctx));
}

//!bind: function $OP_DIV($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_div);
    RET(basic_div<long>(ctx));
}

//!bind: function $OP_MOD($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mod);
    RET(basic_mod<long>(ctx));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_eq);
    RET_BOOL(compare_EQ<long>(ctx));
}

//!bind: function $OP_NE($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ne);
    RET_BOOL(compare_NE<long>(ctx));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_lt);
    RET_BOOL(compare_LT<long>(ctx));
}

//!bind: function $OP_GT($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_gt);
    RET_BOOL(compare_GT<long>(ctx));
}

//!bind: function $OP_LE($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_le);
    RET_BOOL(compare_LE<long>(ctx));
}

//!bind: function $OP_GE($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ge);
    RET_BOOL(compare_GE<long>(ctx));
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_and);
    RET(basic_AND<long>(ctx));
}

//!bind: function $OP_OR($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_or);
    RET(basic_OR<long>(ctx));
}

//!bind: function $OP_XOR($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_xor);
    RET(basic_XOR<long>(ctx));
}


// ###################
// ##     Float     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Float) : Float
YDSH_METHOD float_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Float) : Float
YDSH_METHOD float_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_minus);
    RET(unary_MINUS<double>(ctx));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_add);
    RET(basic_ADD<double>(ctx));
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_sub);
    RET(basic_SUB<double>(ctx));
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_mul);
    RET(basic_MUL<double>(ctx));
}

//!bind: function $OP_DIV($this : Float, $target : Float) : Float
YDSH_METHOD float_2_float_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_div);
    double left = typeAs<FloatObject>(LOCAL(0))->getValue();
    double right = typeAs<FloatObject>(LOCAL(1))->getValue();
    double value = left / right;
    RET(DSValue::create<FloatObject>(value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_eq);
    RET_BOOL(compare_EQ<double>(ctx));
}

//!bind: function $OP_NE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ne);
    RET_BOOL(compare_NE<double>(ctx));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_lt);
    RET_BOOL(compare_LT<double >(ctx));
}

//!bind: function $OP_GT($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_gt);
    RET_BOOL(compare_GT<double>(ctx));
}

//!bind: function $OP_LE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_le);
    RET_BOOL(compare_LE<double>(ctx));
}

//!bind: function $OP_GE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ge);
    RET_BOOL(compare_GE<double>(ctx));
}

// =====  additional float op  ======

//!bind: function isNan($this : Float): Boolean
YDSH_METHOD float_isNan(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isNan);
    double value = typeAs<FloatObject>(LOCAL(0))->getValue();
    RET_BOOL(std::isnan(value));
}

//!bind: function isInf($this : Float): Boolean
YDSH_METHOD float_isInf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isInf);
    double value = typeAs<FloatObject>(LOCAL(0))->getValue();
    RET_BOOL(std::isinf(value));
}

//!bind: function isFinite($this : Float): Boolean
YDSH_METHOD float_isFinite(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isFinite);
    double value = typeAs<FloatObject>(LOCAL(0))->getValue();
    RET_BOOL(std::isfinite(value));
}


// #####################
// ##     Boolean     ##
// #####################

//!bind: function $OP_NOT($this : Boolean) : Boolean
YDSH_METHOD boolean_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_not);
    RET_BOOL(!LOCAL(0).asBool());
}

//!bind: function $OP_EQ($this : Boolean, $target : Boolean) : Boolean
YDSH_METHOD boolean_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_eq);
    RET_BOOL(LOCAL(0).asBool() == LOCAL(1).asBool());
}

//!bind: function $OP_NE($this : Boolean, $target : Boolean) : Boolean
YDSH_METHOD boolean_ne(RuntimeContext & ctx) {
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

//!bind: function size($this : String) : Int32
YDSH_METHOD string_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_size);
    auto size = LOCAL(0).asStrRef().size();
    assert(size <= ArrayObject::MAX_SIZE);
    RET(DSValue::createInt(static_cast<int>(size)));
}

//!bind: function empty($this : String) : Boolean
YDSH_METHOD string_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_empty);
    bool empty = LOCAL(0).asStrRef().empty();
    RET_BOOL(empty);
}

//!bind: function count($this : String) : Int32
YDSH_METHOD string_count(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_count);
    auto ref = LOCAL(0).asStrRef();
    const char *ptr = ref.data();
    unsigned int size = ref.size();
    unsigned int count = 0;
    for(unsigned int i = 0; i < size; i = UnicodeUtil::utf8NextPos(i, ptr[i])) {
        count++;
    }
    RET(DSValue::createInt(count));
}

/**
 * return always false.
 */
static void raiseOutOfRangeError(RuntimeContext &ctx, std::string &&message) {
    raiseError(ctx, TYPE::OutOfRangeError, std::move(message));
}

//!bind: function $OP_GET($this : String, $index : Int32) : String
YDSH_METHOD string_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_get);
    auto ref = LOCAL(0).asStrRef();
    const unsigned int size = ref.size();
    const int index = LOCAL(1).asInt();

    if(index > -1 && static_cast<unsigned int>(index) < size) {
        RET(DSValue::createStr(ref.substr(index, 1)));
    }

    std::string msg("size is ");
    msg += std::to_string(size);
    msg += ", but index is ";
    msg += std::to_string(index);
    raiseOutOfRangeError(ctx, std::move(msg));
    RET_ERROR;
}

//!bind: function charAt($this : String, $index : Int32) : String
YDSH_METHOD string_charAt(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_charAt);
    auto ref = LOCAL(0).asStrRef();
    const int pos = LOCAL(1).asInt();
    const unsigned int size = ref.size();

    if(pos >= 0 && static_cast<unsigned int>(pos) < size) {
        const unsigned int limit = pos;
        unsigned int index = 0;
        unsigned int count = 0;
        for(; index < size; index = UnicodeUtil::utf8NextPos(index, ref[index])) {
            if(count == limit) {
                break;
            }
            count++;
        }
        if(count == limit && index < size) {
            unsigned int nextIndex = UnicodeUtil::utf8NextPos(index, ref[index]);
            RET(DSValue::createStr(ref.slice(index, nextIndex)));
        }
    }

    std::string msg("size is ");
    msg += std::to_string(size);
    msg += ", but code position is ";
    msg += std::to_string(pos);
    raiseOutOfRangeError(ctx, std::move(msg));
    RET_ERROR;
}

static auto sliceImpl(const ArrayObject &obj, unsigned int begin, unsigned int end) {
    auto b = obj.getValues().begin() + begin;
    auto e = obj.getValues().begin() + end;
    return DSValue::create<ArrayObject>(obj.getTypeID(), std::vector<DSValue>(b, e));
}

static auto sliceImpl(const StringRef &ref, unsigned int begin, unsigned int end) {
    return DSValue::createStr(ref.slice(begin, end));
}

/**
 *
 * @tparam T
 * @param ctx
 * @param obj
 * @param startIndex
 * inclusive
 * @param stopIndex
 * exclusive
 * @return
 */
template <typename T>
static auto slice(RuntimeContext &ctx, const T &obj, int startIndex, int stopIndex) {
    const unsigned int size = obj.size();

    // resolve actual index
    startIndex = (startIndex < 0 ? size : 0) + startIndex;
    stopIndex = (stopIndex < 0 ? size : 0) + stopIndex;

    // check range
    if(startIndex > stopIndex || startIndex < 0 || startIndex > static_cast<int>(size) ||
       stopIndex < 0 || stopIndex > static_cast<int>(size)) {
        std::string msg("size is ");
        msg += std::to_string(size);
        msg += ", but range is [";
        msg += std::to_string(startIndex);
        msg += ", ";
        msg += std::to_string(stopIndex);
        msg += ")";
        raiseOutOfRangeError(ctx, std::move(msg));
        RET_ERROR;
    }
    RET(sliceImpl(obj, static_cast<unsigned int>(startIndex), static_cast<unsigned int>(stopIndex)));
}

//!bind: function slice($this : String, $start : Int32, $stop : Int32) : String
YDSH_METHOD string_slice(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_slice);
    RET(slice(ctx, LOCAL(0).asStrRef(), LOCAL(1).asInt(), LOCAL(2).asInt()));
}

//!bind: function from($this : String, $start : Int32) : String
YDSH_METHOD string_sliceFrom(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_sliceFrom);
    auto strObj = LOCAL(0).asStrRef();
    RET(slice(ctx, strObj, LOCAL(1).asInt(), strObj.size()));
}

//!bind: function to($this : String, $stop : Int32) : String
YDSH_METHOD string_sliceTo(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_sliceTo);
    auto strObj = LOCAL(0).asStrRef();
    RET(slice(ctx, strObj, 0, LOCAL(1).asInt()));
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

//!bind: function indexOf($this : String, $target : String) : Int32
YDSH_METHOD string_indexOf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_indexOf);
    auto left = LOCAL(0).asStrRef();
    auto right = LOCAL(1).asStrRef();
    auto index = left.indexOf(right);
    assert(index == StringRef::npos || index <= StringObject::MAX_SIZE);
    RET(DSValue::createInt(static_cast<int>(index)));
}

//!bind: function lastIndexOf($this : String, $target : String) : Int32
YDSH_METHOD string_lastIndexOf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lastIndexOf);
    auto left = LOCAL(0).asStrRef();
    auto right = LOCAL(1).asStrRef();
    auto index = left.lastIndexOf(right);
    assert(index == StringRef::npos || index <= StringObject::MAX_SIZE);
    RET(DSValue::createInt(static_cast<int>(index)));
}

//!bind: function split($this : String, $delim : String) : Array<String>
YDSH_METHOD string_split(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_split);
    auto results = DSValue::create<ArrayObject>(ctx.symbolTable.get(TYPE::StringArray));
    auto ptr = typeAs<ArrayObject>(results);

    auto thisStr = LOCAL(0).asStrRef();
    auto delimStr = LOCAL(1).asStrRef();

    if(delimStr.empty()) {
        ptr->append(LOCAL(0));
    } else {
        for(StringRef::size_type pos = 0; pos != StringRef::npos; ) {
            auto ret = thisStr.find(delimStr, pos);
            ptr->append(DSValue::createStr(thisStr.slice(pos, ret)));
            pos = ret != StringRef::npos ? ret + delimStr.size() : ret;
        }
    }
    RET(results);
}

//!bind: function replace($this : String, $target : String, $rep : String) : String
YDSH_METHOD string_replace(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_replace);

    auto delimStr = LOCAL(1).asStrRef();
    if(delimStr.empty()) {
        RET(LOCAL(0));
    }

    auto thisStr = LOCAL(0).asStrRef();
    auto repStr = LOCAL(2).asStrRef();
    std::string buf;

    for(StringRef::size_type pos = 0; pos != StringRef::npos; ) {
        auto ret = thisStr.find(delimStr, pos);
        auto value = thisStr.slice(pos, ret);
        buf.append(value.data(), value.size());
        if(ret != StringRef::npos) {
            buf.append(repStr.data(), repStr.size());
            pos = ret + delimStr.size();
        } else {
            pos = ret;
        }
    }
    RET(DSValue::createStr(std::move(buf)));
}


//!bind: function toInt32($this : String) : Option<Int32>
YDSH_METHOD string_toInt32(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toInt32);
    auto ref = LOCAL(0).asStrRef();
    auto ret = fromIntLiteral<int32_t>(ref.begin(), ref.end());

    RET(ret.second ? DSValue::createInt(ret.first) : DSValue::createInvalid());
}

//!bind: function toInt64($this : String) : Option<Int64>
YDSH_METHOD string_toInt64(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toInt64);
    auto ref = LOCAL(0).asStrRef();
    auto ret = fromIntLiteral<int64_t>(ref.begin(), ref.end());

    RET(ret.second ? DSValue::create<LongObject>(static_cast<long>(ret.first))
            : DSValue::createInvalid());
}

//!bind: function toFloat($this : String) : Option<Float>
YDSH_METHOD string_toFloat(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toFloat);
    auto ref = LOCAL(0).asStrRef();
    int status = 0;
    double value = convertToDouble(ref.data(), status, false);

    RET(status == 0 ? DSValue::create<FloatObject>(value) : DSValue::createInvalid());
}

//!bind: function $OP_ITER($this : String) : StringIter
YDSH_METHOD string_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_iter);

    /**
     * record StringIter {
     *      var ref : String
     *      var index : Int
     * }
     *
     */
     auto value = DSValue::create<BaseObject>(ctx.symbolTable.get(TYPE::StringIter), 2);
     auto &obj = *typeAs<BaseObject>(value);
     obj[0] = LOCAL(0);
     obj[1] = DSValue::createInt(0);
     RET(value);
}

//!bind: function $OP_MATCH($this : String, $re : Regex) : Boolean
YDSH_METHOD string_match(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_match);
    auto str = LOCAL(0).asStrRef();
    auto *re = typeAs<RegexObject>(LOCAL(1));
    bool r = re->search(str);
    RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : String, $re : Regex) : Boolean
YDSH_METHOD string_unmatch(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_unmatch);
    auto str = LOCAL(0).asStrRef();
    auto *re = typeAs<RegexObject>(LOCAL(1));
    bool r = !re->search(str);
    RET_BOOL(r);
}

//!bind: function realpath($this : String) : Option<String>
YDSH_METHOD string_realpath(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_realpath);
    auto ref = LOCAL(0).asStrRef();
    std::string str = ref.data();
    expandTilde(str);
    char *buf = realpath(str.c_str(), nullptr);
    if(buf == nullptr) {
        RET(DSValue::createInvalid());
    }

    auto ret = DSValue::createStr(buf);
    free(buf);
    RET(ret);
}

//!bind: function lower($this : String) : String
YDSH_METHOD string_lower(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lower);
    std::string str = LOCAL(0).asStrRef().toString();
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    RET(DSValue::createStr(std::move(str)));
}

//!bind: function upper($this : String) : String
YDSH_METHOD string_upper(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_upper);
    std::string str = LOCAL(0).asStrRef().toString();
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    RET(DSValue::createStr(std::move(str)));
}

// ########################
// ##     StringIter     ##
// ########################

//!bind: function $OP_NEXT($this : StringIter) : String
YDSH_METHOD stringIter_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(stringIter_next);
    auto &iter = *typeAs<BaseObject>(LOCAL(0));
    auto ref = iter[0].asStrRef();
    assert(iter[1].asInt() > -1);
    unsigned int curIndex = iter[1].asInt();
    if(curIndex >= ref.size()) {
        raiseOutOfRangeError(ctx, std::string("string iterator reach end of string"));
        RET_ERROR;
    }
    unsigned int newIndex = UnicodeUtil::utf8NextPos(curIndex, ref[curIndex]);
    if(newIndex > ref.size()) {
        fatal("broken string iterator\n");
    }

    unsigned int size = newIndex - curIndex;
    iter[1] = DSValue::createInt(newIndex);
    RET(DSValue::createStr(ref.substr(curIndex, size)));
}

//!bind: function $OP_HAS_NEXT($this : StringIter) : Boolean
YDSH_METHOD stringIter_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(stringIter_hasNext);
    auto &obj = *typeAs<BaseObject>(LOCAL(0));
    auto ref = obj[0].asStrRef();
    assert(obj[1].asInt() > -1);
    unsigned int index = obj[1].asInt();
    RET_BOOL(index < ref.size());
}

// ###################
// ##     Regex     ##
// ###################

//!bind: function $OP_INIT($this : Regex, $str : String) : Regex
YDSH_METHOD regex_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_init);
    auto ref = LOCAL(1).asStrRef();
    const char *errorStr;
    auto re = compileRegex(ref.data(), errorStr, 0);
    if(!re) {
        raiseError(ctx, TYPE::RegexSyntaxError, std::string(errorStr));
        RET_ERROR;
    }
    RET(DSValue::create<RegexObject>(ref.data(), std::move(re)));
}

//!bind: function $OP_MATCH($this : Regex, $target : String) : Boolean
YDSH_METHOD regex_search(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_search);
    auto *re = typeAs<RegexObject>(LOCAL(0));
    auto ref = LOCAL(1).asStrRef();
    bool r = re->search(ref);
    RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : Regex, $target : String) : Boolean
YDSH_METHOD regex_unmatch(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_unmatch);
    auto *re = typeAs<RegexObject>(LOCAL(0));
    auto ref = LOCAL(1).asStrRef();
    bool r = !re->search(ref);
    RET_BOOL(r);
}

//!bind: function match($this: Regex, $target : String) : Array<String>
YDSH_METHOD regex_match(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_match);
    auto *re = typeAs<RegexObject>(LOCAL(0));
    auto ref = LOCAL(1).asStrRef();

    FlexBuffer<int> ovec;
    int matchSize = re->match(ref, ovec);

    auto ret = DSValue::create<ArrayObject>(ctx.symbolTable.get(TYPE::StringArray));
    auto *array = typeAs<ArrayObject>(ret);

    if(matchSize > 0) {
        array->refValues().reserve(matchSize);
    }
    for(int i = 0; i < matchSize; i++) {
        unsigned int size = ovec[i * 2 + 1] - ovec[i * 2];
        auto v = size == 0 ? ctx.emptyStrObj : DSValue::createStr(ref.substr(ovec[i * 2], size));
        array->refValues().push_back(std::move(v));
    }

    RET(ret);
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

//!bind: function value($this : Signal) : Int32
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

//!bind: function kill($this : Signal, $pid : Int32) : Void
YDSH_METHOD signal_kill(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signal_kill);
    int sigNum = LOCAL(0).asSig();
    int pid = LOCAL(1).asInt();
    if(kill(pid, sigNum) != 0) {
        int num = errno;
        std::string str = getSignalName(sigNum);
        raiseSystemError(ctx, num, std::move(str));
        RET_ERROR;
    }
    RET_VOID;
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

//!bind: function $OP_GET($this : Signals, $s : Signal) : Func<Void,[Signal]>
YDSH_METHOD signals_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_get);
    int sigNum = LOCAL(1).asSig();
    RET(getSignalHandler(ctx, sigNum));
}

//!bind: function $OP_SET($this : Signals, $s : Signal, $action : Func<Void,[Signal]>) : Void
YDSH_METHOD signals_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_set);
    installSignalHandler(ctx, LOCAL(1).asSig(), LOCAL(2));
    RET_VOID;
}

//!bind: function signal($this : Signals, $key : String) : Option<Signal>
YDSH_METHOD signals_signal(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_signal);
    const char *key = LOCAL(1).asStrRef().data();
    int sigNum = getSignalNum(key);
    if(sigNum < 0) {
        RET(DSValue::createInvalid());
    }
    RET(DSValue::createSig(sigNum));
}

//!bind: function list($this : Signals) : Array<Signal>
YDSH_METHOD signals_list(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_list);

    auto ret = ctx.symbolTable.createReifiedType(ctx.symbolTable.getArrayTemplate(), {&ctx.symbolTable.get(TYPE::Signal)});
    assert(ret);
    auto type = ret.take();
    auto v = DSValue::create<ArrayObject>(*type);
    auto *array = typeAs<ArrayObject>(v);
    for(auto &e : getUniqueSignalList()) {
        array->append(DSValue::createSig(e));
    }
    RET(v);
}


// ###################
// ##     Array     ##
// ###################

#define TRY(E) ({ auto value = E; if(!value) { RET_ERROR; } std::forward<decltype(value)>(value); })

struct ArrayIndex {
    unsigned int index;
    bool s;

    explicit operator bool() const {
        return this->s;
    }
};

// check index range and get resolved index
static ArrayIndex resolveIndex(int index, int size) {
    index += (index < 0 ? size : 0);
    bool s = index > -1 && index < size;
    return {static_cast<unsigned int>(index), s};
}

static ArrayIndex resolveIndex(RuntimeContext &ctx, int index, int size) {
    auto ret = resolveIndex(index, size);
    if(!ret) {
        std::string message("size is ");
        message += std::to_string(size);
        message += ", but index is ";
        message += std::to_string(index);
        raiseOutOfRangeError(ctx, std::move(message));
    }
    return ret;
}

//!bind: function $OP_GET($this : Array<T0>, $index : Int32) : T0
YDSH_METHOD array_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_get);

    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    int size = obj->getValues().size();
    int index = LOCAL(1).asInt();
    auto ret = TRY(resolveIndex(ctx, index, size));
    RET(obj->getValues()[ret.index]);
}

//!bind: function get($this : Array<T0>, $index : Int32) : Option<T0>
YDSH_METHOD array_get2(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_get);

    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    int size = obj->getValues().size();
    int index = LOCAL(1).asInt();
    auto ret = resolveIndex(index, size);
    if(!ret) {
        RET(DSValue::createInvalid());
    }
    RET(obj->getValues()[ret.index]);
}

//!bind: function $OP_SET($this : Array<T0>, $index : Int32, $value : T0) : Void
YDSH_METHOD array_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_set);

    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    int size = obj->getValues().size();
    int index = LOCAL(1).asInt();
    auto ret = TRY(resolveIndex(ctx, index, size));
    obj->set(ret.index, EXTRACT_LOCAL(2));
    RET_VOID;
}

//!bind: function remove($this : Array<T0>, $index : Int32) : T0
YDSH_METHOD array_remove(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_remove);

    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    int size = obj->getValues().size();
    int index = LOCAL(1).asInt();
    auto ret = TRY(resolveIndex(ctx, index, size));
    auto v = obj->getValues()[ret.index];
    obj->refValues().erase(obj->refValues().begin() + ret.index);
    RET(v);
}

static bool array_fetch(RuntimeContext &ctx, DSValue &value, bool fetchLast = true) {
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    if(obj->getValues().empty()) {
        raiseOutOfRangeError(ctx, std::string("Array size is 0"));
        return false;
    }
    value = fetchLast ? obj->getValues().back() : obj->getValues().front();
    return true;
}

//!bind: function peek($this : Array<T0>) : T0
YDSH_METHOD array_peek(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_peek);
    DSValue value;
    array_fetch(ctx, value);
    return value;
}

static bool array_insertImpl(DSState &ctx, int index, const DSValue &v) {
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    auto size0 = obj->getValues().size();
    if(size0 == ArrayObject::MAX_SIZE) {
        raiseOutOfRangeError(ctx, std::string("reach Array size limit"));
        return false;
    }

    ArrayIndex ret{static_cast<unsigned int>(index), true};
    int size = static_cast<int>(size0);
    if(index != size && !(ret = resolveIndex(ctx, index, size))) {
        return false;
    }
    obj->refValues().insert(obj->refValues().begin() + ret.index, v);
    return true;
}

static bool array_pushImpl(RuntimeContext &ctx) {
    int index = typeAs<ArrayObject>(LOCAL(0))->getValues().size();
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
    typeAs<ArrayObject>(LOCAL(0))->refValues().pop_back();
    RET(v);
}

//!bind: function shift($this : Array<T0>) : T0
YDSH_METHOD array_shift(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_shift);
    DSValue v;
    TRY(array_fetch(ctx, v, false));
    auto &values = typeAs<ArrayObject>(LOCAL(0))->refValues();
    values.erase(values.begin());
    RET(v);
}

//!bind: function unshift($this : Array<T0>, $value : T0) : Void
YDSH_METHOD array_unshift(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_unshift);
    TRY(array_insertImpl(ctx, 0, LOCAL(1)));
    RET_VOID;
}

//!bind: function insert($this : Array<T0>, $index : Int32, $value : T0) : Void
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

//!bind: function extend($this : Array<T0>, $value : Array<T0>) : Array<T0>
YDSH_METHOD array_extend(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_extend);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    auto *value = typeAs<ArrayObject>(LOCAL(1));
    if(obj != value) {
        unsigned int valueSize = value->getValues().size();
        for(unsigned int i = 0; i < valueSize; i++) {
            if(obj->getValues().size() == ArrayObject::MAX_SIZE) {
                raiseOutOfRangeError(ctx, std::string("reach Array size limit"));
                RET_ERROR;
            }
            obj->append(value->getValues()[i]);
        }
    }
    RET(LOCAL(0));
}

//!bind: function swap($this : Array<T0>, $index : Int32, $value : T0) : T0
YDSH_METHOD array_swap(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_swap);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    int index = LOCAL(1).asInt();
    auto ret = TRY(resolveIndex(ctx, index, obj->getValues().size()));
    DSValue value = LOCAL(2);
    std::swap(obj->refValues()[ret.index], value);
    RET(value);
}

//!bind: function slice($this : Array<T0>, $from : Int32, $to : Int32) : Array<T0>
YDSH_METHOD array_slice(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_slice);
    auto &obj = *typeAs<ArrayObject>(LOCAL(0));
    int start = LOCAL(1).asInt();
    int stop = LOCAL(2).asInt();
    return slice(ctx, obj, start, stop);
}

//!bind: function from($this : Array<T0>, $from : Int32) : Array<T0>
YDSH_METHOD array_sliceFrom(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_sliceFrom);
    auto &obj = *typeAs<ArrayObject>(LOCAL(0));
    int start = LOCAL(1).asInt();
    return slice(ctx, obj, start, obj.getValues().size());
}

//!bind: function to($this : Array<T0>, $to : Int32) : Array<T0>
YDSH_METHOD array_sliceTo(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_sliceTo);
    auto &obj = *typeAs<ArrayObject>(LOCAL(0));
    int stop = LOCAL(1).asInt();
    return slice(ctx, obj, 0, stop);
}

//!bind: function copy($this : Array<T0>) : Array<T0>
YDSH_METHOD array_copy(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_copy);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    std::vector<DSValue> values = obj->getValues();
    RET(DSValue::create<ArrayObject>(obj->getTypeID(), std::move(values)));
}

//!bind: function reverse($this : Array<T0>) : Array<T0>
YDSH_METHOD array_reverse(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_reverse);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    std::reverse(obj->refValues().begin(), obj->refValues().end());
    RET(LOCAL(0));
}

//!bind: function sort($this : Array<T0>) : Array<T0> where T0 : _Value
YDSH_METHOD array_sort(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_sort);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    std::sort(obj->refValues().begin(), obj->refValues().end(),
            [](const DSValue &x, const DSValue &y){
        if(x.kind() == DSValueKind::INVALID) {  // (invalid x) < y  => false
            return false;
        }
        if(y.kind() == DSValueKind::INVALID) {  // x < (invalid y) => true
            return true;
        }
        return x.compare(y);
    });
    RET(LOCAL(0));
}

//!bind: function sortWith($this : Array<T0>, $comp : Func<Boolean, [T0, T0]>) : Array<T0>
YDSH_METHOD array_sortWith(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_sortWith);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    try {
        std::sort(obj->refValues().begin(), obj->refValues().end(),
                [&](const DSValue &x, const DSValue &y){
            auto ret = callFunction(ctx, DSValue(LOCAL(1)), makeArgs(x, y));
            if(ctx.hasError()) {
                throw std::runtime_error("");    //FIXME: not use exception
            }
            return ret.asBool();
        });
        RET(LOCAL(0));
    } catch(...) {
        RET_ERROR;
    }
}

//!bind: function join($this : Array<T0>, $delim : String) : String
YDSH_METHOD array_join(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_join);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    auto delim = LOCAL(1).asStrRef();

    bool hasRet = ctx.toStrBuf.empty();
    unsigned int count = 0;
    for(auto &e : obj->getValues()) {
        if(count++ > 0) {
            ctx.toStrBuf.append(delim.data(), delim.size());
        }
        if(e.isInvalid()) {
            raiseError(ctx, TYPE::UnwrappingError, "invalid value");
            RET_ERROR;
        }
        if(!e.opStr(ctx)) {
            ctx.toStrBuf.clear();
            RET_ERROR;
        }
    }

    if(hasRet) {
        std::string value;
        std::swap(value, ctx.toStrBuf);
        RET(DSValue::createStr(std::move(value)));
    } else {
        RET(DSValue::createInvalid());  // dummy
    }
}

//!bind: function size($this : Array<T0>) : Int32
YDSH_METHOD array_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_size);
    int size = typeAs<ArrayObject>(LOCAL(0))->getValues().size();
    RET(DSValue::createInt(size));
}

//!bind: function empty($this : Array<T0>) : Boolean
YDSH_METHOD array_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_empty);
    bool empty = typeAs<ArrayObject>(LOCAL(0))->getValues().empty();
    RET_BOOL(empty);
}

//!bind: function clear($this : Array<T0>) : Void
YDSH_METHOD array_clear(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_clear);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    obj->initIterator();
    obj->refValues().clear();
    RET_VOID;
}

//!bind: function $OP_ITER($this : Array<T0>) : Array<T0>
YDSH_METHOD array_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_iter);
    typeAs<ArrayObject>(LOCAL(0))->initIterator();
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Array<T0>) : T0
YDSH_METHOD array_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_next);
    auto *obj = typeAs<ArrayObject>(LOCAL(0));
    if(!obj->hasNext()) {
        raiseOutOfRangeError(ctx, std::string("array iterator has already reached end"));
        RET_ERROR;
    }
    RET(obj->nextElement());
}

//!bind: function $OP_HAS_NEXT($this : Array<T0>) : Boolean
YDSH_METHOD array_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_hasNext);
    RET_BOOL(typeAs<ArrayObject>(LOCAL(0))->hasNext());
}

//!bind: function $OP_CMD_ARG($this : Array<T0>) : Array<String>
YDSH_METHOD array_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_cmdArg);
    RET(typeAs<ArrayObject>(LOCAL(0))->opCmdArg(ctx));
}


// #################
// ##     Map     ##
// #################

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
YDSH_METHOD map_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_get);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    if(iter == obj->getValueMap().end()) {
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
    auto *obj = typeAs<MapObject>(LOCAL(0));
    obj->set(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
    RET_VOID;
}

//!bind: function put($this : Map<T0, T1>, $key : T0, $value : T1) : Option<T1>
YDSH_METHOD map_put(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_put);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    auto v = obj->set(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
    RET(v);
}

//!bind: function default($this : Map<T0, T1>, $key : T0, $value : T1) : T1
YDSH_METHOD map_default(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_default);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    auto v = obj->setDefault(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
    RET(v);
}

//!bind: function size($this : Map<T0, T1>) : Int32
YDSH_METHOD map_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_size);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    int value = obj->getValueMap().size();
    RET(DSValue::createInt(value));
}

//!bind: function empty($this : Map<T0, T1>) : Boolean
YDSH_METHOD map_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_empty);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    bool value = obj->getValueMap().empty();
    RET_BOOL(value);
}

//!bind: function get($this : Map<T0, T1>, $key : T0) : Option<T1>
YDSH_METHOD map_find(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_find);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    RET(iter != obj->getValueMap().end() ? iter->second : DSValue::createInvalid());
}

//!bind: function find($this : Map<T0, T1>, $key : T0) : Boolean
YDSH_METHOD map_find2(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_find2);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    RET_BOOL(iter != obj->getValueMap().end());
}

//!bind: function remove($this : Map<T0, T1>, $key : T0) : Boolean
YDSH_METHOD map_remove(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_remove);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    bool r = obj->remove(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function swap($this : Map<T0, T1>, $key : T0, $value : T1) : T1
YDSH_METHOD map_swap(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_swap);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    DSValue value = LOCAL(2);
    if(!obj->trySwap(LOCAL(1), value)) {
        std::string msg("not found key: ");
        msg += LOCAL(1).toString();
        raiseError(ctx, TYPE::KeyNotFoundError, std::move(msg));
        RET_ERROR;
    }
    RET(value);
}

//!bind: function copy($this : Map<T0, T1>) : Map<T0, T1>
YDSH_METHOD map_copy(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_copy);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    HashMap map(obj->getValueMap());
    RET(DSValue::create<MapObject>(obj->getTypeID(), std::move(map)));
}

//!bind: function clear($this : Map<T0, T1>) : Void
YDSH_METHOD map_clear(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_clear);
    typeAs<MapObject>(LOCAL(0))->clear();
    RET_VOID;
}

//!bind: function $OP_ITER($this : Map<T0, T1>) : Map<T0, T1>
YDSH_METHOD map_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_iter);
    typeAs<MapObject>(LOCAL(0))->initIterator();
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Map<T0, T1>) : Tuple<T0, T1>
YDSH_METHOD map_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_next);
    auto *obj = typeAs<MapObject>(LOCAL(0));
    if(!obj->hasNext()) {
        raiseOutOfRangeError(ctx, std::string("map iterator has already reached end"));
        RET_ERROR;
    }
    RET(obj->nextElement(ctx));
}

//!bind: function $OP_HAS_NEXT($this : Map<T0, T1>) : Boolean
YDSH_METHOD map_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_hasNext);
    RET_BOOL(typeAs<MapObject>(LOCAL(0))->hasNext());
}

// ###################
// ##     Tuple     ##
// ###################

//!bind: function $OP_CMD_ARG($this : Tuple<>) : Array<String>
YDSH_METHOD tuple_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(tuple_cmdArg);
    RET(typeAs<BaseObject>(LOCAL(0))->opCmdArgAsTuple(ctx));
}


// ###################
// ##     Error     ##
// ###################

//!bind: function $OP_INIT($this : Error, $message : String) : Error
YDSH_METHOD error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_init);
    auto &type = ctx.symbolTable.get(LOCAL(0).getTypeID());
    RET(DSValue(ErrorObject::newError(ctx, type, LOCAL(1))));
}

//!bind: function message($this : Error) : String
YDSH_METHOD error_message(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_message);
    RET(typeAs<ErrorObject>(LOCAL(0))->getMessage());
}

//!bind: function backtrace($this : Error) : Void
YDSH_METHOD error_backtrace(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_backtrace);
    typeAs<ErrorObject>(LOCAL(0))->printStackTrace(ctx);
    RET_VOID;
}

//!bind: function name($this : Error) : String
YDSH_METHOD error_name(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_name);
    RET(typeAs<ErrorObject>(LOCAL(0))->getName());
}

// ####################
// ##     UnixFD     ##
// ####################

//!bind: function $OP_INIT($this : UnixFD, $path : String) : UnixFD
YDSH_METHOD fd_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_init);
    const char *path = LOCAL(1).asStrRef().data();
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if(fd != -1) {
        RET(DSValue::create<UnixFdObject>(fd));
    }
    int e = errno;
    std::string msg = "open failed: ";
    msg += path;
    raiseSystemError(ctx, e, std::move(msg));
    RET_ERROR;
}

//!bind: function close($this : UnixFD) : Void
YDSH_METHOD fd_close(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_close);
    auto *fdObj = typeAs<UnixFdObject>(LOCAL(0));
    int fd = fdObj->getValue();
    if(fdObj->tryToClose(true) < 0) {
        int e = errno;
        raiseSystemError(ctx, e, std::to_string(fd));
        RET_ERROR;
    }
    RET_VOID;
}

//!bind: function dup($this : UnixFD) : UnixFD
YDSH_METHOD fd_dup(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_dup);
    int fd = typeAs<UnixFdObject>(LOCAL(0))->getValue();
    int newfd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if(newfd < 0) {
        int e = errno;
        raiseSystemError(ctx, e, std::to_string(fd));
        RET_ERROR;
    }
    RET(DSValue::create<UnixFdObject>(newfd));
}

//!bind: function $OP_BOOL($this : UnixFD) : Boolean
YDSH_METHOD fd_bool(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_bool);
    int fd = typeAs<UnixFdObject>(LOCAL(0))->getValue();
    RET_BOOL(fd != -1);
}

//!bind: function $OP_NOT($this : UnixFD) : Boolean
YDSH_METHOD fd_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_not);
    int fd = typeAs<UnixFdObject>(LOCAL(0))->getValue();
    RET_BOOL(fd == -1);
}

// #################
// ##     Job     ##
// #################

//!bind: function in($this : Job) : UnixFD
YDSH_METHOD job_in(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_in);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    RET(obj->getInObj());
}

//!bind: function out($this : Job) : UnixFD
YDSH_METHOD job_out(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_out);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    RET(obj->getOutObj());
}

//!bind: function $OP_GET($this : Job, $index : Int32) : UnixFD
YDSH_METHOD job_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_get);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    int index = LOCAL(1).asInt();
    if(index == 0) {
        RET(obj->getInObj());
    }
    if(index == 1) {
        RET(obj->getOutObj());
    }
    std::string msg = "invalid fd number";
    raiseOutOfRangeError(ctx, std::move(msg));
    RET_ERROR;
}

//!bind: function poll($this : Job) : Boolean
YDSH_METHOD job_poll(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_poll);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    RET_BOOL(obj->poll());
}

//!bind: function wait($this : Job) : Int32
YDSH_METHOD job_wait(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_wait);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    bool jobctrl = hasFlag(DSState_option(&ctx), DS_OPTION_JOB_CONTROL);
    auto entry = Job(obj);
    int s = ctx.jobTable.waitAndDetach(entry, jobctrl);
    ctx.jobTable.updateStatus();
    RET(DSValue::createInt(s));
}

//!bind: function raise($this : Job, $s : Signal) : Void
YDSH_METHOD job_raise(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_raise);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    obj->send(LOCAL(1).asSig());
    RET_VOID;
}

//!bind: function detach($this : Job) : Void
YDSH_METHOD job_detach(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_detach);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    ctx.jobTable.detach(obj->getJobID());
    RET_VOID;
}

//!bind: function size($this : Job) : Int32
YDSH_METHOD job_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_size);
    auto *obj = typeAs<JobImplObject>(LOCAL(0));
    RET(DSValue::createInt(obj->getProcSize()));
}

//!bind: function pid($this : Job, $index : Int32) : Int32
YDSH_METHOD job_pid(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_pid);
    auto *entry = typeAs<JobImplObject>(LOCAL(0));
    int index = LOCAL(1).asInt();

    if(index > -1 && static_cast<unsigned int>(index) < entry->getProcSize()) {
        int pid = entry->getPid(index);
        RET(DSValue::createInt(pid));
    }
    std::string msg = "number of processes is: ";
    msg += std::to_string(entry->getProcSize());
    msg += ", but index is: ";
    msg += std::to_string(index);
    raiseOutOfRangeError(ctx, std::move(msg));
    RET_ERROR;
}

} //namespace ydsh


#endif //YDSH_BUILTIN_H
