#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

# line marker of syntax error
v="$(cat << EOF
(stdin):1: [syntax error] mismatched token: <EOS>, expected: =
var a
      ^
EOF
)"

test "$(echo -e 'var a   \n    \\\n   \t  \t  \n   ' | $YDSH_BIN 2>&1)" = "$v"


# line marker of semantic error
v="$(cat << EOF
(string):1: [semantic error] require Int32, but is String
[34, "hey"]
     ^~~~~
EOF
)"

test "$($YDSH_BIN -c '[34, "hey"]' 2>&1)" = "$v"

exit 0