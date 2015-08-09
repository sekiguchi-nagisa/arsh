#!/usr/bin/env bash

YDSH_BIN=$1

trap "echo trap error; exit 1" ERR

$YDSH_BIN --ho 2>&1 | grep 'illegal option: --ho'

$YDSH_BIN --ho 2>&1 | grep 'ydsh, version'

$YDSH_BIN --ho 2>&1 | grep 'Options:'

exit 0