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
  number of local variable: 1
  number of global variable: 38
Code:
   9: LOAD_CONST  0
  11: STORE_GLOBAL  37
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
  code size: 38
  number of local variable: 1
  number of global variable: 38
Code:
   9: LOAD_CONST  0
  11: STORE_GLOBAL  37
  14: LOAD_GLOBAL  37
  17: LOAD_CONST  1
  19: CALL_FUNC  1
  22: POP
  23: ENTER_FINALLY  33
  28: GOTO  37
  33: LOAD_CONST  2
  35: POP
  36: EXIT_FINALLY
  37: STOP_EVAL
Constant Pool:
  0: (null) function(f)
  1: Int32 1
  2: Int32 3
Source Pos Entry:
  lineNum: 1, address: 19, pos: 67
Exception Table:
  begin: 14, end: 33, type: Any, dest: 33

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