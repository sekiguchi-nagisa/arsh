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

#include <ydsh/ydsh.h>
#include "core.h"
#include "object.h"
#include "signals.h"
#include "misc/unicode.hpp"
#include "misc/num.h"

// helper macro
#define LOCAL(index) (getLocal(ctx, index))
#define EXTRACT_LOCAL(index) (extractLocal(ctx, index))
#define RET(value) return value
#define RET_BOOL(value) return ((value) ? getTrueObj(ctx) : getFalseObj(ctx))
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
YDSH_METHOD basic_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue(); \
    T result = left OPERATOR right; \
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result)); \
}

#define GEN_RELATE_OP(NAME, OPERATOR) \
template <typename T> \
YDSH_METHOD relate_##NAME(RuntimeContext &ctx) { \
    using ObjType = ObjTypeStub<T>; \
    auto left = (T) typeAs<ObjType>(LOCAL(0))->getValue(); \
    auto right = (T) typeAs<ObjType>(LOCAL(1))->getValue(); \
    bool result = left OPERATOR right; \
    RET_BOOL(result); \
}

#define GEN_UNARY_OP(NAME, OPERATOR) \
template <typename T> \
YDSH_METHOD unary_##NAME(RuntimeContext &ctx) { \
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
        raiseError(ctx, getPool(ctx).get(TYPE::ArithmeticError), "zero division");
        return false;
    }
    return true;
}

static inline bool checkZeroMod(RuntimeContext &ctx, int right) {
    if(right == 0) {
        raiseError(ctx, getPool(ctx).get(TYPE::ArithmeticError), "zero modulo");
        return false;
    }
    return true;
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
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result));
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
    RET(DSValue::create<ObjType>(*(typeAs<ObjType>(LOCAL(0))->getType()), result));
}


// #################
// ##     Any     ##
// #################

//!bind: function $OP_STR($this : Any) : String
YDSH_METHOD to_str(RuntimeContext & ctx) {
    SUPPRESS_WARNING(to_str);
    RET(LOCAL(0)->str(ctx));
}

//!bind: function $OP_INTERP($this : Any) : String
YDSH_METHOD to_interp(RuntimeContext & ctx) {
    SUPPRESS_WARNING(to_interp);
    RET(LOCAL(0)->interp(ctx, nullptr));
}


// ##################
// ##     Byte     ##
// ##################

//!bind: function $OP_PLUS($this: Byte) : Byte
YDSH_METHOD byte_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Byte) : Byte
YDSH_METHOD byte_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_minus);
    RET(unary_MINUS<unsigned char>(ctx));
}

//!bind: function $OP_NOT($this : Byte) : Byte
YDSH_METHOD byte_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_not);
    RET(unary_NOT<unsigned char>(ctx));
}


// ###################
// ##     Int16     ##
// ###################

//!bind: function $OP_PLUS($this: Int16) : Int16
YDSH_METHOD int16_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Int16) : Int16
YDSH_METHOD int16_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_minus);
    RET(unary_MINUS<short>(ctx));
}

//!bind: function $OP_NOT($this : Int16) : Int16
YDSH_METHOD int16_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_not);
    RET(unary_NOT<short>(ctx));
}


// ####################
// ##     Uint16     ##
// ####################

//!bind: function $OP_PLUS($this: Uint16) : Uint16
YDSH_METHOD uint16_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Uint16) : Uint16
YDSH_METHOD uint16_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_minus);
    RET(unary_MINUS<unsigned short>(ctx));
}

//!bind: function $OP_NOT($this : Uint16) : Uint16
YDSH_METHOD uint16_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_not);
    RET(unary_NOT<unsigned short>(ctx));
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
    RET(unary_MINUS<int>(ctx));
}

//!bind: function $OP_NOT($this : Int32) : Int32
YDSH_METHOD int_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_not);
    RET(unary_NOT<int>(ctx));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_add);
    RET(basic_ADD<int>(ctx));
}

//!bind: function $OP_SUB($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_sub);
    RET(basic_SUB<int>(ctx));
}

//!bind: function $OP_MUL($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mul);
    RET(basic_MUL<int>(ctx));
}

//!bind: function $OP_DIV($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_div);
    RET(basic_div<int>(ctx));
}

//!bind: function $OP_MOD($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mod);
    RET(basic_mod<int>(ctx));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_lt);
    RET(relate_LT<int>(ctx));
}

//!bind: function $OP_GT($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_gt);
    RET(relate_GT<int>(ctx));
}

//!bind: function $OP_LE($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_le);
    RET(relate_LE<int>(ctx));
}

//!bind: function $OP_GE($this : Int32, $target : Int32) : Boolean
YDSH_METHOD int_2_int_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ge);
    RET(relate_GE<int>(ctx));
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_and);
    RET(basic_AND<int>(ctx));
}

//!bind: function $OP_OR($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_or);
    RET(basic_OR<int>(ctx));
}

//!bind: function $OP_XOR($this : Int32, $target : Int32) : Int32
YDSH_METHOD int_2_int_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_xor);
    RET(basic_XOR<int>(ctx));
}


// ####################
// ##     Uint32     ##
// ####################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Uint32) : Uint32
YDSH_METHOD uint_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Uint32) : Uint32
YDSH_METHOD uint_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_minus);
    RET(unary_MINUS<unsigned int>(ctx));
}

//!bind: function $OP_NOT($this : Uint32) : Uint32
YDSH_METHOD uint_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_not);
    RET(unary_NOT<unsigned int>(ctx));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_add);
    RET(basic_ADD<unsigned int>(ctx));
}

//!bind: function $OP_SUB($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_sub);
    RET(basic_SUB<unsigned int>(ctx));
}

//!bind: function $OP_MUL($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_mul);
    RET(basic_MUL<unsigned int>(ctx));
}

//!bind: function $OP_DIV($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_div);
    RET(basic_div<unsigned int>(ctx));
}

//!bind: function $OP_MOD($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_mod);
    RET(basic_mod<unsigned int>(ctx));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Uint32, $target : Uint32) : Boolean
YDSH_METHOD uint_2_uint_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Uint32, $target : Uint32) : Boolean
YDSH_METHOD uint_2_uint_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Uint32, $target : Uint32) : Boolean
YDSH_METHOD uint_2_uint_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_lt);
    RET(relate_LT<unsigned int>(ctx));
}

//!bind: function $OP_GT($this : Uint32, $target : Uint32) : Boolean
YDSH_METHOD uint_2_uint_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_gt);
    RET(relate_GT<unsigned int>(ctx));
}

//!bind: function $OP_LE($this : Uint32, $target : Uint32) : Boolean
YDSH_METHOD uint_2_uint_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_le);
    RET(relate_LE<unsigned int>(ctx));
}

//!bind: function $OP_GE($this : Uint32, $target : Uint32) : Boolean
YDSH_METHOD uint_2_uint_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_ge);
    RET(relate_GE<unsigned int>(ctx));
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_and);
    RET(basic_AND<unsigned int>(ctx));
}

//!bind: function $OP_OR($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_or);
    RET(basic_OR<unsigned int>(ctx));
}

//!bind: function $OP_XOR($this : Uint32, $target : Uint32) : Uint32
YDSH_METHOD uint_2_uint_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_xor);
    RET(basic_XOR<unsigned int>(ctx));
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
    RET(basic_ADD<long>(ctx));
}

//!bind: function $OP_SUB($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_sub);
    RET(basic_SUB<long>(ctx));
}

//!bind: function $OP_MUL($this : Int64, $target : Int64) : Int64
YDSH_METHOD int64_2_int64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mul);
    RET(basic_MUL<long>(ctx));
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
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_lt);
    RET(relate_LT<long>(ctx));
}

//!bind: function $OP_GT($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_gt);
    RET(relate_GT<long>(ctx));
}

//!bind: function $OP_LE($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_le);
    RET(relate_LE<long>(ctx));
}

//!bind: function $OP_GE($this : Int64, $target : Int64) : Boolean
YDSH_METHOD int64_2_int64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ge);
    RET(relate_GE<long>(ctx));
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

// ####################
// ##     Uint64     ##
// ####################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Uint64) : Uint64
YDSH_METHOD uint64_plus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Uint64) : Uint64
YDSH_METHOD uint64_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_minus);
    RET(unary_MINUS<unsigned long>(ctx));
}

//!bind: function $OP_NOT($this : Uint64) : Uint64
YDSH_METHOD uint64_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_not);
    RET(unary_NOT<unsigned long>(ctx));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_add);
    RET(basic_ADD<unsigned long>(ctx));
}

//!bind: function $OP_SUB($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_sub);
    RET(basic_SUB<unsigned long>(ctx));
}

//!bind: function $OP_MUL($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_mul);
    RET(basic_MUL<unsigned long>(ctx));
}

//!bind: function $OP_DIV($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_div);
    RET(basic_div<unsigned long>(ctx));
}

//!bind: function $OP_MOD($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_mod);
    RET(basic_mod<unsigned long>(ctx));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Uint64, $target : Uint64) : Boolean
YDSH_METHOD uint64_2_uint64_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Uint64, $target : Uint64) : Boolean
YDSH_METHOD uint64_2_uint64_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Uint64, $target : Uint64) : Boolean
YDSH_METHOD uint64_2_uint64_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_lt);
    RET(relate_LT<unsigned long>(ctx));
}

//!bind: function $OP_GT($this : Uint64, $target : Uint64) : Boolean
YDSH_METHOD uint64_2_uint64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_gt);
    RET(relate_GT<unsigned long>(ctx));
}

//!bind: function $OP_LE($this : Uint64, $target : Uint64) : Boolean
YDSH_METHOD uint64_2_uint64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_le);
    RET(relate_LE<unsigned long>(ctx));
}

//!bind: function $OP_GE($this : Uint64, $target : Uint64) : Boolean
YDSH_METHOD uint64_2_uint64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_ge);
    RET(relate_GE<unsigned long>(ctx));
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_and);
    RET(basic_AND<unsigned long>(ctx));
}

//!bind: function $OP_OR($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_or);
    RET(basic_OR<unsigned long>(ctx));
}

//!bind: function $OP_XOR($this : Uint64, $target : Uint64) : Uint64
YDSH_METHOD uint64_2_uint64_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_xor);
    RET(basic_XOR<unsigned long>(ctx));
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
    double left = typeAs<Float_Object>(LOCAL(0))->getValue();
    double right = typeAs<Float_Object>(LOCAL(1))->getValue();
    double value = left / right;
    RET(DSValue::create<Float_Object>(getPool(ctx).get(TYPE::Float), value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_eq);
    RET_BOOL(LOCAL(0)->equals(LOCAL(1)));
}

//!bind: function $OP_NE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ne);
    RET_BOOL(!LOCAL(0)->equals(LOCAL(1)));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_lt);
    RET(relate_LT<double>(ctx));
}

//!bind: function $OP_GT($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_gt);
    RET(relate_GT<double>(ctx));
}

//!bind: function $OP_LE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_le);
    RET(relate_LE<double>(ctx));
}

//!bind: function $OP_GE($this : Float, $target : Float) : Boolean
YDSH_METHOD float_2_float_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ge);
    RET(relate_GE<double>(ctx));
}

// =====  additional float op  ======

//!bind: function isNan($this : Float): Boolean
YDSH_METHOD float_isNan(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isNan);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue();
    RET_BOOL(std::isnan(value));
}

//!bind: function isInf($this : Float): Boolean
YDSH_METHOD float_isInf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isInf);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue();
    RET_BOOL(std::isinf(value));
}

//!bind: function isFinite($this : Float): Boolean
YDSH_METHOD float_isFinite(RuntimeContext &ctx) {
    SUPPRESS_WARNING(float_isFinite);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue();
    RET_BOOL(std::isfinite(value));
}


// #####################
// ##     Boolean     ##
// #####################

//!bind: function $OP_NOT($this : Boolean) : Boolean
YDSH_METHOD boolean_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_not);
    RET_BOOL(!typeAs<Boolean_Object>(LOCAL(0))->getValue());
}

//!bind: function $OP_EQ($this : Boolean, $target : Boolean) : Boolean
YDSH_METHOD boolean_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_eq);
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function $OP_NE($this : Boolean, $target : Boolean) : Boolean
YDSH_METHOD boolean_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_ne);
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}


// ####################
// ##     String     ##
// ####################

//!bind: function $OP_EQ($this : String, $target : String) : Boolean
YDSH_METHOD string_eq(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_eq);
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function $OP_NE($this : String, $target : String) : Boolean
YDSH_METHOD string_ne(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_ne);
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET_BOOL(r);
}

static int cmpstring(DSState &ctx) {
    auto *str1 = typeAs<String_Object>(LOCAL(0));
    auto *str2 = typeAs<String_Object>(LOCAL(1));
    unsigned int size = std::min(str1->size(), str2->size());
    return memcmp(str1->getValue(), str2->getValue(), size);
}


//!bind: function $OP_LT($this : String, $target : String) : Boolean
YDSH_METHOD string_lt(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lt);
    RET_BOOL(cmpstring(ctx) < 0);
}

//!bind: function $OP_GT($this : String, $target : String) : Boolean
YDSH_METHOD string_gt(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_gt);
    RET_BOOL(cmpstring(ctx) > 0);
}

//!bind: function $OP_LE($this : String, $target : String) : Boolean
YDSH_METHOD string_le(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_le);
    RET_BOOL(cmpstring(ctx) <= 0);
}

//!bind: function $OP_GE($this : String, $target : String) : Boolean
YDSH_METHOD string_ge(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_ge);
    RET_BOOL(cmpstring(ctx) >= 0);
}

//!bind: function size($this : String) : Int32
YDSH_METHOD string_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_size);
    int size = typeAs<String_Object>(LOCAL(0))->size();
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), size));
}

//!bind: function empty($this : String) : Boolean
YDSH_METHOD string_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_empty);
    bool empty = typeAs<String_Object>(LOCAL(0))->empty();
    RET_BOOL(empty);
}

//!bind: function count($this : String) : Int32
YDSH_METHOD string_count(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_count);
    const char *ptr = typeAs<String_Object>(LOCAL(0))->getValue();
    const unsigned int size = typeAs<String_Object>(LOCAL(0))->size();
    unsigned int count = 0;
    for(unsigned int i = 0; i < size; i = UnicodeUtil::utf8NextPos(i, ptr[i])) {
        count++;
    }
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), count));
}

/**
 * return always false.
 */
static void raiseOutOfRangeError(RuntimeContext &ctx, std::string &&message) {
    raiseError(ctx, getPool(ctx).get(TYPE::OutOfRangeError), std::move(message));
}

//!bind: function $OP_GET($this : String, $index : Int32) : String
YDSH_METHOD string_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_get);
    auto strObj = typeAs<String_Object>(LOCAL(0));
    const int pos = typeAs<Int_Object>(LOCAL(1))->getValue();
    const unsigned int size = strObj->size();

    if(pos >= 0 && static_cast<unsigned int>(pos) < size) {
        const unsigned int limit = pos;
        unsigned int index = 0;
        unsigned int count = 0;
        for(; index < size; index = UnicodeUtil::utf8NextPos(index, strObj->getValue()[index])) {
            if(count == limit) {
                break;
            }
            count++;
        }
        if(count == limit && index < size) {
            unsigned int nextIndex = UnicodeUtil::utf8NextPos(index, strObj->getValue()[index]);
            RET(DSValue::create<String_Object>(
                    getPool(ctx).get(TYPE::String), std::string(strObj->getValue() + index, nextIndex - index)));
        }
    }

    std::string msg("size is ");
    msg += std::to_string(size);
    msg += ", but code position is ";
    msg += std::to_string(pos);
    raiseOutOfRangeError(ctx, std::move(msg));
    RET_ERROR;
}

/**
 * startIndex is inclusive.
 * stopIndex is exclusive.
 */
static DSValue sliceImpl(RuntimeContext &ctx, String_Object *strObj, int startIndex, int stopIndex) {
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
        raiseOutOfRangeError(ctx, std::move(msg));
        RET_ERROR;
    }

    RET(DSValue::create<String_Object>(
            getPool(ctx).get(TYPE::String),
            std::string(strObj->getValue() + startIndex, stopIndex - startIndex)));
}

//!bind: function slice($this : String, $start : Int32, $stop : Int32) : String
YDSH_METHOD string_slice(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_slice);
    RET(sliceImpl(ctx, typeAs<String_Object>(LOCAL(0)),
              typeAs<Int_Object>(LOCAL(1))->getValue(),
              typeAs<Int_Object>(LOCAL(2))->getValue()));
}

//!bind: function from($this : String, $start : Int32) : String
YDSH_METHOD string_sliceFrom(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_sliceFrom);
    auto strObj = typeAs<String_Object>(LOCAL(0));
    RET(sliceImpl(ctx, strObj, typeAs<Int_Object>(LOCAL(1))->getValue(), strObj->size()));
}

//!bind: function to($this : String, $stop : Int32) : String
YDSH_METHOD string_sliceTo(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_sliceTo);
    auto strObj = typeAs<String_Object>(LOCAL(0));
    RET(sliceImpl(ctx, strObj, 0, typeAs<Int_Object>(LOCAL(1))->getValue()));
}

static void *xmemmem(const void *haystack, size_t haystackSize, const void *needle, size_t needleSize) {
    if(needleSize == 0) {
        return const_cast<void *>(haystack);
    }
    return memmem(haystack, haystackSize, needle, needleSize);
}

static bool startsWith(const String_Object *thisObj, const String_Object *targetObj, int offset) {
    if(offset < 0) {
        return false;
    }
    const char *thisStr = thisObj->getValue() + offset;
    const char *targetStr = targetObj->getValue();
    const unsigned int thisSize = thisObj->size() - offset;
    const unsigned int targetSize = targetObj->size();

    return xmemmem(thisStr, thisSize, targetStr, targetSize) == thisStr;
}

//!bind: function startsWith($this : String, $target : String) : Boolean
YDSH_METHOD string_startsWith(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_startsWith);
    auto thisObj = typeAs<String_Object>(LOCAL(0));
    auto targetObj = typeAs<String_Object>(LOCAL(1));

    bool r = startsWith(thisObj, targetObj, 0);
    RET_BOOL(r);
}

//!bind: function endsWith($this : String, $target : String) : Boolean
YDSH_METHOD string_endsWith(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_endsWith);
    auto thisObj = typeAs<String_Object>(LOCAL(0));
    auto targetObj = typeAs<String_Object>(LOCAL(1));

    bool r = startsWith(thisObj, targetObj, thisObj->size() - targetObj->size());
    RET_BOOL(r);
}

//!bind: function indexOf($this : String, $target : String) : Int32
YDSH_METHOD string_indexOf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_indexOf);
    auto *thisObj = typeAs<String_Object>(LOCAL(0));
    auto *targetObj = typeAs<String_Object>(LOCAL(1));

    void *ptr = xmemmem(thisObj->getValue(), thisObj->size(),
                        targetObj->getValue(), targetObj->size());
    int index = -1;
    if(ptr != nullptr) {
        index = reinterpret_cast<const char *>(ptr) - thisObj->getValue();
    }
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), index));
}

//!bind: function lastIndexOf($this : String, $target : String) : Int32
YDSH_METHOD string_lastIndexOf(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lastIndexOf);
    const char *thisStr = typeAs<String_Object>(LOCAL(0))->getValue();
    const char *targetStr = typeAs<String_Object>(LOCAL(1))->getValue();
    const unsigned int thisSize = typeAs<String_Object>(LOCAL(0))->size();
    const unsigned int targetSize = typeAs<String_Object>(LOCAL(1))->size();
    const char *end = thisStr + thisSize;

    int index = -1;
    for(const char *ptr = thisStr; ptr != end; ptr++) {
        ptr = reinterpret_cast<const char *>(xmemmem(ptr, thisSize - (ptr - thisStr), targetStr, targetSize));
        if(ptr == nullptr) {
            break;
        }
        index = ptr - thisStr;
    }

    if(thisSize == targetSize && targetSize == 0) {
        index = 0;
    }
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), index));
}

//!bind: function split($this : String, $delim : String) : Array<String>
YDSH_METHOD string_split(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_split);
    const char *thisStr = typeAs<String_Object>(LOCAL(0))->getValue();
    const char *delimStr = typeAs<String_Object>(LOCAL(1))->getValue();
    const unsigned int thisSize = typeAs<String_Object>(LOCAL(0))->size();
    const unsigned int delimSize = typeAs<String_Object>(LOCAL(1))->size();

    auto results = DSValue::create<Array_Object>(getPool(ctx).get(TYPE::StringArray));
    auto ptr = typeAs<Array_Object>(results);

    const char *remain = thisStr;
    if(delimSize > 0) {
        while(true) {
            auto *ret = reinterpret_cast<const char *>(
                    xmemmem(remain, thisSize - (remain - thisStr), delimStr, delimSize));
            if(ret == nullptr) {
                break;
            }
            ptr->append(DSValue::create<String_Object>(getPool(ctx).get(TYPE::String), std::string(remain, ret - remain)));
            remain = ret + delimSize;
        }
    }

    if(remain == thisStr) {
        ptr->append(LOCAL(0));
    } else if(remain != thisStr + thisSize) {
        ptr->append(DSValue::create<String_Object>(getPool(ctx).get(TYPE::String), std::string(remain, thisSize - (remain - thisStr))));
    }

    RET(results);
}

//!bind: function toInt32($this : String) : Option<Int32>
YDSH_METHOD string_toInt32(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toInt32);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    long value = convertToInt64(str, status, false);

    // range check
    if(status != 0 || value > INT32_MAX || value < INT32_MIN) {
        status = 1;
        value = 0;
    }

    RET(status == 0 ? DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), (int)value) : DSValue::createInvalid());
}

//!bind: function toUint32($this : String) : Option<Uint32>
YDSH_METHOD string_toUint32(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toUint32);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    long value = convertToInt64(str, status, false);

    // range check
    if(status != 0 || value > UINT32_MAX || value < 0) {
        status = 1;
        value = 0;
    }
    RET(status == 0 ? DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Uint32), (unsigned int)value) : DSValue::createInvalid());
}

//!bind: function toInt64($this : String) : Option<Int64>
YDSH_METHOD string_toInt64(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toInt64);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    long value = convertToInt64(str, status, false);

    RET(status == 0 ? DSValue::create<Long_Object>(getPool(ctx).get(TYPE::Int64), value) : DSValue::createInvalid());
}

//!bind: function toUint64($this : String) : Option<Uint64>
YDSH_METHOD string_toUint64(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toUint64);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    unsigned long value = convertToUint64(str, status, false);

    RET(status == 0 ? DSValue::create<Long_Object>(getPool(ctx).get(TYPE::Uint64), value) : DSValue::createInvalid());
}

//!bind: function toFloat($this : String) : Option<Float>
YDSH_METHOD string_toFloat(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_toFloat);
    const char *str = typeAs<String_Object>(LOCAL(0))->getValue();
    int status = 0;
    double value = convertToDouble(str, status, false);

    RET(status == 0 ? DSValue::create<Float_Object>(getPool(ctx).get(TYPE::Float), value) : DSValue::createInvalid());
}

//!bind: function $OP_ITER($this : String) : StringIter
YDSH_METHOD string_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_iter);
    auto *str = typeAs<String_Object>(LOCAL(0));
    RET(DSValue::create<StringIter_Object>(getPool(ctx).get(TYPE::StringIter), str));
}

static bool regexSearch(const Regex_Object *re, const String_Object *str);

//!bind: function $OP_MATCH($this : String, $re : Regex) : Boolean
YDSH_METHOD string_match(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_match);
    auto *str = typeAs<String_Object>(LOCAL(0));
    auto *re = typeAs<Regex_Object>(LOCAL(1));
    bool r = regexSearch(re, str);
    RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : String, $re : Regex) : Boolean
YDSH_METHOD string_unmatch(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_unmatch);
    auto *str = typeAs<String_Object>(LOCAL(0));
    auto *re = typeAs<Regex_Object>(LOCAL(1));
    bool r = !regexSearch(re, str);
    RET_BOOL(r);
}

//!bind: function realpath($this : String) : Option<String>
YDSH_METHOD string_realpath(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_realpath);
    auto *obj = typeAs<String_Object>(LOCAL(0));
    std::string str = obj->getValue();
    expandTilde(str);
    char *buf = realpath(str.c_str(), nullptr);
    if(buf == nullptr) {
        RET(DSValue::createInvalid());
    }

    str = buf;
    free(buf);
    RET(DSValue::create<String_Object>(*obj->getType(), std::move(str)));
}

//!bind: function lower($this : String) : String
YDSH_METHOD string_lower(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_lower);
    auto *obj = typeAs<String_Object>(LOCAL(0));
    std::string str = obj->getValue();
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    RET(DSValue::create<String_Object>(*obj->getType(), std::move(str)));
}

//!bind: function upper($this : String) : String
YDSH_METHOD string_upper(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_upper);
    auto *obj = typeAs<String_Object>(LOCAL(0));
    std::string str = obj->getValue();
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    RET(DSValue::create<String_Object>(*obj->getType(), std::move(str)));
}

// ########################
// ##     StringIter     ##
// ########################

//!bind: function $OP_NEXT($this : StringIter) : String
YDSH_METHOD stringIter_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(stringIter_next);
    auto strIter = typeAs<StringIter_Object>(LOCAL(0));
    auto strObj = typeAs<String_Object>(strIter->strObj);
    if(strIter->curIndex >= strObj->size()) {
        raiseOutOfRangeError(ctx, std::string("string iterator reach end of string"));
        RET_ERROR;
    }
    unsigned int curIndex = strIter->curIndex;
    strIter->curIndex = UnicodeUtil::utf8NextPos(curIndex, strObj->getValue()[curIndex]);
    if(strIter->curIndex > strObj->size()) {
        fatal("broken string iterator\n");
    }

    unsigned int size = strIter->curIndex - curIndex;
    RET(DSValue::create<String_Object>(
            getPool(ctx).get(TYPE::String), std::string(strObj->getValue() + curIndex, size)));
}

//!bind: function $OP_HAS_NEXT($this : StringIter) : Boolean
YDSH_METHOD stringIter_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(stringIter_hasNext);
    auto strIter = typeAs<StringIter_Object>(LOCAL(0));
    bool r = strIter->curIndex < typeAs<String_Object>(strIter->strObj)->size();
    RET_BOOL(r);
}

// ###################
// ##     Regex     ##
// ###################

static bool regexSearch(const Regex_Object *re, const String_Object *str) {
    int ovec[1];
    int match = pcre_exec(re->getRe().get(), nullptr, str->getValue(), str->size(), 0, 0, ovec, arraySize(ovec));
    return match >= 0;
}

//!bind: constructor ($this : Regex, $str : String)
YDSH_METHOD regex_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_init);
    auto *str = typeAs<String_Object>(LOCAL(1));
    const char *errorStr;
    auto re = compileRegex(str->getValue(), errorStr, 0);
    if(!re) {
        raiseError(ctx, getPool(ctx).get(TYPE::RegexSyntaxError), std::string(errorStr));
        RET_ERROR;
    }
    setLocal(ctx, 0, DSValue::create<Regex_Object>(getPool(ctx).get(TYPE::Regex), std::move(re)));
    RET_VOID;
}

//!bind: function $OP_MATCH($this : Regex, $target : String) : Boolean
YDSH_METHOD regex_search(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_search);
    auto *re = typeAs<Regex_Object>(LOCAL(0));
    auto *str = typeAs<String_Object>(LOCAL(1));
    bool r = regexSearch(re, str);
    RET_BOOL(r);
}

//!bind: function $OP_UNMATCH($this : Regex, $target : String) : Boolean
YDSH_METHOD regex_unmatch(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_unmatch);
    auto *re = typeAs<Regex_Object>(LOCAL(0));
    auto *str = typeAs<String_Object>(LOCAL(1));
    bool r = !regexSearch(re, str);
    RET_BOOL(r);
}

//!bind: function match($this: Regex, $target : String) : Array<String>
YDSH_METHOD regex_match(RuntimeContext &ctx) {
    SUPPRESS_WARNING(regex_match);
    auto *re = typeAs<Regex_Object>(LOCAL(0));
    auto *str = typeAs<String_Object>(LOCAL(1));

    int captureSize;
    pcre_fullinfo(re->getRe().get(), nullptr, PCRE_INFO_CAPTURECOUNT, &captureSize);
    auto *ovec = static_cast<int *>(malloc(sizeof(int) * (captureSize + 1) * 3));
    int matchSize = pcre_exec(re->getRe().get(), nullptr, str->getValue(), str->size(), 0, 0, ovec, (captureSize + 1) * 3);

    auto ret = DSValue::create<Array_Object>(getPool(ctx).get(TYPE::StringArray));
    auto *array = typeAs<Array_Object>(ret);

    if(matchSize > 0) {
        array->refValues().reserve(matchSize);
    }
    for(int i = 0; i < matchSize; i++) {
        unsigned int size = ovec[i * 2 + 1] - ovec[i * 2];
        auto v = size == 0 ? getEmptyStrObj(ctx) :
                 DSValue::create<String_Object>(getPool(ctx).get(TYPE::String),
                                                std::string(str->getValue() + ovec[i * 2], size));
        array->refValues().push_back(std::move(v));
    }
    free(ovec);

    RET(ret);
}

// ####################
// ##     Signal     ##
// ####################

//!bind: function name($this : Signal) : String
YDSH_METHOD signal_name(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signal_name);
    auto *obj = typeAs<Int_Object>(LOCAL(0));
    const char *name = getSignalName(obj->getValue());
    assert(name != nullptr);
    RET(DSValue::create<String_Object>(getPool(ctx).get(TYPE::String), name));
}

//!bind: function value($this : Signal) : Int32
YDSH_METHOD signal_value(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signal_value);
    auto *obj = typeAs<Int_Object>(LOCAL(0));
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), obj->getValue()));
}

//!bind: function kill($this : Signal, $pid : Int32) : Void
YDSH_METHOD signal_kill(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signal_kill);
    int sigNum = typeAs<Int_Object>(LOCAL(0))->getValue();
    int pid = typeAs<Int_Object>(LOCAL(1))->getValue();
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
    int left = typeAs<Int_Object>(LOCAL(0))->getValue();
    int right = typeAs<Int_Object>(LOCAL(1))->getValue();
    bool ret = left == right;
    RET_BOOL(ret);
}

//!bind: function $OP_NE($this : Signal, $target : Signal) : Boolean
YDSH_METHOD signal_ne(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signal_ne);
    int left = typeAs<Int_Object>(LOCAL(0))->getValue();
    int right = typeAs<Int_Object>(LOCAL(1))->getValue();
    bool ret = left != right;
    RET_BOOL(ret);
}


// #####################
// ##     Signals     ##
// #####################

//!bind: function $OP_GET($this : Signals, $s : Signal) : Func<Void,[Signal]>
YDSH_METHOD signals_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_get);
    int sigNum = typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(getSignalHandler(ctx, sigNum));
}

//!bind: function $OP_SET($this : Signals, $s : Signal, $action : Func<Void,[Signal]>) : Void
YDSH_METHOD signals_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_set);
    auto *obj = typeAs<Int_Object>(LOCAL(1));
    installSignalHandler(ctx, obj->getValue(), LOCAL(2));
    RET_VOID;
}

//!bind: function signal($this : Signals, $key : String) : Option<Signal>
YDSH_METHOD signals_signal(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_signal);
    const char *key = typeAs<String_Object>(LOCAL(1))->getValue();
    int sigNum = getSignalNum(key);
    if(sigNum < 0) {
        RET(DSValue::createInvalid());
    }
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Signal), sigNum));
}

//!bind: function list($this : Signals) : Array<Signal>
YDSH_METHOD signals_list(RuntimeContext &ctx) {
    SUPPRESS_WARNING(signals_list);

    auto &type = getPool(ctx).createReifiedType(getPool(ctx).getArrayTemplate(), {&getPool(ctx).get(TYPE::Signal)});
    auto v = DSValue::create<Array_Object>(type);
    auto *array = typeAs<Array_Object>(v);
    for(auto &e : getUniqueSignalList()) {
        array->append(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Signal), e));
    }
    RET(v);
}


// ###################
// ##     Array     ##
// ###################

//!bind: constructor ($this : Array<T0>)
YDSH_METHOD array_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_init);
    DSType *type = LOCAL(0)->getType();
    setLocal(ctx, 0, DSValue::create<Array_Object>(*type));
    RET_VOID;
}

// check index range and throw exception.
static bool checkRange(RuntimeContext &ctx, int index, int size) {
    if(index < 0 || index >= size) {
        std::string message("size is ");
        message += std::to_string(size);
        message += ", but index is ";
        message += std::to_string(index);
        raiseOutOfRangeError(ctx, std::move(message));
        return false;
    }
    return true;
}

//!bind: function $OP_GET($this : Array<T0>, $index : Int32) : T0
YDSH_METHOD array_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_get);

    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int size = obj->getValues().size();
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!checkRange(ctx, index, size)) {
        RET_ERROR;
    }
    RET(obj->getValues()[index]);
}

//!bind: function get($this : Array<T0>, $index : Int32) : Option<T0>
YDSH_METHOD array_get2(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_get);

    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int size = obj->getValues().size();
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(index < 0 || index >= size) {
        RET(DSValue::createInvalid());
    }
    RET(obj->getValues()[index]);
}

//!bind: function $OP_SET($this : Array<T0>, $index : Int32, $value : T0) : Void
YDSH_METHOD array_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_set);

    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int size = obj->getValues().size();
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!checkRange(ctx, index, size)) {
        RET_ERROR;
    }
    obj->set(index, EXTRACT_LOCAL(2));
    RET_VOID;
}

static bool array_peekImpl(RuntimeContext &ctx, DSValue &value) {
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    if(obj->getValues().empty()) {
        raiseOutOfRangeError(ctx, std::string("Array size is 0"));
        return false;
    }
    value = obj->refValues().back();
    return true;
}

//!bind: function peek($this : Array<T0>) : T0
YDSH_METHOD array_peek(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_peek);
    DSValue value;
    array_peekImpl(ctx, value);
    return value;
}

static bool array_insertImpl(DSState &ctx, int index, const DSValue &v) {
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int size = obj->getValues().size();
    if(size == INT32_MAX) {
        raiseOutOfRangeError(ctx, std::string("reach Array size limit"));
        return false;
    }
    if(index != size && !checkRange(ctx, index, size)) {
        return false;
    }
    obj->refValues().insert(obj->refValues().begin() + static_cast<unsigned int>(index), v);
    return true;
}

static bool array_pushImpl(RuntimeContext &ctx) {
    int index = typeAs<Array_Object>(LOCAL(0))->getValues().size();
    return array_insertImpl(ctx, index, LOCAL(1));
}

//!bind: function push($this : Array<T0>, $value : T0) : Void
YDSH_METHOD array_push(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_push);
    array_pushImpl(ctx);
    RET_VOID;
}

//!bind: function pop($this : Array<T0>) : T0
YDSH_METHOD array_pop(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_pop);
    DSValue v;
    if(!array_peekImpl(ctx, v)) {
        RET_ERROR;
    }
    typeAs<Array_Object>(LOCAL(0))->refValues().pop_back();
    RET(v);
}

//!bind: function insert($this : Array<T0>, $index : Int32, $value : T0) : Void
YDSH_METHOD array_insert(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_insert);
    if(!array_insertImpl(ctx, typeAs<Int_Object>(LOCAL(1))->getValue(), LOCAL(2))) {
        RET_ERROR;
    }
    RET_VOID;
}

//!bind: function add($this : Array<T0>, $value : T0) : Array<T0>
YDSH_METHOD array_add(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_add);
    if(!array_pushImpl(ctx)) {
        RET_ERROR;
    }
    RET(LOCAL(0));
}

//!bind: function extend($this : Array<T0>, $value : Array<T0>) : Array<T0>
YDSH_METHOD array_extend(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_extend);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    auto *value = typeAs<Array_Object>(LOCAL(1));
    if(obj != value) {
        unsigned int valueSize = value->getValues().size();
        for(unsigned int i = 0; i < valueSize; i++) {
            if(obj->getValues().size() == INT32_MAX) {
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
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!checkRange(ctx, index, obj->getValues().size())) {
        RET_ERROR;
    }
    DSValue value = LOCAL(2);
    std::swap(obj->refValues()[index], value);
    RET(value);
}

/**
 * startIndex is inclusive.
 * stopIndex is exclusive.
 */
static DSValue slice(RuntimeContext &ctx, Array_Object *arrayObj, int startIndex, int stopIndex) {
    const unsigned int size = arrayObj->getValues().size();

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
        raiseOutOfRangeError(ctx, std::move(msg));
        RET_ERROR;
    }

    auto begin = arrayObj->getValues().begin() + startIndex;
    auto end = arrayObj->getValues().begin() + stopIndex;
    std::vector<DSValue> values(begin, end);
    RET(DSValue::create<Array_Object>(*arrayObj->getType(), std::move(values)));
}

//!bind: function slice($this : Array<T0>, $from : Int32, $to : Int32) : Array<T0>
YDSH_METHOD array_slice(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_slice);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int start = typeAs<Int_Object>(LOCAL(1))->getValue();
    int stop = typeAs<Int_Object>(LOCAL(2))->getValue();
    return slice(ctx, obj, start, stop);
}

//!bind: function from($this : Array<T0>, $from : Int32) : Array<T0>
YDSH_METHOD array_sliceFrom(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_sliceFrom);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int start = typeAs<Int_Object>(LOCAL(1))->getValue();
    return slice(ctx, obj, start, obj->getValues().size());
}

//!bind: function to($this : Array<T0>, $to : Int32) : Array<T0>
YDSH_METHOD array_sliceTo(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_sliceTo);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    int stop = typeAs<Int_Object>(LOCAL(1))->getValue();
    return slice(ctx, obj, 0, stop);
}

//!bind: function size($this : Array<T0>) : Int32
YDSH_METHOD array_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_size);
    int size = typeAs<Array_Object>(LOCAL(0))->getValues().size();
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), size));
}

//!bind: function empty($this : Array<T0>) : Boolean
YDSH_METHOD array_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_empty);
    bool empty = typeAs<Array_Object>(LOCAL(0))->getValues().empty();
    RET_BOOL(empty);
}

//!bind: function clear($this : Array<T0>) : Void
YDSH_METHOD array_clear(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_clear);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    obj->initIterator();
    obj->refValues().clear();
    RET_VOID;
}

//!bind: function $OP_ITER($this : Array<T0>) : Array<T0>
YDSH_METHOD array_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_iter);
    typeAs<Array_Object>(LOCAL(0))->initIterator();
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Array<T0>) : T0
YDSH_METHOD array_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_next);
    auto *obj = typeAs<Array_Object>(LOCAL(0));
    if(!obj->hasNext()) {
        raiseOutOfRangeError(ctx, std::string("array iterator has already reached end"));
        RET_ERROR;
    }
    RET(obj->nextElement());
}

//!bind: function $OP_HAS_NEXT($this : Array<T0>) : Boolean
YDSH_METHOD array_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_hasNext);
    RET_BOOL(typeAs<Array_Object>(LOCAL(0))->hasNext());
}

//!bind: function $OP_CMD_ARG($this : Array<T0>) : Array<String>
YDSH_METHOD array_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_cmdArg);
    RET(LOCAL(0)->commandArg(ctx, nullptr));
}


// #################
// ##     Map     ##
// #################

//!bind: constructor ($this : Map<T0, T1>)
YDSH_METHOD map_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_init);
    DSType *type = LOCAL(0)->getType();
    setLocal(ctx, 0, DSValue::create<Map_Object>(*type));
    RET_VOID;
}

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
YDSH_METHOD map_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_get);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    if(iter == obj->getValueMap().end()) {
        std::string msg("not found key: ");
        msg += LOCAL(1)->toString(ctx, nullptr);
        raiseError(ctx, getPool(ctx).get(TYPE::KeyNotFoundError), std::move(msg));
        RET_ERROR;
    }
    RET(iter->second);
}

//!bind: function $OP_SET($this : Map<T0, T1>, $key : T0, $value : T1) : Void
YDSH_METHOD map_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_set);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    obj->set(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
    RET_VOID;
}

//!bind: function put($this : Map<T0, T1>, $key : T0, $value : T1) : Option<T1>
YDSH_METHOD map_put(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_put);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    auto v = obj->set(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
    RET(v);
}

//!bind: function default($this : Map<T0, T1>, $key : T0, $value : T1) : T1
YDSH_METHOD map_default(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_default);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    auto v = obj->setDefault(EXTRACT_LOCAL(1), EXTRACT_LOCAL(2));
    RET(v);
}

//!bind: function size($this : Map<T0, T1>) : Int32
YDSH_METHOD map_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_size);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    int value = obj->getValueMap().size();
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), value));
}

//!bind: function empty($this : Map<T0, T1>) : Boolean
YDSH_METHOD map_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_empty);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    bool value = obj->getValueMap().empty();
    RET_BOOL(value);
}

//!bind: function get($this : Map<T0, T1>, $key : T0) : Option<T1>
YDSH_METHOD map_find(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_find);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    RET(iter != obj->getValueMap().end() ? iter->second : DSValue::createInvalid());
}

//!bind: function find($this : Map<T0, T1>, $key : T0) : Boolean
YDSH_METHOD map_find2(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_find2);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    RET_BOOL(iter != obj->getValueMap().end());
}

//!bind: function remove($this : Map<T0, T1>, $key : T0) : Boolean
YDSH_METHOD map_remove(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_remove);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    bool r = obj->remove(LOCAL(1));
    RET_BOOL(r);
}

//!bind: function swap($this : Map<T0, T1>, $key : T0, $value : T1) : T1
YDSH_METHOD map_swap(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_swap);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    DSValue value = LOCAL(2);
    if(!obj->trySwap(LOCAL(1), value)) {
        std::string msg("not found key: ");
        msg += LOCAL(1)->toString(ctx, nullptr);
        raiseError(ctx, getPool(ctx).get(TYPE::KeyNotFoundError), std::move(msg));
        RET_ERROR;
    }
    RET(value);
}

//!bind: function clear($this : Map<T0, T1>) : Void
YDSH_METHOD map_clear(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_clear);
    typeAs<Map_Object>(LOCAL(0))->clear();
    RET_VOID;
}

//!bind: function $OP_ITER($this : Map<T0, T1>) : Map<T0, T1>
YDSH_METHOD map_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_iter);
    typeAs<Map_Object>(LOCAL(0))->initIterator();
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Map<T0, T1>) : Tuple<T0, T1>
YDSH_METHOD map_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_next);
    auto *obj = typeAs<Map_Object>(LOCAL(0));
    if(!obj->hasNext()) {
        raiseOutOfRangeError(ctx, std::string("map iterator has already reached end"));
        RET_ERROR;
    }
    RET(obj->nextElement(ctx));
}

//!bind: function $OP_HAS_NEXT($this : Map<T0, T1>) : Boolean
YDSH_METHOD map_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_hasNext);
    RET_BOOL(typeAs<Map_Object>(LOCAL(0))->hasNext());
}

// ###################
// ##     Tuple     ##
// ###################

//!bind: function $OP_CMD_ARG($this : Tuple<>) : Array<String>
YDSH_METHOD tuple_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(tuple_cmdArg);
    RET(LOCAL(0)->commandArg(ctx, nullptr));
}


// ###################
// ##     Error     ##
// ###################

//!bind: constructor ($this : Error, $message : String)
YDSH_METHOD error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_init);
    DSType *type = LOCAL(0)->getType();
    setLocal(ctx, 0, DSValue(Error_Object::newError(ctx, *type, LOCAL(1))));
    RET_VOID;
}

//!bind: function message($this : Error) : String
YDSH_METHOD error_message(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_message);
    RET(typeAs<Error_Object>(LOCAL(0))->getMessage());
}

//!bind: function backtrace($this : Error) : Void
YDSH_METHOD error_backtrace(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_backtrace);
    typeAs<Error_Object>(LOCAL(0))->printStackTrace(ctx);
    RET_VOID;
}

//!bind: function name($this : Error) : String
YDSH_METHOD error_name(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_name);
    RET(typeAs<Error_Object>(LOCAL(0))->getName());
}

// ####################
// ##     UnixFD     ##
// ####################

//!bind: constructor ($this : UnixFD, $path : String)
YDSH_METHOD fd_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_init);
    const char *path = typeAs<String_Object>(LOCAL(1))->getValue();
    int fd = open(path, O_CREAT | O_RDWR, 0666);
    if(fd != -1) {
        int flag = fcntl(fd, F_GETFD);
        if(flag != -1) {
            if(fcntl(fd, F_SETFD, flag | FD_CLOEXEC) != -1) {
                auto obj = DSValue::create<UnixFD_Object>(getPool(ctx).get(TYPE::UnixFD), fd);
                setLocal(ctx, 0, std::move(obj));
                RET_VOID;
            }
        }
    }
    int e = errno;
    close(fd);
    std::string msg = "open failed: ";
    msg += path;
    raiseSystemError(ctx, e, std::move(msg));
    RET_ERROR;
}

//!bind: function close($this : UnixFD) : Void
YDSH_METHOD fd_close(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_close);
    auto *fdObj = typeAs<UnixFD_Object>(LOCAL(0));
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
    int fd = typeAs<UnixFD_Object>(LOCAL(0))->getValue();
    int newfd = dup(fd);
    if(newfd < 0) {
        int e = errno;
        raiseSystemError(ctx, e, std::to_string(fd));
        RET_ERROR;
    }
    RET(DSValue::create<UnixFD_Object>(getPool(ctx).get(TYPE::UnixFD), newfd));
}

//!bind: function $OP_BOOL($this : UnixFD) : Boolean
YDSH_METHOD fd_bool(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_bool);
    int fd = typeAs<UnixFD_Object>(LOCAL(0))->getValue();
    RET_BOOL(fd != -1);
}

//!bind: function $OP_NOT($this : UnixFD) : Boolean
YDSH_METHOD fd_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(fd_not);
    int fd = typeAs<UnixFD_Object>(LOCAL(0))->getValue();
    RET_BOOL(fd == -1);
}

// #################
// ##     Job     ##
// #################

//!bind: function in($this : Job) : UnixFD
YDSH_METHOD job_in(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_in);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    RET(obj->getInObj());
}

//!bind: function out($this : Job) : UnixFD
YDSH_METHOD job_out(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_out);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    RET(obj->getOutObj());
}

//!bind: function $OP_GET($this : Job, $index : Int32) : UnixFD
YDSH_METHOD job_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_get);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();
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
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    RET_BOOL(obj->poll());
}

//!bind: function wait($this : Job) : Int32
YDSH_METHOD job_wait(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_wait);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    bool jobctrl = hasFlag(DSState_option(&ctx), DS_OPTION_JOB_CONTROL);
    int s = obj->wait(getJobTable(ctx), jobctrl);
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), s));
}

//!bind: function raise($this : Job, $s : Signal) : Void
YDSH_METHOD job_raise(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_raise);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    auto *sig = typeAs<Int_Object>(LOCAL(1));
    obj->getEntry()->send(sig->getValue());
    RET_VOID;
}

//!bind: function detach($this : Job) : Void
YDSH_METHOD job_detach(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_detach);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    getJobTable(ctx).detach(obj->getEntry()->jobID());
    RET_VOID;
}

//!bind: function size($this : Job) : Int32
YDSH_METHOD job_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_size);
    auto *obj = typeAs<Job_Object>(LOCAL(0));
    RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), obj->getEntry()->getProcSize()));
}

//!bind: function pid($this : Job, $index : Int32) : Int32
YDSH_METHOD job_pid(RuntimeContext &ctx) {
    SUPPRESS_WARNING(job_pid);
    auto &entry = typeAs<Job_Object>(LOCAL(0))->getEntry();
    int index = typeAs<Int_Object>(LOCAL(1))->getValue();

    if(index > -1 && static_cast<unsigned int>(index) < entry->getProcSize()) {
        int pid = entry->getPid(index);
        RET(DSValue::create<Int_Object>(getPool(ctx).get(TYPE::Int32), pid));
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
