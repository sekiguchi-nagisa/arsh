#!/usr/bin/env bash


trap "echo trap error; exit 1" ERR

YDSH_BIN=$1

$YDSH_BIN --trace-exit -c 'exit 23' 2>&1 | grep 'Shell Exit: terminated by exit 23'

exit 0