#!/usr/bin/env bash

DIR="$(mktemp -d 2> /dev/null || mktemp -d -t hferug)"

cleanup_tmpdir() {
    rm -rf $DIR
}

trap 'cleanup_tmpdir' EXIT

TARGET=$DIR/target.ds

echo 'assert($0 == "'${TARGET}'"); assert($@.size() == 1); assert($@[0] == "A")' > $TARGET

if [ -e $TARGET ]; then
    :
else
    echo not found $TARGET
    exit 1
fi

YDSH_BIN=$1

$YDSH_BIN $TARGET A

if [ $? != 0 ]; then
    exit 1
fi

exit 0