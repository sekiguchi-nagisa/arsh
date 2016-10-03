#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

$YDSH_BIN --dump-ast -c '[12, 32] is Array<Int>' | grep '### dump typed AST ###'

exit 0