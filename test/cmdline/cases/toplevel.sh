#!/usr/bin/env bash


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


$YDSH_BIN --print-toplevel -c '$true' | grep '(Boolean) true'
$YDSH_BIN -c '$true print' | grep '(Boolean) true'

$YDSH_BIN --print-toplevel -c 'var a = new Tuple<Any>(9); $a._0 = $a; $a' 2>&1 | grep '[runtime errpr]'

exit 0