#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

echo 'assert($0 == "ydsh")' | $YDSH_BIN

echo -n '\' | $YDSH_BIN

# with arguments
echo 'assert($0 == "ydsh" && $1 == "hoge" && $2 == "123")' | $YDSH_BIN -s hoge 123

# force interactive
test "$(echo '$true' | $YDSH_BIN -i --quiet --norc)" = "(Boolean) true"


exit 0
