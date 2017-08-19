#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --check-only -c 'exit 88'
test "$($YDSH_BIN --check-only -c 'exit 88' 2>&1)" = ""

# when specified '--check-only' option, only work '--dump-untyped-ast' and '--dump-ast'
test "$($YDSH_BIN --check-only --dump-untyped-ast -c 'exit 88')" != ""
test "$($YDSH_BIN --check-only --dump-ast -c 'exit 88')" != ""
test "$($YDSH_BIN --check-only --dump-code -c 'exit 88' 2>&1)" = ""


# equivalent to '--check-only' option
$YDSH_BIN -n -c 'exit 88'
test "$($YDSH_BIN -n -c 'exit 88' 2>&1)" = ""

test "$($YDSH_BIN -n --dump-untyped-ast -c 'exit 88')" != ""
test "$($YDSH_BIN -n --dump-ast -c 'exit 88')" != ""
test "$($YDSH_BIN -n --dump-code -c 'exit 88' 2>&1)" = ""

exit 0