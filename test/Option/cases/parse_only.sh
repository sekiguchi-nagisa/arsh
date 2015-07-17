#!/usr/bin/env bash

trap "echo trap error; exit 1" ERR

YDSH_BIN=$1

$YDSH_BIN --parse-only -c 'exit 88'

exit 0