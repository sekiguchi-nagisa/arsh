#!/usr/bin/env bash

trap "echo trap error; exit 1" ERR

YDSH_BIN=$1

$YDSH_BIN --disable-assertion -c 'assert($false)'

exit 0