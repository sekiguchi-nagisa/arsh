#!/usr/bin/env bash


trap "echo trap error; exit 1" ERR

YDSH_BIN=$1


$YDSH_BIN --print-toplevel -c '$true' | grep '(Boolean) true'

exit 0