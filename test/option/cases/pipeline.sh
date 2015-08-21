#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1

echo 'assert($0 == "ydsh")' | $YDSH_BIN

echo -n '\' | $YDSH_BIN

exit 0
