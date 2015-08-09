#!/usr/bin/env bash

YDSH_BIN=$1

trap "echo trap error; exit 1" ERR

# check version messsage
$YDSH_BIN --version | grep 'ydsh, version'

# chech copyright
$YDSH_BIN --version | grep 'Copyright (C)'

exit 0