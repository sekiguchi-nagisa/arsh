#!/usr/bin/env bash

DIR="$(mktemp -d 2> /dev/null || mktemp -d -t hferug)"

cleanup_tmpdir() {
    rm -rf $DIR
}

trap 'echo trap error; cleanup_tmpdir; exit 1' ERR

TARGET=$DIR/target.ds

echo 'assert($0 == "'${TARGET}'"); assert($@.size() == 1); assert($@[0] == "A")' > $TARGET

YDSH_BIN=$1

$YDSH_BIN $TARGET A


a=hfuierht456
$YDSH_BIN $a 2>&1 | grep "ydsh: $a"


cleanup_tmpdir
exit 0