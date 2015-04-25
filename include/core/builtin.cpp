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

#include <core/RuntimeContext.h>
#include <core/DSObject.h>

#include <math.h>

// helper macro
#define LOCAL(index) (ctx.localStack[ctx.localVarOffset + (index)])
#define RET(value) do { ctx.returnObject = (value); return true; } while(0)
#define TO_BOOL(value) ((value) ? ctx.trueObj : ctx.falseObj)

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

//!bind: function $OP_CMD_ARG($this : Any) : Any
static inline bool to_cmd_arg(RuntimeContext &ctx) {
    SUPPRESS_WARNING(to_cmd_arg);
    RET(LOCAL(0)->commandArg(ctx));
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
    int value = -TYPE_AS(Int_Object, LOCAL(0))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_NOT($this : Int32) : Int32
static inline bool int_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_not);
    int value = ~TYPE_AS(Int_Object, LOCAL(0))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}


// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_add);
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
                + TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_SUB($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_sub);
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
                - TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_MUL($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mul);
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
                * TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_DIV($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_div);
    int left = TYPE_AS(Int_Object, LOCAL(0))->value;
    int right = TYPE_AS(Int_Object, LOCAL(1))->value;
    if(!ctx.checkZeroDiv(right)) {
        return false;
    }
    int value = left / right;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_MOD($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_mod);
    int left = TYPE_AS(Int_Object, LOCAL(0))->value;
    int right = TYPE_AS(Int_Object, LOCAL(1))->value;
    if(!ctx.checkZeroMod(right)) {
        return false;
    }
    int value = left % right;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_eq);
    RET(TO_BOOL(LOCAL(0)->equals(LOCAL(1))));
}

//!bind: function $OP_NE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ne);
    RET(TO_BOOL(!LOCAL(0)->equals(LOCAL(1))));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_lt);
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
             < TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GT($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_gt);
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
             > TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_LE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_le);
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
             <= TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GE($this : Int32, $target : Int32) : Boolean
static inline bool int_2_int_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_ge);
    bool r = TYPE_AS(Int_Object, LOCAL(0))->value
             >= TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//   =====  logical  =====

//!bind: function $OP_AND($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_and(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_and);
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
                & TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_OR($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_or(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_or);
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
                | TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function $OP_XOR($this : Int32, $target : Int32) : Int32
static inline bool int_2_int_xor(RuntimeContext & ctx) {
    SUPPRESS_WARNING(int_2_int_xor);
    int value = TYPE_AS(Int_Object, LOCAL(0))->value
                ^TYPE_AS(Int_Object, LOCAL(1))->value;
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
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
    double value = -TYPE_AS(Float_Object, LOCAL(0))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

// =====  binary op  =====

//   =====  arithmetic  =====

//!bind: function $OP_ADD($this : Float, $target : Float) : Float
static inline bool float_2_float_add(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_add);
    double value = TYPE_AS(Float_Object, LOCAL(0))->value
                   + TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_SUB($this : Float, $target : Float) : Float
static inline bool float_2_float_sub(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_sub);
    double value = TYPE_AS(Float_Object, LOCAL(0))->value
                   - TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_MUL($this : Float, $target : Float) : Float
static inline bool float_2_float_mul(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_mul);
    double value = TYPE_AS(Float_Object, LOCAL(0))->value
                   * TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_DIV($this : Float, $target : Float) : Float
static inline bool float_2_float_div(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_div);
    double left = TYPE_AS(Float_Object, LOCAL(0))->value;
    double right = TYPE_AS(Float_Object, LOCAL(1))->value;
    if(!ctx.checkZeroDiv(right)) {
        return false;
    }
    double value = left / right;
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//!bind: function $OP_MOD($this : Float, $target : Float) : Float
static inline bool float_2_float_mod(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_mod);
    double left = TYPE_AS(Float_Object, LOCAL(0))->value;
    double right = TYPE_AS(Float_Object, LOCAL(1))->value;
    if(!ctx.checkZeroMod(right)) {
        return false;
    }
    double value = fmod(left, right);
    RET(std::make_shared<Float_Object>(ctx.pool.getFloatType(), value));
}

//   =====  equality  =====

//!bind: function $OP_EQ($this : Float, $target : Float) : Boolean
static inline bool float_2_float_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_eq);
    RET(TO_BOOL(LOCAL(0)->equals(LOCAL(1))));
}

//!bind: function $OP_NE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ne);
    RET(TO_BOOL(!LOCAL(0)->equals(LOCAL(1))));
}

//   =====  relational  =====

//!bind: function $OP_LT($this : Float, $target : Float) : Boolean
static inline bool float_2_float_lt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_lt);
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
             < TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GT($this : Float, $target : Float) : Boolean
static inline bool float_2_float_gt(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_gt);
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
             > TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_LE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_le(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_le);
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
             <= TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}

//!bind: function $OP_GE($this : Float, $target : Float) : Boolean
static inline bool float_2_float_ge(RuntimeContext & ctx) {
    SUPPRESS_WARNING(float_2_float_ge);
    bool r = TYPE_AS(Float_Object, LOCAL(0))->value
             >= TYPE_AS(Float_Object, LOCAL(1))->value;
    RET(TO_BOOL(r));
}


// #####################
// ##     Boolean     ##
// #####################

//!bind: function $OP_NOT($this : Boolean) : Boolean
static inline bool boolean_not(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_not);
    bool value = TYPE_AS(Boolean_Object, LOCAL(0))->value;
    RET(TO_BOOL(!value));
}

//!bind: function $OP_EQ($this : Boolean, $target : Boolean) : Boolean
static inline bool boolean_eq(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_eq);
    bool r = LOCAL(0)->equals(LOCAL(1));
    RET(TO_BOOL(r));
}

//!bind: function $OP_NE($this : Boolean, $target : Boolean) : Boolean
static inline bool boolean_ne(RuntimeContext & ctx) {
    SUPPRESS_WARNING(boolean_ne);
    bool r = !LOCAL(0)->equals(LOCAL(1));
    RET(TO_BOOL(r));
}


// ####################
// ##     String     ##
// ####################

//!bind: function $OP_ADD($this : String, $target : Any) : String
static inline bool string_add(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_add);
    ctx.getLocal(1);
    ctx.toString();
    std::shared_ptr<String_Object> str(new String_Object(ctx.pool.getStringType()));
    str->value += TYPE_AS(String_Object, LOCAL(0))->value;
    str->value += TYPE_AS(String_Object, ctx.peek())->value;
    RET(str);
}

//!bind: function $OP_EQ($this : String, $target : String) : Boolean
static inline bool string_eq(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_eq);
    bool  r = LOCAL(0)->equals(LOCAL(1));
    RET(TO_BOOL(r));
}

//!bind: function $OP_NE($this : String, $target : String) : Boolean
static inline bool string_ne(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_ne);
    bool  r = !LOCAL(0)->equals(LOCAL(1));
    RET(TO_BOOL(r));
}

//!bind: function size($this : String) : Int32
static inline bool string_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_size);
    int size = TYPE_AS(String_Object, LOCAL(0))->value.size();
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), size));
}

//!bind: function empty($this : String) : Boolean
static inline bool string_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(string_empty);
    bool empty = TYPE_AS(String_Object, LOCAL(0))->value.empty();
    RET(TO_BOOL(empty));
}


// ###################
// ##     Array     ##
// ###################

//!bind: constructor ($this : Array<T0>)
static inline bool array_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_init);
    DSType *type = LOCAL(0)->getType();
    ctx.setLocal(0, std::make_shared<Array_Object>(type));
    return true;
}

//!bind: function add($this : Array<T0>, $value : T0) : Array<T0>
static inline bool array_add(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_add);
    TYPE_AS(Array_Object, LOCAL(0))->values.push_back(LOCAL(1));
    RET(LOCAL(0));
}

// check index range and throw exception.
static bool checkRange(RuntimeContext &ctx, int index, unsigned int size) {
    unsigned int actualIndex = (unsigned int) index;
    if(index < 0 || actualIndex >= size) {
        std::string message("size is ");
        message += std::to_string(size);
        message += ", but index is ";
        message += std::to_string(index);
        ctx.throwOutOfIndexError(std::move(message));
        return false;
    }
    return true;
}

//!bind: function $OP_GET($this : Array<T0>, $index : Int32) : T0
static inline bool array_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_get);

    Array_Object *obj = TYPE_AS(Array_Object, LOCAL(0));
    unsigned int size = obj->values.size();
    int index = TYPE_AS(Int_Object, LOCAL(1))->value;
    if(!checkRange(ctx, index, size)) {
        return false;
    }
    RET(obj->values[(unsigned int)index]);
}

//!bind: function $OP_SET($this : Array<T0>, $index : Int32, $value : T0) : Void
static inline bool array_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_set);

    Array_Object *obj = TYPE_AS(Array_Object, LOCAL(0));
    unsigned int size = obj->values.size();
    int index = TYPE_AS(Int_Object, LOCAL(1))->value;
    if(!checkRange(ctx, index, size)) {
        return false;
    }
    obj->values[(unsigned int) index] = LOCAL(2);
    return true;
}

//!bind: function size($this : Array<T0>) : Int32
static inline bool array_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_size);
    int size = TYPE_AS(Array_Object, LOCAL(0))->values.size();
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), size));
}

//!bind: function empty($this : Array<T0>) : Boolean
static inline bool array_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_empty);
    bool empty = TYPE_AS(Array_Object, LOCAL(0))->values.empty();
    RET(TO_BOOL(empty));
}

//!bind: function $OP_ITER($this : Array<T0>) : Array<T0>
static inline bool array_iter(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_iter);
    TYPE_AS(Array_Object, LOCAL(0))->curIndex = 0;
    RET(LOCAL(0));
}

//!bind: function $OP_NEXT($this : Array<T0>) : T0
static inline bool array_next(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_next);
    Array_Object *obj = TYPE_AS(Array_Object, LOCAL(0));
    unsigned int size = obj->values.size();
    int index = obj->curIndex++;
    if(!checkRange(ctx, index, size)) {
        return false;
    }
    RET(obj->values[(unsigned int)index]);
}

//!bind: function $OP_HAS_NEXT($this : Array<T0>) : Boolean
static inline bool array_hasNext(RuntimeContext &ctx) {
    SUPPRESS_WARNING(array_hasNext);
    Array_Object *obj = TYPE_AS(Array_Object, LOCAL(0));
    bool r = obj->curIndex < obj->values.size();
    RET(TO_BOOL(r));
}


// #################
// ##     Map     ##
// #################

//!bind: constructor ($this : Map<T0, T1>)
static inline bool map_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_init);
    DSType *type = LOCAL(0)->type;
    ctx.setLocal(0, std::make_shared<Map_Object>(type));
    return  true;
}

//!bind: function $OP_GET($this : Map<T0, T1>, $key : T0) : T1
static inline bool map_get(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_get);
    Map_Object *obj = TYPE_AS(Map_Object, LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    if(iter == obj->getValueMap().end()) {
        std::string msg("not found key: ");
        msg += LOCAL(1)->toString(ctx);
        ctx.throwError(ctx.pool.getKeyNotFoundErrorType(), std::move(msg));
        return false;
    }
    RET(iter->second);
}

//!bind: function $OP_SET($this : Map<T0, T1>, $key : T0, $value : T1) : Void
static inline bool map_set(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_set);
    Map_Object *obj = TYPE_AS(Map_Object, LOCAL(0));
    obj->set(LOCAL(1), LOCAL(2));
    return true;
}

//!bind: function size($this : Map<T0, T1>) : Int32
static inline bool map_size(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_size);
    Map_Object *obj = TYPE_AS(Map_Object, LOCAL(0));
    int value = obj->getValueMap().size();
    RET(std::make_shared<Int_Object>(ctx.pool.getIntType(), value));
}

//!bind: function empty($this : Map<T0, T1>) : Boolean
static inline bool map_empty(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_empty);
    Map_Object *obj = TYPE_AS(Map_Object, LOCAL(0));
    bool value = obj->getValueMap().empty();
    RET(TO_BOOL(value));
}

//!bind: function find($this : Map<T0, T1>, $key : T0) : Boolean
static inline bool map_find(RuntimeContext &ctx) {
    SUPPRESS_WARNING(map_find);
    Map_Object *obj = TYPE_AS(Map_Object, LOCAL(0));
    auto iter = obj->getValueMap().find(LOCAL(1));
    RET(TO_BOOL(iter != obj->getValueMap().end()));
}

// ###################
// ##     Tuple     ##
// ###################

//!bind: constructor ($this : Tuple<T0>, $arg : T0)
static inline bool tuple_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(tuple_init);
    DSType *type = LOCAL(0)->type;
    ctx.setLocal(0, std::make_shared<Tuple_Object>(type));
    TYPE_AS(Tuple_Object, LOCAL(0))->set(0, LOCAL(1));
    return true;
}


// ###################
// ##     Error     ##
// ###################

//!bind: constructor ($this : Error, $message : String)
static inline bool error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_init);
    DSType *type = LOCAL(0)->type;
    ctx.setLocal(0, std::shared_ptr<DSObject>(Error_Object::newError(ctx, type, LOCAL(1))));
    return true;
}

//!bind: function message($this : Error) : String
static inline bool error_message(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_message);
    RET(TYPE_AS(Error_Object, LOCAL(0))->message);
}

//!bind: function backtrace($this : Error) : Void
static inline bool error_backtrace(RuntimeContext &ctx) {
    SUPPRESS_WARNING(error_backtrace);
    TYPE_AS(Error_Object, LOCAL(0))->printStackTrace(ctx);
    return true;
}

//!bind: constructor ($this : ArithmeticError, $message : String)
static inline bool arith_error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(arith_error_init);
    return error_init(ctx);
}

//!bind: constructor ($this : OutOfIndexError, $message : String)
static inline bool out_error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(out_error_init);
    return error_init(ctx);
}

//!bind: constructor ($this : KeyNotFoundError, $message : String)
static inline bool key_error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(key_error_init);
    return error_init(ctx);
}

//!bind: constructor ($this : TypeCastError, $message : String)
static inline bool cast_error_init(RuntimeContext &ctx) {
    SUPPRESS_WARNING(cast_error_init);
    return error_init(ctx);
}

// ##################
// ##     DBus     ##
// ##################

//!bind: function systemBus($this : DBus) : Bus
static inline bool dbus_systemBus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_systemBus);
    RET(TYPE_AS(DBus_Object, LOCAL(0))->getSystemBus());
}

//!bind: function sessionBus($this : DBus) : Bus
static inline bool dbus_sessionBus(RuntimeContext &ctx) {
    SUPPRESS_WARNING(dbus_sessionBus);
    RET(TYPE_AS(DBus_Object, LOCAL(0))->getSessionBus());
}

// #################
// ##     Bus     ##
// #################

//!bind: function connection($this : Bus, $dest : String) : Connection
static inline bool bus_connection(RuntimeContext &ctx) {
    SUPPRESS_WARNING(bus_connection);
    RET(std::make_shared<Connection_Object>(ctx.pool.getConnectionType(), LOCAL(1)));
}


} //namespace core
} //namespace ydsh



#endif /* CORE_BUILTIN_CPP_ */
