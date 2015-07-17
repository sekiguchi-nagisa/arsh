#!/usr/bin/env bash

trap "echo trap error; exit 1" ERR

YDSH_BIN=$1

$YDSH_BIN --dump-ast -c '12' | grep '### dump typed AST ###'

$YDSH_BIN --dump-ast -c '12' | grep 'IntValueNode (lineNum: 1, type: Int32)'

exit 0