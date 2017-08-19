#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --compile-only -c 'exit 88'

test "$($YDSH_BIN --compile-only -c 'exit 88' 2>&1)" == ""

test "$($YDSH_BIN --compile-only --dump-untyped-ast -c 'exit 88')" != ""
test "$($YDSH_BIN --compile-only --dump-ast -c 'exit 88')" != ""
test "$($YDSH_BIN --compile-only --dump-code -c 'exit 88')" != ""

exit 0