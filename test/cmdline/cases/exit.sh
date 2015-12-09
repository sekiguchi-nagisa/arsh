#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --trace-exit -c 'exit 23' 2>&1 | grep 'Shell Exit: terminated by exit 23'

exit 0