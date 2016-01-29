#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


# check version message
$YDSH_BIN --version | grep "ydsh, version $($YDSH_BIN -c 'echo $YDSH_VERSION')"

# chech copyright
$YDSH_BIN --version | grep 'Copyright (C)'

exit 0