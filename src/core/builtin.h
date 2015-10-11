/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef CORE_BUILTIN_CPP_
#define CORE_BUILTIN_CPP_

#include <cmath>

#include "RuntimeContext.h"
#include "DSObject.h"

// helper macro
#define LOCAL(index) (ctx.getLocal(index))
#define RET(value) do { ctx.push(value); return true; } while(0)
#define RET_BOOL(value) do { ctx.push((value) ? ctx.getTrueObj() : ctx.getFalseObj()); return true; } while(0)

#define SUPPRESS_WARNING(a) (void)a

/**
 *   //!bind: function <method name>($this : <receiver type>, $param1 : <type1>, $param2? : <type2>, ...) : <return type>
 *   //!bind: constructor <type name>($param1 : <type1>, ....)
 *   $<param name>?  has default value
 */

namespace ydsh {
namespace core {

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
    RET(LOCAL(0)->interp(ctx));
}


// ##################
// ##     Byte     ##
// ##################

//!bind: function $OP_PLUS($this: Byte) : Byte
static inline bool byte_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Byte) : Int16
static inline bool byte_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_minus);
    short value = -typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt16Type(), value));
}

//!bind: function $OP_NOT($this : Byte) : Byte
static inline bool byte_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(byte_not);
    unsigned char value = ~typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getByteType(), value));
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
    short value = -typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt16Type(), value));
}

//!bind: function $OP_NOT($this : Int16) : Int16
static inline bool int16_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(int16_not);
    short value = ~typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt16Type(), value));
}


// ####################
// ##     Uint16     ##
// ####################

//!bind: function $OP_PLUS($this: Uint16) : Uint16
static inline bool uint16_plus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_plus);
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this: Uint16) : Int32
static inline bool uint16_minus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_minus);
    int value = -typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getInt32Type(), value));
}

//!bind: function $OP_NOT($this : Uint16) : Uint16
static inline bool uint16_not(RuntimeContext &ctx) {
    SUPPRESS_WARNING(uint16_not);
    unsigned short value = ~typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint16Type(), value));
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
    int value = -typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_NOT($this : Int32) : Int32
static inline bool int_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_not);
    int value = ~typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_add);
    int value = typeAs<Int_Object>(LOCAL(0))->getValue()
                + typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_SUB($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_sub);
    int value = typeAs<Int_Object>(LOCAL(0))->getValue()
                - typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_MUL($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mul);
    int value = typeAs<Int_Object>(LOCAL(0))->getValue()
                * typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_DIV($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_div);
    int left = typeAs<Int_Object>(LOCAL(0))->getValue();
    int right = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroDiv(right)) {
        return false;
    }
    int value = left / right;
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_MOD($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mod);
    int left = typeAs<Int_Object>(LOCAL(0))->getValue();
    int right = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroMod(right)) {
        return false;
    }
    int value = left % right;
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
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
    bool r = typeAs<Int_Object>(LOCAL(0))->getValue()
             < typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GT($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_gt);
    bool r = typeAs<Int_Object>(LOCAL(0))->getValue()
             > typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_LE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_le);
    bool r = typeAs<Int_Object>(LOCAL(0))->getValue()
             <= typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ge);
    bool r = typeAs<Int_Object>(LOCAL(0))->getValue()
             >= typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_and);
    int value = typeAs<Int_Object>(LOCAL(0))->getValue()
                & typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_OR($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_or);
    int value = typeAs<Int_Object>(LOCAL(0))->getValue()
                | typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
}

//!bind: function $OP_XOR($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_xor);
    int value = typeAs<Int_Object>(LOCAL(0))->getValue()
                ^ typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getIntType(), value));
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

//!bind: function $OP_MINUS($this : Uint32) : Int64
static inline bool uint_minus(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_minus);
    long value = typeAs<Int_Object>(LOCAL(0))->getValue();
    value = -value;
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_NOT($this : Uint32) : Uint32
static inline bool uint_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_not);
    unsigned int value = ~typeAs<Int_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_add);
    unsigned int value = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
                + (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}

//!bind: function $OP_SUB($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_sub);
    unsigned int value = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
                - (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}

//!bind: function $OP_MUL($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_mul);
    unsigned int value = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
                * (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}

//!bind: function $OP_DIV($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_div);
    unsigned int left = typeAs<Int_Object>(LOCAL(0))->getValue();
    unsigned int right = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroDiv((int) right)) {
        return false;
    }
    unsigned int value = left / right;
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}

//!bind: function $OP_MOD($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_mod);
    unsigned int left = typeAs<Int_Object>(LOCAL(0))->getValue();
    unsigned int right = typeAs<Int_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroMod((int) right)) {
        return false;
    }
    unsigned int value = left % right;
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
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
    bool r = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
             < (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GT($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_gt);
    bool r = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
             > (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_LE($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_le);
    bool r = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
             <= (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GE($this : Uint32, $target : Uint32) : Boolean
static inline bool uint_2_uint_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_ge);
    bool r = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
             >= (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_and);
    unsigned int value = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
                &(unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}

//!bind: function $OP_OR($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_or);
    unsigned int value = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
                | (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
}

//!bind: function $OP_XOR($this : Uint32, $target : Uint32) : Uint32
static inline bool uint_2_uint_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint_2_uint_xor);
    unsigned int value = (unsigned int) typeAs<Int_Object>(LOCAL(0))->getValue()
                ^ (unsigned int) typeAs<Int_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Int_Object>(ctx.getPool().getUint32Type(), value));
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
    long value = -typeAs<Long_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_NOT($this : Int64) : Int64
static inline bool int64_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_not);
    long value = ~typeAs<Long_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_add);
    long value = typeAs<Long_Object>(LOCAL(0))->getValue()
                + typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_SUB($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_sub);
    long value = typeAs<Long_Object>(LOCAL(0))->getValue()
                - typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_MUL($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mul);
    long value = typeAs<Long_Object>(LOCAL(0))->getValue()
                * typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_DIV($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_div);
    long left = typeAs<Long_Object>(LOCAL(0))->getValue();
    long right = typeAs<Long_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroDiv((int) right)) {
        return false;
    }
    long value = left / right;
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_MOD($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_mod);
    long left = typeAs<Long_Object>(LOCAL(0))->getValue();
    long right = typeAs<Long_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroMod((int) right)) {
        return false;
    }
    long value = left % right;
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
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
    bool r = typeAs<Long_Object>(LOCAL(0))->getValue()
             < typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GT($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_gt);
    bool r = typeAs<Long_Object>(LOCAL(0))->getValue()
             > typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_LE($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_le);
    bool r = typeAs<Long_Object>(LOCAL(0))->getValue()
             <= typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GE($this : Int64, $target : Int64) : Boolean
static inline bool int64_2_int64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_ge);
    bool r = typeAs<Long_Object>(LOCAL(0))->getValue()
             >= typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_and);
    long value = typeAs<Long_Object>(LOCAL(0))->getValue()
                & typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_OR($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_or);
    long value = typeAs<Long_Object>(LOCAL(0))->getValue()
                | typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
}

//!bind: function $OP_XOR($this : Int64, $target : Int64) : Int64
static inline bool int64_2_int64_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int64_2_int64_xor);
    long value = typeAs<Long_Object>(LOCAL(0))->getValue()
                ^ typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getInt64Type(), value));
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
    unsigned long value = -typeAs<Long_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_NOT($this : Uint64) : Uint64
static inline bool uint64_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_not);
    unsigned long value = ~typeAs<Long_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_add);
    unsigned long value = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
                 + (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_SUB($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_sub);
    unsigned long value = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
                 - (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_MUL($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_mul);
    unsigned long value = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
                 * (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_DIV($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_div);
    unsigned long left = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue();
    unsigned long right = (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroDiv((int) right)) {
        return false;
    }
    unsigned long value = left / right;
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_MOD($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_mod);
    unsigned long left = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue();
    unsigned long right = (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    if(!ctx.checkZeroMod((int) right)) {
        return false;
    }
    unsigned long value = left % right;
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
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
    bool r = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
             < (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GT($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_gt);
    bool r = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
             > (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_LE($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_le);
    bool r = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
             <= (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GE($this : Uint64, $target : Uint64) : Boolean
static inline bool uint64_2_uint64_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_ge);
    bool r = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
             >= (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_and);
    unsigned long value = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
                 & (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_OR($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_or);
    unsigned long value = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
                 | (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
}

//!bind: function $OP_XOR($this : Uint64, $target : Uint64) : Uint64
static inline bool uint64_2_uint64_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(uint64_2_uint64_xor);
    unsigned long value = (unsigned long) typeAs<Long_Object>(LOCAL(0))->getValue()
                 ^ (unsigned long) typeAs<Long_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Long_Object>(ctx.getPool().getUint64Type(), value));
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
    double value = -typeAs<Float_Object>(LOCAL(0))->getValue();
    RET(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
static inline bool float_2_float_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_add);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue()
                   + typeAs<Float_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value));
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
static inline bool float_2_float_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_sub);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue()
                   - typeAs<Float_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value));
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
static inline bool float_2_float_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_mul);
    double value = typeAs<Float_Object>(LOCAL(0))->getValue()
                   * typeAs<Float_Object>(LOCAL(1))->getValue();
    RET(DSValue::create<Float_Object>(ctx.getPool().getFloatType(), value));
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
    bool r = typeAs<Float_Object>(LOCAL(0))->getValue()
             < typeAs<Float_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GT($this : Float, $target : Float) : Boolean
static inline bool float_2_float_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_gt);
    bool r = typeAs<Float_Object>(LOCAL(0))->getValue()
             > typeAs<Float_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_LE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_le);
    bool r = typeAs<Float_Object>(LOCAL(0))->getValue()
             <= typeAs<Float_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
}

//!bind: function $OP_GE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ge);
    bool r = typeAs<Float_Object>(LOCAL(0))->getValue()
             >= typeAs<Float_Object>(LOCAL(1))->getValue();
    RET_BOOL(r);
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
    if(ctx.toString(0) != EvalStatus::SUCCESS) {
        return false;
    }

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
    ctx.setLocal(0, DSValue::create<Array_Object>(type));
    return true;
}

//!bind: function add($this : Array<T0>, $value : T0) : Array<T0>
static inline bool array_add(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_add);
    Array_Object *obj = typeAs<Array_Object>(LOCAL(0));
    if(obj->getValues().size() == INT32_MAX) {
        ctx.throwError(ctx.getPool().getOutOfRangeErrorType(), "reach Array size limit");
        return false;
    }
    obj->append(LOCAL(1));
    RET(LOCAL(0));
}

// check index range and throw exception.
static bool checkRange(RuntimeContext &ctx, int index, int size) {
    if(index < 0 || index >= size) {
        std::string message("size is ");
        message += std::to_string(size);
        message += ", but index is ";
        message += std::to_string(index);
        ctx.throwOutOfRangeError(std::move(message));
        return false;
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
        ctx.throwError(ctx.getPool().getOutOfRangeErrorType(),
                       "array iterator has already reached end");
        return false;
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
    RET(LOCAL(0)->commandArg(ctx));
}


// #################
// ##     Map     ##
// #################

//!bind: constructor ($this : Map<T0, T1>)
static inline bool map_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, DSValue::create<Map_Object>(type));
    return true;
}

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
static inline bool map_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_get);
    Map_Object *obj = typeAs<Map_Object>(LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    if(iter == obj->getValueMap().end()) {
        std::string msg("not found key: ");
        msg += LOCAL(1)->toString(ctx);
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
        ctx.throwError(ctx.getPool().getOutOfRangeErrorType(),
                       "map iterator has already reached end");
        return false;
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
    ctx.setLocal(0, DSValue::create<Tuple_Object>(type));
    typeAs<Tuple_Object>(LOCAL(0))->set(0, LOCAL(1));
    return true;
}

//!bind: function $OP_CMD_ARG($this : Tuple<>) : Array<String>
static inline bool tuple_cmdArg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(tuple_cmdArg);
    RET(LOCAL(0)->commandArg(ctx));
}


// ###################
// ##     Error     ##
// ###################

//!bind: constructor ($this : Error, $message : String)
static inline bool error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, DSValue(Error_Object::newError(ctx, type, LOCAL(1))));
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



#endif /* CORE_BUILTIN_CPP_ */
