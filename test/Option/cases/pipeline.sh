#!/usr/bin/env bash

trap "echo trap error; exit 1" ERR

YDSH_BIN=$1

echo 'assert($0 == "ydsh")' | $YDSH_BIN

echo -n '\' | $YDSH_BIN

exit 0
