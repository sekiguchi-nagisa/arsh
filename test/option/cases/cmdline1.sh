#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


$YDSH_BIN -c 'assert($0 == "ydsh")'

$YDSH_BIN -c 'assert($0 == "A"); assert($@.size() == 1); assert($@[0] == "G")' A G

$YDSH_BIN -c '\'  # do nothing

exit 0