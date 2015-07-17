#!/usr/bin/env bash

YDSH_BIN=$1


trap "echo trap error; exit 1" ERR


$YDSH_BIN -c 'assert($0 == "ydsh")'

$YDSH_BIN -c 'assert($0 == "A"); assert($@.size() == 1); assert($@[0] == "G")' A G

exit 0