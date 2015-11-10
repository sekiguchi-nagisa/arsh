#!/usr/bin/env bash

YDSH_BIN=$1

$YDSH_BIN -c 'if($DBus.available()) { true; } else { false; }'

if [ $? == 0 ]; then
    $YDSH_BIN --feature | grep 'USE_DBUS'
    exit $?
else
    $YDSH_BIN --feature | grep 'USE_DBUS'
    test $? != 0
    exit $?
fi