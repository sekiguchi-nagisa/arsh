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

#ifndef BUILTIN_CPP_
#define BUILTIN_CPP_

#include <core/RuntimeContext.h>
#include <core/DSObject.h>

#include <math.h>

// helper macro
#define LOCAL(index) (ctx.localStack[ctx.localVarOffset + (index)])
#define RET(value) do { ctx.returnObject = (value); return true; } while(0)
#define TO_BOOL(value) ((value) ? ctx.trueObj : ctx.falseObj)

/**
 *   //!bind: function <method name>($this : <receiver type>, $param1 : <type1>, $param2? : <type2>, ...) : <return type>
 *   //!bind: constructor <type name>($param1 : <type1>, ....)
 *   $<param name>?  has default value
 */


// #################
// ##     Any     ##
// #################

//!bind: function $OP_TO_STR($this : Any) : String
static bool to_str(RuntimeContext &ctx) {
    RET(LOCAL(0)->str(ctx));
}

//!bind: function $OP_TO_INTERP($this : Any) : String
static bool to_interp(RuntimeContext &ctx) {
    RET(LOCAL(0)->interp(ctx));
}

//TODO: add to_cmd_arg
////!bind: function $OP_TO_CMD_ARG($this : Any) : CommanddArg
//static bool to_cmd_arg(RuntimeContext &ctx);


// #################
// ##     Int     ##
// #################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Int) : Int
static bool int_plus(RuntimeContext &ctx) {
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Int) : Int
static bool int_minus(RuntimeContext &ctx) {
    int value = - TYPE_AS(Int_Object, LOCAL(0))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_NOT($this : Int) : Int
static bool int_not(RuntimeContext &ctx) {
    int value = ~ TYPE_AS(Int_Object, LOCAL(0))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int, $target : Int) : Int
static bool int_2_int_add(RuntimeContext &ctx) {
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
              + TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_SUB($this : Int, $target : Int) : Int
static bool int_2_int_sub(RuntimeContext &ctx) {
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
              - TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_MUL($this : Int, $target : Int) : Int
static bool int_2_int_mul(RuntimeContext &ctx) {
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
              * TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_DIV($this : Int, $target : Int) : Int
static bool int_2_int_div(RuntimeContext &ctx) {
    int left = TYPE_AS(Int_Object, LOCAL(0))->value;
    int right = TYPE_AS(Int_Object, LOCAL(1))->value;
    if(!ctx.checkZeroDiv(right)) {
        return false;
    }
    int value = left / right;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_MOD($this : Int, $target : Int) : Int
static bool int_2_int_mod(RuntimeContext &ctx) {
    int left = TYPE_AS(Int_Object, LOCAL(0))->value;
    int right = TYPE_AS(Int_Object, LOCAL(1))->value;
    if(!ctx.checkZeroMod(right)) {
        return false;
    }
    int value = left % right;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int, $target : Int) : Int
static bool int_2_int_eq(RuntimeContext &ctx) {
    RET(TO_BOOL(LOCAL(0)->equals(LOCAL(1))));
}

//!bind: function $OP_NE($this : Int, $target : Int) : Int
static bool int_2_int_ne(RuntimeContext &ctx) {
    RET(TO_BOOL(!LOCAL(0)->equals(LOCAL(1))));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int, $target : Int) : Int
static bool int_2_int_lt(RuntimeContext &ctx) {
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
           < TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GT($this : Int, $target : Int) : Int
static bool int_2_int_gt(RuntimeContext &ctx) {
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
           > TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_LE($this : Int, $target : Int) : Int
static bool int_2_int_le(RuntimeContext &ctx) {
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
           <= TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GE($this : Int, $target : Int) : Int
static bool int_2_int_ge(RuntimeContext &ctx) {
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
           >= TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int, $target : Int) : Int
static bool int_2_int_and(RuntimeContext &ctx) {
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
              & TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_OR($this : Int, $target : Int) : Int
static bool int_2_int_or(RuntimeContext &ctx) {
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
              | TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_XOR($this : Int, $target : Int) : Int
static bool int_2_int_xor(RuntimeContext &ctx) {
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
              ^ TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}


// ###################
// ##     Float     ##
// ###################

// =====  unary op  =====

//!bind: function $OP_PLUS($this : Float) : Float
static bool float_plus(RuntimeContext &ctx) {
    RET(LOCAL(0));
}

//!bind: function $OP_MINUS($this : Float) : Float
static bool float_minus(RuntimeContext &ctx) {
    double value = - TYPE_AS(Float_Object, LOCAL(0))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
static bool float_2_float_add(RuntimeContext &ctx) {
    double value = TYPE_AS(Float_Object, LOCAL(0))->value
                 + TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
static bool float_2_float_sub(RuntimeContext &ctx) {
    double value = TYPE_AS(Float_Object, LOCAL(0))->value
                 - TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
static bool float_2_float_mul(RuntimeContext &ctx) {
    double value = TYPE_AS(Float_Object, LOCAL(0))->value
                 * TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_DIV($this : Float, $target : Float) : Float
static bool float_2_float_div(RuntimeContext &ctx) {
    double left = TYPE_AS(Float_Object, LOCAL(0))->value;
    double right = TYPE_AS(Float_Object, LOCAL(1))->value;
    if(!ctx.checkZeroDiv(right)) {
        return false;
    }
    double value = left / right;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_MOD($this : Float, $target : Float) : Float
static bool float_2_float_mod(RuntimeContext &ctx) {
    double left = TYPE_AS(Float_Object, LOCAL(0))->value;
    double right = TYPE_AS(Float_Object, LOCAL(1))->value;
    if(!ctx.checkZeroMod(right)) {
        return false;
    }
    double value = fmod(left, right);
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Float
static bool float_2_float_eq(RuntimeContext &ctx) {
    RET(TO_BOOL(LOCAL(0)->equals(LOCAL(1))));
}

//!bind: function $OP_NE($this : Float, $target : Float) : Float
static bool float_2_float_ne(RuntimeContext &ctx) {
    RET(TO_BOOL(!LOCAL(0)->equals(LOCAL(1))));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Float
static bool float_2_float_lt(RuntimeContext &ctx) {
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
           < TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GT($this : Float, $target : Float) : Float
static bool float_2_float_gt(RuntimeContext &ctx) {
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
           > TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_LE($this : Float, $target : Float) : Float
static bool float_2_float_le(RuntimeContext &ctx) {
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
           <= TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GE($this : Float, $target : Float) : Float
static bool float_2_float_ge(RuntimeContext &ctx) {
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
           >= TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}


// #####################
// ##     Boolean     ##
// #####################

//!bind: function $OP_NOT($this : Boolean) : Boolean
static bool boolean_not(RuntimeContext &ctx) {
    bool value = TYPE_AS(Boolean_Object, LOCAL(0))->value;
    RET(TO_BOOL(!value));
}

//!bind: function $OP_EQ($this : Boolean, $target : Boolean) : Boolean
static bool boolean_eq(RuntimeContext &ctx) {
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET(TO_BOOL(r));
}

//!bind: function $OP_NE($this : Boolean, $target : Boolean) : Boolean
static bool boolean_ne(RuntimeContext &ctx) {
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET(TO_BOOL(r));
}


#endif /* BUILTIN_CPP_ */
