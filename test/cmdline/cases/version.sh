#!/usr/bin/env bash

ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_BIN=$1


# check version messsage
$YDSH_BIN --version | grep 'ydsh, version'
$YDSH_BIN --version | grep "version $($YDSH_BIN -e ps_intrp '\v')"
$YDSH_BIN --version | grep "version $($YDSH_BIN -e ps_intrp '\V')"

# chech copyright
$YDSH_BIN --version | grep 'Copyright (C)'

exit 0