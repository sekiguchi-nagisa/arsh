#!/usr/bin/env bash

YDSH_BIN=$1

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR


V="$(cat << EOS
### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 23
  number of local variable: 0
  number of global variable: 43
Code:
   9: LOAD_CONST  0
  11: STORE_GLOBAL  42
  14: LOAD_CONST  1
  16: CALL_METHOD  0  0
  21: POP
  22: STOP_EVAL
Constant Pool:
  0: Int32 34
  1: Int32 34
Source Pos Entry:
  lineNum: 1, address: 16, pos: 12
Exception Table:
EOS
)"

test "$($YDSH_BIN --dump-code -c 'var a = 34; 34 as String')" = "$V"


V="$(cat << EOS
### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 36
  number of local variable: 0
  number of global variable: 43
Code:
   9: LOAD_CONST  0
  11: STORE_GLOBAL  42
  14: LOAD_GLOBAL  42
  17: LOAD_CONST  1
  19: CALL_FUNC  1
  22: POP
  23: ENTER_FINALLY  8
  26: GOTO  35
  31: LOAD_CONST  2
  33: POP
  34: EXIT_FINALLY
  35: STOP_EVAL
Constant Pool:
  0: (null) function(f)
  1: Int32 1
  2: Int32 3
Source Pos Entry:
  lineNum: 1, address: 19, pos: 67
Exception Table:
  begin: 14, end: 31, type: Any, dest: 31, offset: 0, size: 0

DSCode: function f
  code size: 20
  number of local variable: 1
Code:
   7: LOAD_LOCAL  0
  10: INSTANCE_OF  Array<Int32>
  19: RETURN_V
Constant Pool:
Source Pos Entry:
Exception Table:
EOS
)"

test "$($YDSH_BIN --dump-code -c 'function f($a : Any) : Boolean { return $a is Array<Int>; }; try { $f(1) } finally {3}')" == "$V"