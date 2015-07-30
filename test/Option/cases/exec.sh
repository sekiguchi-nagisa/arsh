#!/usr/bin/env bash

YDSH_BIN=$1

test "$($YDSH_BIN -e echo hello)" = "hello"

if [ $? != 0 ]; then
    exit 1
fi

# not found builtin command
$YDSH_BIN -e fhurehfurei

if [ $? != 1 ]; then
    exit 1
fi

# exit
$YDSH_BIN -e exit 34

if [ $? != 34 ]; then
    exit 1
fi

exit 0