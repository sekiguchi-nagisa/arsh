#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


# check version messsage
$YDSH_BIN --version | grep 'ydsh, version'

# chech copyright
$YDSH_BIN --version | grep 'Copyright (C)'

exit 0