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

# builtin-exec
$YDSH_BIN -e exec # do nothing
if [ $? != 0 ]; then
    exit 1
fi

$YDSH_BIN -e exec echo hello
if [ $? != 0 ]; then
    exit 1
fi

$YDSH_BIN -e exec -a hoge echo hello
if [ $? != 0 ]; then
    exit 1
fi

$YDSH_BIN -e exec -c $YDSH_BIN -c 'assert(printenv SHLVL | grep 1)'
if [ $? != 0 ]; then
    exit 1
fi

$YDSH_BIN -e exec -c $YDSH_BIN -c 'assert(printenv PATH | grep /bin:/usr/bin:/usr/local/bin)'
if [ $? != 0 ]; then
    exit 1
fi

$YDSH_BIN -e exec -u    # invalid option
test $? != 0