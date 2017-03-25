#!/usr/bin/env bash

YDSH_BIN=$1

# syntax error
$YDSH_BIN -c '23 / /'

if [ $? != 1 ]; then
    exit 1
fi

# semantic error
$YDSH_BIN -c 'var a = #WWW'

if [ $? != 1 ]; then
    exit 1
fi

# assertion
$YDSH_BIN -c 'assert(12 == 4)'

if [ $? != 1 ]; then
    exit 1
fi

# exception
$YDSH_BIN -c '34 / 0'

if [ $? != 1 ]; then
    exit 1
fi

# normal
$YDSH_BIN -c 'lajfeoifreo'

if [ $? != 1 ]; then
    exit 1
fi

$YDSH_BIN -c '__puts -3'

if [ $? != 1 ]; then
    exit 1
fi

$YDSH_BIN -c 'echo hello'

if [ $? != 0 ]; then
    exit 1
fi

# exit
$YDSH_BIN -c 'exit 0'

if [ $? != 0 ]; then
    exit 1
fi

$YDSH_BIN -c 'exit 66'

if [ $? != 66 ]; then
    exit 1
fi

# eval
test "$($YDSH_BIN -c 'exec > /dev/null; echo hello')" = ""

if [ $? != 0 ]; then
    exit 1
fi

exit 0