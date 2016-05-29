/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#ifndef YDSH_CORE_BUILTIN_H
#define YDSH_CORE_BUILTIN_H

#include <cmath>
#include <cstring>
#include <type_traits>

#include "context.h"
#include "object.h"
#include "../misc/unicode.hpp"
#include "../misc/num.h"

// helper macro
#define LOCAL(index) (ctx.getLocal(index))
#define RET(value) do { ctx.push(value); return true; } while(false)
#define RET_BOOL(value) do { ctx.push((value) ? ctx.getTrueObj() : ctx.getFalseObj()); return true; } while(false)

#define SUPPRESS_WARNING(a) (void)a

/**
 *   //!bind: function <method name>($this : <receiver type>, $param1 : <type1>, $param2? : <type2>, ...) : <return type>
 *   //!bind: constructor <type name>($param1 : <type1>, ....)
 *   $<param name>?  has default value
 */

namespace ydsh {
namespace core {

template <typename T>
using ObjTypeStub = typename std::conditional<
        std::is_same<double, T>::value,
        Float_Object,
        typename std::conditional<std::is_same<long, T>::value || std::is_same<unsigned long, T>::value,
                Long_Object, Int_Object>::type>::type;


// for binary op
#define EACH_BASIC_OP(OP) \
    OP(ADD, +) \
    OP(SUB, -) \
    OP(MUL, *) \
    OP(AND, &) \
    OP(OR,  |) \
    OP(XOR, ^)

#define EACH_RELATE_OP(OP) \
    OP(LT, <) \
    OP(GT, >) \
    OP(LE, <=) \
    OP(GE, >=)

#define EACH_UNARY_OP(OP) \
    OP(MINUS, -) \
    OP(NOT, ~)


#define GEN_BASIC_OP(NAME, OPERATOR) \
template <typename T> \
static inline bool basic_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue(); \
    T result = left OPERATOR right; \
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result)); \
}

#define GEN_RELATE_OP(NAME, OPERATOR) \
template <typename T> \
static inline bool relate_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue(); \
    bool result = left OPERATOR right; \
    RET_BOOL(result); \
}

#define GEN_UNARY_OP(NAME, OPERATOR) \
template <typename T> \
static inline bool unary_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto right = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    T result = OPERATOR right; \
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result)); \
}


EACH_BASIC_OP(GEN_BASIC_OP)

EACH_RELATE_OP(GEN_RELATE_OP)

EACH_UNARY_OP(GEN_UNARY_OP)


static inline bool checkZeroDiv(RuntimeContext &ctx, int right) {
    if(right == 0) {
        ctx.throwError(ctx.getPool().getArithmeticErrorType(), "zero division");
        return false;
    }
    return true;
}

static inline bool checkZeroMod(RuntimeContext &ctx, int right) {
    if(right == 0) {
        ctx.throwError(ctx.getPool().getArithmeticErrorType(), "zero module");
        return false;
    }
    return true;
}

template <typename T>
static inline bool basic_div(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();
    if(!checkZeroDiv(ctx, (int) right)) {
        return false;
    }
    T result = left / right;
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result));
}

template <typename T>
static inline bool basic_mod(RuntimeContext &ctx) {
    using ObjType = ObjTypeStub<T>;
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue();
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue();
    if(!checkZeroMod(ctx, (int) right)) {
        return false;
    }
    T result = left % right;
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result));
}


// #################
// ##     Any     ##
// #################

//!bind: function $OP_STR($this : Any) : String
static inline bool to_str(RuntimeContext & ctx) {
    SUPPRESS_WARNING(to_str);
    RET(LOCAL(0)->str(ctx));
}

//!bind: function $OP_INTERP($this : Any) : String
static inline bool to_interp(RuntimeContext & ctx) {
    SUPPRESS_WARNING(to_interp);
    RET(LOCAL(0)->interp(ctx, nullptr));
}


// ##################
// ##     Byte     ##
// ##################

//!bind: function $OP_PLUS($this: Byte) : Byte
static inline bool byte_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Byte) : Byte
static inline bool byte_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_minus);
    return unary_MINUS<unsigned char>(ctx);
}

//!bind: function $OP_NOT($this : Byte) : Byte
static inline bool byte_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_not);
    return unary_NOT<unsigned char>(ctx);
}


// ###################
// ##     Int16     ##
// ###################

//!bind: function $OP_PLUS($this: Int16) : Int16
static inline bool int16_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Int16) : Int16
static inline bool int16_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_minus);
    return unary_MINUS<short>(ctx);
}

//!bind: function $OP_NOT($this : Int16) : Int16
static inline bool int16_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_not);
    return unary_NOT<short>(ctx);
}


// ####################
// ##     Uint16     ##
// ####################

//!bind: function $OP_PLUS($this: Uint16) : Uint16
static inline bool uint16_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Uint16) : Uint16
static inline bool uint16_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_minus);
    return unary_MINUS<unsigned short>(ctx);
}

//!bind: function $OP_NOT($this : Uint16) : Uint16
static inline bool uint16_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_not);
    return unary_NOT<unsigned short>(ctx);
}


// ###################
// ##     Int32     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Int32) : Int32
static inline bool int_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int32) : Int32
static inline bool int_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_minus);
    return unary_MINUS<int>(ctx);
}

//!bind: function $OP_NOT($this : Int32) : Int32
static inline bool int_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_not);
    return unary_NOT<int>(ctx);
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_add);
    return basic_ADD<int>(ctx);
}

//!bind: function $OP_SUB($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_sub);
    return basic_SUB<int>(ctx);
}

//!bind: function $OP_MUL($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mul);
    return basic_MUL<int>(ctx);
}

//!bind: function $OP_DIV($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_div);
    return basic_div<int>(ctx);
}

//!bind: function $OP_MOD($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mod);
    return basic_mod<int>(ctx);
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_lt);
    return relate_LT<int>(ctx);
}

//!bind: function $OP_GT($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_gt);
    return relate_GT<int>(ctx);
}

//!bind: function $OP_LE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_le);
    return relate_LE<int>(ctx);
}

//!bind: function $OP_GE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ge);
    return relate_GE<int>(ctx);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_and);
    return basic_AND<int>(ctx);
}

//!bind: function $OP_OR($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_or);
    return basic_OR<int>(ctx);
}

//!bind: function $OP_XOR($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_xor);
    return basic_XOR<int>(ctx);
}


// ####################
// ##     Uint32     ##
// ####################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Uint32) : Uint32
static inline bool uint_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Uint32) : Uint32
static inline bool uint_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_minus);
    return unary_MINUS<unsigned int>(ctx);
}

//!bind: function $OP_NOT($this : Uint32) : Uint32
static inline bool uint_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_not);
    return unary_NOT<unsigned int>(ctx);
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_add);
    return basic_ADD<unsigned int>(ctx);
}

//!bind: function $OP_SUB($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_sub);
    return basic_SUB<unsigned int>(ctx);
}

//!bind: function $OP_MUL($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_mul);
    return basic_MUL<unsigned int>(ctx);
}

//!bind: function $OP_DIV($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_div);
    return basic_div<unsigned int>(ctx);
}

//!bind: function $OP_MOD($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_mod);
    return basic_mod<unsigned int>(ctx);
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_lt);
    return relate_LT<unsigned int>(ctx);
}

//!bind: function $OP_GT($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_gt);
    return relate_GT<unsigned int>(ctx);
}

//!bind: function $OP_LE($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_le);
    return relate_LE<unsigned int>(ctx);
}

//!bind: function $OP_GE($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_ge);
    return relate_GE<unsigned int>(ctx);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_and);
    return basic_AND<unsigned int>(ctx);
}

//!bind: function $OP_OR($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_or);
    return basic_OR<unsigned int>(ctx);
}

//!bind: function $OP_XOR($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_xor);
    return basic_XOR<unsigned int>(ctx);
}

// ###################
// ##     Int64     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Int64) : Int64
static inline bool int64_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int64) : Int64
static inline bool int64_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_minus);
    return unary_MINUS<long>(ctx);
}

//!bind: function $OP_NOT($this : Int64) : Int64
static inline bool int64_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_not);
    return unary_NOT<long>(ctx);
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_add);
    return basic_ADD<long>(ctx);
}

//!bind: function $OP_SUB($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_sub);
    return basic_SUB<long>(ctx);
}

//!bind: function $OP_MUL($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mul);
    return basic_MUL<long>(ctx);
}

//!bind: function $OP_DIV($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_div);
    return basic_div<long>(ctx);
}

//!bind: function $OP_MOD($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mod);
    return basic_mod<long>(ctx);
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_lt);
    return relate_LT<long>(ctx);
}

//!bind: function $OP_GT($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_gt);
    return relate_GT<long>(ctx);
}

//!bind: function $OP_LE($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_le);
    return relate_LE<long>(ctx);
}

//!bind: function $OP_GE($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ge);
    return relate_GE<long>(ctx);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_and);
    return basic_AND<long>(ctx);
}

//!bind: function $OP_OR($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_or);
    return basic_OR<long>(ctx);
}

//!bind: function $OP_XOR($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_xor);
    return basic_XOR<long>(ctx);
}

// ####################
// ##     Uint64     ##
// ####################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Uint64) : Uint64
static inline bool uint64_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Uint64) : Uint64
static inline bool uint64_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_minus);
    return unary_MINUS<unsigned long>(ctx);
}

//!bind: function $OP_NOT($this : Uint64) : Uint64
static inline bool uint64_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_not);
    return unary_NOT<unsigned long>(ctx);
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_add);
    return basic_ADD<unsigned long>(ctx);
}

//!bind: function $OP_SUB($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_sub);
    return basic_SUB<unsigned long>(ctx);
}

//!bind: function $OP_MUL($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_mul);
    return basic_MUL<unsigned long>(ctx);
}

//!bind: function $OP_DIV($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_div);
    return basic_div<unsigned long>(ctx);
}

//!bind: function $OP_MOD($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_mod);
    return basic_mod<unsigned long>(ctx);
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_lt);
    return relate_LT<unsigned long>(ctx);
}

//!bind: function $OP_GT($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_gt);
    return relate_GT<unsigned long>(ctx);
}

//!bind: function $OP_LE($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_le);
    return relate_LE<unsigned long>(ctx);
}

//!bind: function $OP_GE($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_ge);
    return relate_GE<unsigned long>(ctx);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_and);
    return basic_AND<unsigned long>(ctx);
}

//!bind: function $OP_OR($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_or);
    return basic_OR<unsigned long>(ctx);
}

//!bind: function $OP_XOR($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_xor);
    return basic_XOR<unsigned long>(ctx);
}


// ###################
// ##     Float     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Float) : Float
static inline bool float_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Float) : Float
static inline bool float_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_minus);
    return unary_MINUS<double>(ctx);
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
static inline bool float_2_float_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_add);
    return basic_ADD<double>(ctx);
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
static inline bool float_2_float_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_sub);
    return basic_SUB<double>(ctx);
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
static inline bool float_2_float_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_mul);
    return basic_MUL<double>(ctx);
}

//!bind: function $OP_DIV($this : Float, $target : Float) : Float
static inline bool float_2_float_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_div);
    double left = typeAs<Float_Object>(LOCAL(0))->getValue();
    double right = typeAs<Float_Object>(LOCAL(1))->getValue();
    double value = left / right;
    RET(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Boolean
static inline bool float_2_float_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Boolean
static inline bool float_2_float_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_lt);
    return relate_LT<double>(ctx);
}

//!bind: function $OP_GT($this : Float, $target : Float) : Boolean
static inline bool float_2_float_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_gt);
    return relate_GT<double>(ctx);
}

//!bind: function $OP_LE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_le);
    return relate_LE<double>(ctx);
}

//!bind: function $OP_GE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ge);
    return relate_GE<double>(ctx);
}

// =====  additional float op  ======

//!bind: function isNan($this : Float): Boolean
static inline bool float_isNan(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isNan);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue();
    RET_BOOL(std::isnan(value));
}

//!bind: function isInf($this : Float): Boolean
static inline bool float_isInf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isInf);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue();
    RET_BOOL(std::isinf(value));
}

//!bind: function isFinite($this : Float): Boolean
static inline bool float_isFinite(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isFinite);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue();
    RET_BOOL(std::isfinite(value));
}


// #####################
// ##     Boolean     ##
// #####################

//!bind: function $OP_NOT($this : Boolean) : Boolean
static inline bool boolean_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_not);
    RET_BOOL(!typeAs<Boolean_Object>(LOCAL(0))->getValue());
}

//!bind: function $OP_EQ($this : Boolean, $target : Boolean) : Boolean
static inline bool boolean_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_eq);
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function $OP_NE($this : Boolean, $target : Boolean) : Boolean
static inline bool boolean_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_ne);
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}


// ####################
// ##     String     ##
// ####################

//!bind: function $OP_ADD($this : String, $target : Any) : String
static inline bool string_add(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_add);

    // cats LOCAL(1) to string
    ctx.loadLocal(1);
    ctx.callToString();

    std::string str(typeAs<String_Object>(LOCAL(0))->getValue());
    str += typeAs<String_Object>(ctx.peek())->getValue();
    RET(DSValue::create<String_Object>(ctx.getPool().getStringType(), std::move(str)));
}

//!bind: function $OP_EQ($this : String, $target : String) : Boolean
static inline bool string_eq(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_eq);
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function $OP_NE($this : String, $target : String) : Boolean
static inline bool string_ne(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_ne);
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function $OP_LT($this : String, $target : String) : Boolean
static inline bool string_lt(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lt);
    bool r = strcmp(typeAs<String_Object>(LOCAL(0))->getValue(),
                    typeAs<String_Object>(LOCAL(1))->getValue()) < 0;
    RET_BOOL(r);
}

//!bind: function $OP_GT($this : String, $target : String) : Boolean
static inline bool string_gt(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_gt);
    bool r = strcmp(typeAs<String_Object>(LOCAL(0))->getValue(),
                    typeAs<String_Object>(LOCAL(1))->getValue()) > 0;
    RET_BOOL(r);
}

//!bind: function $OP_LE($this : String, $target : String) : Boolean
static inline bool string_le(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_le);
    bool r = strcmp(typeAs<String_Object>(LOCAL(0))->getValue(),
                    typeAs<String_Object>(LOCAL(1))->getValue()) <= 0;
    RET_BOOL(r);
}

//!bind: function $OP_GE($this : String, $target : String) : Boolean
static inline bool string_ge(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_ge);
    bool r = strcmp(typeAs<String_Object>(LOCAL(0))->getValue(),
                    typeAs<String_Object>(LOCAL(1))->getValue()) >= 0;
    RET_BOOL(r);
}

//!bind: function size($this : String) : Int32
static inline bool string_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_size);
    int size = typeAs<String_Object>(LOCAL(0))->size();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), size));
}

//!bind: function empty($this : String) : Boolean
static inline bool string_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_empty);
    bool empty = typeAs<String_Object>(LOCAL(0))->empty();
    RET_BOOL(empty);
}

//!bind: function count($this : String) : Int32
static inline bool string_count(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_count);
    const char *ptr = typeAs<String_Object>(LOCAL(0))->getValue();
    const unsigned int size = typeAs<String_Object>(LOCAL(0))->size();
    unsigned int count = 0;
    for(unsigned int i = 0; i < size; i = misc::UnicodeUtil::utf8NextPos(i, ptr[i])) {
        count++;
    }
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), count));
}

/**
 * return always false.
 */
static bool throwOutOfRangeError(RuntimeContext &ctx, std::string &&message) {
    ctx.throwError(ctx.getPool().getOutOfRangeErrorType(), std::move(message));
    return false;
}

//!bind: function $OP_GET($this : String, $index : Int32) : String
static inline bool string_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_get);
    auto strObj = typeAs<String_Object>(LOCAL(0));
    const int pos = typeAs<Int_Object>(LOCAL(1))->getValue();
    const unsigned int size = strObj->size();

    if(pos >= 0 && static_cast<unsigned int>(pos) < size) {
        const unsigned int limit = pos;
        unsigned int index = 0;
        unsigned int count = 0;
        for(; index < size; index = misc::UnicodeUtil::utf8NextPos(index, strObj->getValue()[index])) {
            if(count == limit) {
                break;
            }
            count++;
        }
        if(count == limit && index < size) {
            unsigned int nextIndex = misc::UnicodeUtil::utf8NextPos(index, strObj->getValue()[index]);
            RET(DSValue::create<String_Object>(
                    ctx.getPool().getStringType(), std::string(strObj->getValue() + index, nextIndex - index)));
        }
    }

    std::string msg("size is ");
    msg += std::to_string(size);
    msg += ", but code position is ";
    msg += std::to_string(pos);
    return throwOutOfRangeError(ctx, std::move(msg));
}

/**
 * startIndex is inclusive.
 * stopIndex is exclusive.
 */
static bool sliceImpl(RuntimeContext &ctx, String_Object *strObj, int startIndex, int stopIndex) {
    const unsigned int size = strObj->size();

    // resolve actual index
    startIndex = (startIndex < 0 ? size : 0) + startIndex;
    stopIndex = (stopIndex < 0 ? size : 0) + stopIndex;

    // check range
    if(startIndex > stopIndex || startIndex < 0 || startIndex >= static_cast<int>(size) ||
       stopIndex < 0 || stopIndex > static_cast<int>(size)) {
        std::string msg("size is ");
        msg += std::to_string(size);
        msg += ", but range is [";
        msg += std::to_string(startIndex);
        msg += ", ";
        msg += std::to_string(stopIndex);
        msg += ")";
        return throwOutOfRangeError(ctx, std::move(msg));
    }

    RET(DSValue::create<String_Object>(
            ctx.getPool().getStringType(),
            std::string(strObj->getValue() + startIndex, stopIndex - startIndex)));
}

//!bind: function slice($this : String, $start : Int32, $stop : Int32) : String
static inline bool string_slice(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_slice);
    return sliceImpl(ctx, typeAs<String_Object>(LOCAL(0)),
                     typeAs<Int_Object>(LOCAL(1))->getValue(),
                     typeAs<Int_Object>(LOCAL(2))->getValue());
}

//!bind: function sliceFrom($this : String, $start : Int32) : String
static inline bool string_sliceFrom(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_sliceFrom);
    auto strObj = typeAs<String_Object>(LOCAL(0));
    return sliceImpl(ctx, strObj, typeAs<Int_Object>(LOCAL(1))->getValue(), strObj->size());
}

//!bind: function sliceTo($this : String, $stop : Int32) : String
static inline bool string_sliceTo(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_sliceTo);
    auto strObj = typeAs<String_Object>(LOCAL(0));
    return sliceImpl(ctx, strObj, 0, typeAs<Int_Object>(LOCAL(1))->getValue());
}

static bool startsWith(const char *thisStr, const char *targetStr, int offset) {
    if(offset < 0) {
        return false;
    }
    const char *str = thisStr + offset;
    return strstr(str, targetStr) == str;
}

//!bind: function startsWith($this : String, $target : String) : Boolean
static inline bool string_startsWith(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_startsWith);
    auto thisObj = typeAs<String_Object>(LOCAL(0));
    auto targetObj = typeAs<String_Object>(LOCAL(1));

    bool r = startsWith(thisObj->getValue(), targetObj->getValue(), 0);
    RET_BOOL(r);
}

//!bind: function endsWith($this : String, $target : String) : Boolean
static inline bool string_endsWith(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_endsWith);
    auto thisObj = typeAs<String_Object>(LOCAL(0));
    auto targetObj = typeAs<String_Object>(LOCAL(1));

    bool r = startsWith(thisObj->getValue(), targetObj->getValue(), thisObj->size() - targetObj->size());
    RET_BOOL(r);
}

//!bind: function indexOf($this : String, $target : String) : Int32
static inline bool string_indexOf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_indexOf);
    const char *thisStr = typeAs<String_Object>(LOCAL(0))->getValue();
    const char *targetStr = typeAs<String_Object>(LOCAL(1))->getValue();

    const char *ptr = strstr(thisStr, targetStr);
    int index = -1;
    if(ptr != nullptr) {
        index = ptr - thisStr;
    }
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), index));
}

//!bind: function lastIndexOf($this : String, $target : String) : Int32
static inline bool string_lastIndexOf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lastIndexOf);
    const char *thisStr = typeAs<String_Object>(LOCAL(0))->getValue();
    const char *targetStr = typeAs<String_Object>(LOCAL(1))->getValue();

    int index = -1;
    for(const char *ptr = thisStr; *ptr != '\0'; ptr++) {
        ptr = strstr(ptr, targetStr);
        if(ptr == nullptr) {
            break;
        }
        index = ptr - thisStr;
    }

    if(*thisStr == *targetStr && *thisStr == '\0') {
        index = 0;
    }
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), index));
}

//!bind: function split($this : String, $delim : String) : Array<String>
static inline bool string_split(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_split);
    const char *thisStr = typeAs<String_Object>(LOCAL(0))->getValue();
    const char *delimStr = typeAs<String_Object>(LOCAL(1))->getValue();
    const unsigned int delimSize = typeAs<String_Object>(LOCAL(1))->size();

    auto results = DSValue::create<Array_Object>(ctx.getPool().getStringArrayType());
    auto ptr = typeAs<Array_Object>(results);

    const char *remain = thisStr;
    while(delimSize > 0) {
        const char *ret = strstr(remain, delimStr);
        if(ret == nullptr) {
            break;
        }
        ptr->append(DSValue::create<String_Object>(ctx.getPool().getStringType(), std::string(remain, ret - remain)));
        remain = ret + delimSize;
    }

    if(remain == thisStr) {
        ptr->append(LOCAL(0));
    } else if(*remain != '\0') {
        ptr->append(DSValue::create<String_Object>(ctx.getPool().getStringType(), std::string(remain)));
    }

    RET(std::move(results));
}

static bool createResult(RuntimeContext &ctx, DSValue &&value, bool success) {
    // get tuple type
    std::vector<DSType *> types(2);
    types[0] = value.get()->getType();
    types[1] = &ctx.getPool().getBooleanType();
    auto &tupleType = ctx.getPool().createTupleType(std::move(types));

    // create result
    DSValue tuple = DSValue::create<Tuple_Object>(tupleType);
    auto *ptr = typeAs<Tuple_Object>(tuple);
    ptr->set(0, value);
    ptr->set(1, success ? ctx.getTrueObj() : ctx.getFalseObj());

    RET(std::move(tuple));
}

//!bind: function toInt32($this : String) : Tuple<Int32, Boolean>
static inline bool string_toInt32(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toInt32);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    long value = convertToInt64(str, status, false);

    // range check
    if(status != 0 || value > INT32_MAX || value < INT32_MIN) {
        status = 1;
        value = 0;
    }
    return createResult(ctx, DSValue::create<Int_Object>(
            ctx.getPool().getInt32Type(), (int)value), status == 0);
}

//!bind: function toUint32($this : String) : Tuple<Uint32, Boolean>
static inline bool string_toUint32(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toUint32);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    long value = convertToInt64(str, status, false);

    // range check
    if(status != 0 || value > UINT32_MAX || value < 0) {
        status = 1;
        value = 0;
    }
    return createResult(ctx, DSValue::create<Int_Object>(
            ctx.getPool().getUint32Type(), (unsigned int)value), status == 0);
}

//!bind: function toInt64($this : String) : Tuple<Int64, Boolean>
static inline bool string_toInt64(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toInt64);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    long value = convertToInt64(str, status, false);

    return createResult(ctx, DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value), status == 0);
}

//!bind: function toUint64($this : String) : Tuple<Uint64, Boolean>
static inline bool string_toUint64(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toUint64);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    unsigned long value = convertToUint64(str, status, false);

    return createResult(ctx, DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value), status == 0);
}

//!bind: function toFloat($this : String) : Tuple<Float, Boolean>
static inline bool string_toFloat(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toFloat);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    double value = convertToDouble(str, status, false);

    return createResult(ctx, DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value), status == 0);
}

//!bind: function $OP_ITER($this : String) : StringIter
static inline bool string_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_iter);
    String_Object *str = typeAs<String_Object>(LOCAL(0));
    RET(DSValue::create<StringIter_Object>(ctx.getPool().getStringIterType(), str));
}


// ########################
// ##     StringIter     ##
// ########################

//!bind: function $OP_NEXT($this : StringIter) : String
static inline bool stringIter_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(stringIter_next);
    auto strIter = typeAs<StringIter_Object>(LOCAL(0));
    auto strObj = typeAs<String_Object>(strIter->strObj);
    if(strIter->curIndex >= strObj->size()) {
        return throwOutOfRangeError(ctx, std::string("string iterator reach end of string"));
    }
    unsigned int curIndex = strIter->curIndex;
    strIter->curIndex = misc::UnicodeUtil::utf8NextPos(curIndex, strObj->getValue()[curIndex]);
    if(strIter->curIndex > strObj->size()) {
        fatal("broken string iterator\n");
    }

    unsigned int size = strIter->curIndex - curIndex;
    RET(DSValue::create<String_Object>(
            ctx.getPool().getStringType(), std::string(strObj->getValue() + curIndex, size)));
}

//!bind: function $OP_HAS_NEXT($this : StringIter) : Boolean
static inline bool stringIter_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(stringIter_hasNext);
    auto strIter = typeAs<StringIter_Object>(LOCAL(0));
    bool r = strIter->curIndex < typeAs<String_Object>(strIter->strObj)->size();
    RET_BOOL(r);
}


// ########################
// ##     ObjectPath     ##
// ########################

//!bind: function $OP_EQ($this : ObjectPath, $target : ObjectPath) : Boolean
static inline bool objectpath_eq(RuntimeContext &ctx) {
    SUPPRESS_WARNING(objectpath_eq);
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function $OP_NE($this : ObjectPath, $target : ObjectPath) : Boolean
static inline bool objectpath_ne(RuntimeContext &ctx) {
    SUPPRESS_WARNING(objectpath_ne);
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function size($this : ObjectPath) : Int32
static inline bool objectpath_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(objectpath_size);
    int size = typeAs<String_Object>(LOCAL(0))->size();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), size));
}


// ###################
// ##     Array     ##
// ###################

//!bind: constructor ($this : Array<T0>)
static inline bool array_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, DSValue::create<Array_Object>(*type));
    return true;
}

// check index range and throw exception.
static bool checkRange(RuntimeContext &ctx, int index, int size) {
    if(index < 0 || index >= size) {
        std::string message("size is ");
        message += std::to_string(size);
        message += ", but index is ";
        message += std::to_string(index);
        return throwOutOfRangeError(ctx, std::move(message));
    }
    return true;
}

//!bind: function $OP_GET($this : Array<T0>, $index : Int32) : T0
static inline bool array_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_get);

    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    int size = obj->getValues().size();
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!checkRange(ctx, index, size)) {
        return false;
    }
    RET(obj->getValues()[index]);
}

//!bind: function $OP_SET($this : Array<T0>, $index : Int32, $value : T0) : Void
static inline bool array_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_set);

    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    int size = obj->getValues().size();
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!checkRange(ctx, index, size)) {
        return false;
    }
    obj->set(index, LOCAL(2));
    return true;
}

//!bind: function peek($this : Array<T0>) : T0
static inline bool array_peek(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_peek);
    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    if(obj->getValues().empty()) {
        return throwOutOfRangeError(ctx, std::string("Array size is 0"));
    }

    DSValue poped = obj->refValues().back();
    RET(std::move(poped));
}

//!bind: function push($this : Array<T0>, $value : T0) : Void
static inline bool array_push(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_push);
    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    if(obj->getValues().size() == INT32_MAX) {
        return throwOutOfRangeError(ctx, std::string("reach Array size limit"));
    }
    obj->append(LOCAL(1));
    return true;
}

//!bind: function pop($this : Array<T0>) : T0
static inline bool array_pop(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_pop);
    bool r = array_peek(ctx);
    if(r) {
        typeAs<Array_Object>(LOCAL(0))->refValues().pop_back();
    }
    return r;
}

//!bind: function add($this : Array<T0>, $value : T0) : Array<T0>
static inline bool array_add(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_add);
    bool r = array_push(ctx);
    if(!r) {
        return false;
    }
    RET(LOCAL(0));
}

//!bind: function swap($this : Array<T0>, $index : Int32, $value : T0) : T0
static inline bool array_swap(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_swap);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!checkRange(ctx, index, obj->getValues().size())) {
        return false;
    }
    DSValue value = LOCAL(2);
    std::swap(obj->refValues()[index], value);
    RET(std::move(value));
}

//!bind: function size($this : Array<T0>) : Int32
static inline bool array_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_size);
    int size = typeAs<Array_Object>(LOCAL(0))->getValues().size();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), size));
}

//!bind: function empty($this : Array<T0>) : Boolean
static inline bool array_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_empty);
    bool empty = typeAs<Array_Object>(LOCAL(0))->getValues().empty();
    RET_BOOL(empty);
}

//!bind: function clear($this : Array<T0>) : Void
static inline bool array_clear(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_clear);
    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    obj->initIterator();
    obj->refValues().clear();
    return true;
}

//!bind: function $OP_ITER($this : Array<T0>) : Array<T0>
static inline bool array_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_iter);
    typeAs<Array_Object>(LOCAL(0))->initIterator();
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Array<T0>) : T0
static inline bool array_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_next);
    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    if(!obj->hasNext()) {
        return throwOutOfRangeError(ctx, std::string("array iterator has already reached end"));
    }
    RET(obj->nextElement());
}

//!bind: function $OP_HAS_NEXT($this : Array<T0>) : Boolean
static inline bool array_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_hasNext);
    RET_BOOL(typeAs<Array_Object>(LOCAL(0))->hasNext());
}

//!bind: function $OP_CMD_ARG($this : Array<T0>) : Array<String>
static inline bool array_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_cmdArg);
    RET(LOCAL(0)->commandArg(ctx, nullptr));
}


// #################
// ##     Map     ##
// #################

//!bind: constructor ($this : Map<T0, T1>)
static inline bool map_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, DSValue::create<Map_Object>(*type));
    return true;
}

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
static inline bool map_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_get);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    if(iter == obj->getValueMap().end()) {
        std::string msg("not found key: ");
        msg += LOCAL(1)->toString(ctx, nullptr);
        ctx.throwError(ctx.getPool().getKeyNotFoundErrorType(), std::move(msg));
        return false;
    }
    RET(iter->second);
}

//!bind: function $OP_SET($this : Map<T0, T1>, $key : T0, $value : T1) : Void
static inline bool map_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_set);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    obj->set(LOCAL(1), LOCAL(2));
    return true;
}

//!bind: function put($this : Map<T0, T1>, $key : T0, $value : T1) : Boolean
static inline bool map_put(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_put);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    auto pair = obj->refValueMap().insert(std::make_pair(LOCAL(1), LOCAL(2)));
    RET_BOOL(pair.second);
}

//!bind: function size($this : Map<T0, T1>) : Int32
static inline bool map_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_size);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    int value = obj->getValueMap().size();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), value));
}

//!bind: function empty($this : Map<T0, T1>) : Boolean
static inline bool map_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_empty);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    bool value = obj->getValueMap().empty();
    RET_BOOL(value);
}

//!bind: function find($this : Map<T0, T1>, $key : T0) : Boolean
static inline bool map_find(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_find);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    RET_BOOL(iter != obj->getValueMap().end());
}

//!bind: function remove($this : Map<T0, T1>, $key : T0) : Boolean
static inline bool map_remove(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_remove);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    unsigned int size = obj->refValueMap().erase(LOCAL(1));
    RET_BOOL(size == 1);
}

//!bind: function swap($this : Map<T0, T1>, $key : T0, $value : T1) : T1
static inline bool map_swap(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_swap);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->refValueMap().find(LOCAL(1));
    if(iter == obj->refValueMap().end()) {
        std::string msg("not found key: ");
        msg += LOCAL(1)->toString(ctx, nullptr);
        ctx.throwError(ctx.getPool().getKeyNotFoundErrorType(), std::move(msg));
        return false;
    }

    DSValue value = LOCAL(2);
    std::swap(iter->second, value);
    RET(std::move(value));
}

//!bind: function clear($this : Map<T0, T1>) : Void
static inline bool map_clear(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_clear);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    obj->initIterator();
    obj->refValueMap().clear();
    return true;
}

//!bind: function $OP_ITER($this : Map<T0, T1>) : Map<T0, T1>
static inline bool map_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_iter);
    typeAs<Map_Object>(LOCAL(0))->initIterator();
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Map<T0, T1>) : Tuple<T0, T1>
static inline bool map_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_next);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    if(!obj->hasNext()) {
        return throwOutOfRangeError(ctx, std::string("map iterator has already reached end"));
    }
    RET(obj->nextElement(ctx));
}

//!bind: function $OP_HAS_NEXT($this : Map<T0, T1>) : Boolean
static inline bool map_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_hasNext);
    RET_BOOL(typeAs<Map_Object>(LOCAL(0))->hasNext());
}

// ###################
// ##     Tuple     ##
// ###################

//!bind: constructor ($this : Tuple<T0>, $arg : T0)
static inline bool tuple_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(tuple_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, DSValue::create<Tuple_Object>(*type));
    typeAs<Tuple_Object>(LOCAL(0))->set(0, LOCAL(1));
    return true;
}

//!bind: function $OP_CMD_ARG($this : Tuple<>) : Array<String>
static inline bool tuple_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(tuple_cmdArg);
    RET(LOCAL(0)->commandArg(ctx, nullptr));
}


// ###################
// ##     Error     ##
// ###################

//!bind: constructor ($this : Error, $message : String)
static inline bool error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, DSValue(Error_Object::newError(ctx, *type, LOCAL(1))));
    return true;
}

//!bind: function message($this : Error) : String
static inline bool error_message(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_message);
    RET(typeAs<Error_Object>(LOCAL(0))->getMessage());
}

//!bind: function backtrace($this : Error) : Void
static inline bool error_backtrace(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_backtrace);
    typeAs<Error_Object>(LOCAL(0))->printStackTrace(ctx);
    return true;
}

//!bind: function name($this : Error) : String
static inline bool error_name(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_name);
    RET(typeAs<Error_Object>(LOCAL(0))->getName(ctx));
}

// ##################
// ##     DBus     ##
// ##################

//!bind: function systemBus($this : DBus) : Bus
static inline bool dbus_systemBus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_systemBus);
    return typeAs<DBus_Object>(LOCAL(0))->getSystemBus(ctx);
}

//!bind: function sessionBus($this : DBus) : Bus
static inline bool dbus_sessionBus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_sessionBus);
    return typeAs<DBus_Object>(LOCAL(0))->getSessionBus(ctx);
}

//!bind: function waitSignal($this : DBus, $obj : DBusObject) : Void
static inline bool dbus_waitSignal(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_waitSignal);
    return typeAs<DBus_Object>(LOCAL(0))->waitSignal(ctx);
}

//!bind: function available($this : DBus) : Boolean
static inline bool dbus_available(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_available);
    RET_BOOL(typeAs<DBus_Object>(LOCAL(0))->supportDBus());
}

//!bind: function getService($this : DBus, $proxy : DBusObject) : Service
static inline bool dbus_getSrv(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_getSrv);
    return typeAs<DBus_Object>(LOCAL(0))->getServiceFromProxy(ctx, LOCAL(1));
}

//!bind: function getObjectPath($this : DBus, $proxy : DBusObject) : ObjectPath
static inline bool dbus_getPath(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_getPath);
    return typeAs<DBus_Object>(LOCAL(0))->getObjectPathFromProxy(ctx, LOCAL(1));
}

//!bind: function getIfaces($this : DBus, $proxy : DBusObject) : Array<String>
static inline bool dbus_getIface(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_getIface);
    return typeAs<DBus_Object>(LOCAL(0))->getIfaceListFromProxy(ctx, LOCAL(1));
}

//!bind: function introspect($this : DBus, $proxy : DBusObject) : String
static inline bool dbus_introspect(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_introspect);
    return typeAs<DBus_Object>(LOCAL(0))->introspectProxy(ctx, LOCAL(1));
}

// #################
// ##     Bus     ##
// #################

//!bind: function service($this : Bus, $dest : String) : Service
static inline bool bus_service(RuntimeContext &ctx) {
    SUPPRESS_WARNING(bus_service);
    String_Object *strObj = typeAs<String_Object>(LOCAL(1));
    return typeAs<Bus_Object>(LOCAL(0))->service(ctx, std::string(strObj->getValue()));
}

//!bind: function listNames($this : Bus) : Array<String>
static inline bool bus_listNames(RuntimeContext &ctx) {
    SUPPRESS_WARNING(bus_listNames);
    return typeAs<Bus_Object>(LOCAL(0))->listNames(ctx, false);
}

//!bind: function listActiveNames($this : Bus) : Array<String>
static inline bool bus_listActiveNames(RuntimeContext &ctx) {
    SUPPRESS_WARNING(bus_listActiveNames);
    return typeAs<Bus_Object>(LOCAL(0))->listNames(ctx, true);
}

// #####################
// ##     Service     ##
// #####################

//!bind: function object($this : Service, $path : ObjectPath) : DBusObject
static inline bool service_object(RuntimeContext &ctx) {
    SUPPRESS_WARNING(service_object);
    return typeAs<Service_Object>(LOCAL(0))->object(ctx, LOCAL(1));
}


} //namespace core
} //namespace ydsh



#endif //YDSH_CORE_BUILTIN_H
