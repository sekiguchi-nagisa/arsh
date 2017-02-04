#!/usr/bin/env bash


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


test "$($YDSH_BIN --print-toplevel -c '$true')" = '(Boolean) true'

$YDSH_BIN --print-toplevel -c "$'hello\x00world'" | grep $'hello\x00world'

v="$(cat << EOF
[runtime error]
StackOverflowError: caused by circular reference
    from (string):1 '<toplevel>()'
EOF
)"

test "$($YDSH_BIN --print-toplevel -c 'var a = new Tuple<Any>(9); $a._0 = $a; $a' 2>&1 || true)" = "$v"

v="$(cat << EOF
[runtime error]
cannot obtain string representation
EOF
)"

test "$($YDSH_BIN --print-toplevel -c 'var a = new Tuple<Any>(9); $a._0 = $a; throw $a' 2>&1 || true)" = "$v"

test "$($YDSH_BIN --print-toplevel -c 'var a = $true as Option<Boolean>; $a')" = '(Option<Boolean>) true'

v="$(cat << EOF
[runtime error]
UnwrappingError: invalid value
    from (string):1 '<toplevel>()'
EOF
)"

test "$($YDSH_BIN --print-toplevel -c 'new Option<Boolean>()' 2>&1 || true)" = "$v"

exit 0