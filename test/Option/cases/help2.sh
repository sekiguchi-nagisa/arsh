#!/usr/bin/env bash

YDSH_BIN=$1

# ignore rest option
$YDSH_BIN --norc --help --version | grep 'Options:'

exit $?

