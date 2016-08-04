#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --disable-assertion -c 'assert(12 / 0 == 12)'

v="$(cat << EOF
assert
    (false)    :
        "hello assertion"
EOF
)"

m="$(cat << EOF
Assertion Error: hello assertion
    from (string):2 '<toplevel>()'
EOF
)"

test "$($YDSH_BIN -c "$v" 2>&1 || true)" = "$m"

exit 0