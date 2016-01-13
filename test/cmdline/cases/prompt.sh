#!/usr/bin/env bash

# test for time dependent prompt string
# if not set USE_FIXED_TIME, do nothing

YDSH_BIN=$1

$YDSH_BIN --feature | grep USE_FIXED_TIME

if [ $? != 0 ]; then
    echo not set USE_FIXED_TIME, do nothing
    exit 0
fi


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR


export TIME_SOURCE='2016-1-13T15:15:12Z'

test "$($YDSH_BIN -e ps_intrp '\d')" = "Wed 1 13"
test "$($YDSH_BIN -e ps_intrp '\t')" = "15:15:12"
test "$($YDSH_BIN -e ps_intrp '\T')" = "03:15:12"
test "$($YDSH_BIN -e ps_intrp '\@')" = "03:15 PM"