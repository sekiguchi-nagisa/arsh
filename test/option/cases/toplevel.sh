#!/usr/bin/env bash


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


$YDSH_BIN --print-toplevel -c '$true' | grep '(Boolean) true'

exit 0