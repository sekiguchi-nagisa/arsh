#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --parse-only -c 'var a = 34; $a = $a;'

test "$($YDSH_BIN --parse-only -c 'var a = 34; $a = $a;' 2>&1)" = ""

# when specified '--parse-only' option, only work '--dump-untyped-ast'
test "$($YDSH_BIN --parse-only --dump-untyped-ast -c 'var a = 34; $a = $a;')" != ""
test "$($YDSH_BIN --parse-only --dump-ast -c 'var a = 34; $a = $a;' 2>&1)" = ""
test "$($YDSH_BIN --parse-only --dump-code -c 'var a = 34; $a = $a;' 2>&1)" = ""

exit 0