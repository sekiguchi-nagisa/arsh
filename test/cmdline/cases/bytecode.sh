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
  code size: 22
  number of local variable: 0
  number of global variable: 43
Code:
   8: LOAD_CONST  0
  10: STORE_GLOBAL  42
  13: LOAD_CONST  1
  15: CALL_METHOD  0  0
  20: POP
  21: STOP_EVAL
Constant Pool:
  0: Int32 34
  1: Int32 34
Source Pos Entry:
  lineNum: 1, address: 15, pos: 12
Exception Table:
EOS
)"

test "$($YDSH_BIN --dump-code -c 'var a = 34; 34 as String')" = "$V"


V="$(cat << EOS
### dump compiled code ###
Source File: (string)
DSCode: top level
  code size: 35
  number of local variable: 0
  number of global variable: 43
Code:
   8: LOAD_CONST  0
  10: STORE_GLOBAL  42
  13: LOAD_GLOBAL  42
  16: LOAD_CONST  1
  18: CALL_FUNC  1
  21: POP
  22: ENTER_FINALLY  8
  25: GOTO  34
  30: LOAD_CONST  2
  32: POP
  33: EXIT_FINALLY
  34: STOP_EVAL
Constant Pool:
  0: (null) function(f)
  1: Int32 1
  2: Int32 3
Source Pos Entry:
  lineNum: 1, address: 18, pos: 67
Exception Table:
  begin: 13, end: 30, type: Any, dest: 30, offset: 0, size: 0

DSCode: function f
  code size: 18
  number of local variable: 1
Code:
   6: LOAD_LOCAL  0
   8: INSTANCE_OF  Array<Int32>
  17: RETURN_V
Constant Pool:
Source Pos Entry:
Exception Table:
EOS
)"

test "$($YDSH_BIN --dump-code -c 'function f($a : Any) : Boolean { return $a is Array<Int>; }; try { $f(1) } finally {3}')" == "$V"