#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --parse-only -c 'exit 88'

$YDSH_BIN -n -c 'exit 88'

exit 0