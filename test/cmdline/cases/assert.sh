#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

# disable assert
$YDSH_BIN --disable-assertion -c 'assert(12 / 0 == 12)'

# assert with message
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

# assert without message
m="$(cat << 'EOF'
Assertion Error: `34 == 43'
    from (string):1 '<toplevel>()'
EOF
)"

test "$($YDSH_BIN -c 'assert 34 == 43' 2>&1 || true)" = "$m"


exit 0