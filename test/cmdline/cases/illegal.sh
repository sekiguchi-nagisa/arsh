#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --ho 2>&1 | grep 'illegal option: --ho'

$YDSH_BIN --ho 2>&1 | grep 'ydsh, version'

$YDSH_BIN --ho 2>&1 | grep 'Options:'

exit 0