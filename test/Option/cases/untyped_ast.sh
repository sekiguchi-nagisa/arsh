#!/usr/bin/env bash

trap "echo trap error; exit 1" ERR

YDSH_BIN=$1

$YDSH_BIN --dump-untyped-ast -c '12' | grep '### dump untyped AST ###'

$YDSH_BIN --dump-untyped-ast -c '12' | grep 'IntValueNode (lineNum: 1, type: (null))'

exit 0