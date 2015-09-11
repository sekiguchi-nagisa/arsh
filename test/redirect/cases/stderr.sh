#!/usr/bin/env bash


DIR="$(mktemp -d 2> /dev/null || mktemp -d -t lfreop)"

cleanup_tmpdir() {
    rm -rf $DIR
}

ereport() {
    echo trap error in $1
    cleanup_tmpdir
    exit 1
}

trap 'ereport $LINENO' ERR


TARGET=$DIR/hoge.txt


YDSH_BIN=$1

$YDSH_BIN -c '__puts -2 AAA' 2>&1 | grep AAA

# builtin command
$YDSH_BIN -c "__puts -2 123 2> $TARGET"
test "$(cat $TARGET)" = "$(echo 123)"

$YDSH_BIN -c "__puts -2 DEF 2>> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF)"


# external command
$YDSH_BIN -c "sh -c 'echo 123 1>&2' 2> $TARGET"
test "$(cat $TARGET)" = "$(echo 123)"

$YDSH_BIN -c "sh -c 'echo DEF 1>&2' 2>> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF)"


# eval
$YDSH_BIN -c "eval sh -c 'echo 123 1>&2' 2> $TARGET"
test "$(cat $TARGET)" = "$(echo 123)"

$YDSH_BIN -c "eval sh -c 'echo DEF 1>&2' 2>> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF)"

cleanup_tmpdir
exit 0