#!/usr/bin/env bash


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


test "$($YDSH_BIN --print-toplevel -c '23 as String')" = '(String) 23'

test "$($YDSH_BIN --print-toplevel -c '$true')" = '(Boolean) true'

test "$($YDSH_BIN --print-toplevel -c 'true')" = ""

test "$($YDSH_BIN --print-toplevel -c 'true | true')" = ""

$YDSH_BIN --print-toplevel -c "$'hello\x00world'" | grep $'hello\x00world'

v="$(cat << EOF
[runtime error]
StackOverflowError: caused by circular reference
    from (string):1 '<toplevel>()'
EOF
)"

test "$($YDSH_BIN --print-toplevel -c 'var a = (9 as Any,); $a._0 = $a; $a' 2>&1 || true)" = "$v"

v="$(cat << EOF
[runtime error]
cannot obtain string representation
EOF
)"

test "$($YDSH_BIN --print-toplevel -c 'var a = (9 as Any,); $a._0 = $a; throw $a' 2>&1 || true)" = "$v"

test "$($YDSH_BIN --print-toplevel -c 'var a = $true as Option<Boolean>; $a')" = '(Option<Boolean>) true'

test "$($YDSH_BIN --print-toplevel -c 'new Option<Boolean>()' 2>&1 || true)" = '(Option<Boolean>) (invalid)'

test "$($YDSH_BIN --print-toplevel -c 'var a = $true as String as Option<String>; $a')" = '(Option<String>) true'

test "$($YDSH_BIN --print-toplevel -c 'new Option<String>()' 2>&1 || true)" = '(Option<String>) (invalid)'

exit 0