#!/usr/bin/env bash


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


$YDSH_BIN --print-toplevel -c '$true' | grep '(Boolean) true'

test "$($YDSH_BIN --print-toplevel -c 'var a = new Tuple<Any>(9); $a._0 = $a; $a' 2>&1)" \
       = 'cannot obtain string representation'

exit 0