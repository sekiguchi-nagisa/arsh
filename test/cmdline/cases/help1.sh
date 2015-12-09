#!/usr/bin/env bash

YDSH_BIN=$1

$YDSH_BIN --help | grep 'Options:'

exit $?

