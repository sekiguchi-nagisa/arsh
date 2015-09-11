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

# builtin command
test "$($YDSH_BIN -c '__puts -1 AAA -2 123 2>&1')" = "$(echo AAA && echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 2>&1 > /dev/null')" = "$(echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 2>&1 2> /dev/null')" = "$(echo AAA)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1>&2')" = "$(echo)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1>&2' 2>&1)" = "$(echo AAA && echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1>&2 > /dev/null' 2>&1)" = "$(echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1>&2 2> /dev/null' 2>&1)" = "$(echo AAA)"


$YDSH_BIN -c "__puts -1 AAA -2 123 &> $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123)"

$YDSH_BIN -c "__puts -1 AAA -2 123 >& $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123)"

$YDSH_BIN -c "__puts -1 AAA -2 123 &>> $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123 && echo AAA && echo 123)"


# external command
test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 2>&1')" = "$(echo AAA && echo 123)"

test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 2>&1 > /dev/null')" = "$(echo 123)"

test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 2>&1 2> /dev/null')" = "$(echo AAA)"

test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 1>&2')" = "$(echo)"

test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 1>&2' 2>&1)" = "$(echo AAA && echo 123)"

test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 1>&2 > /dev/null' 2>&1)" = "$(echo 123)"

test "$($YDSH_BIN -c 'sh -c "echo AAA && echo 123 1>&2" 1>&2 2> /dev/null' 2>&1)" = "$(echo AAA)"


$YDSH_BIN -c "sh -c 'echo AAA && echo 123 1>&2' &> $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123)"

$YDSH_BIN -c "sh -c 'echo AAA && echo 123 1>&2' >& $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123)"

$YDSH_BIN -c "sh -c 'echo AAA && echo 123 1>&2' &>> $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123 && echo AAA && echo 123)"


# eval
test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 2>&1')" = "$(echo AAA && echo 123)"

test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 2>&1 > /dev/null')" = "$(echo 123)"

test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 2>&1 2> /dev/null')" = "$(echo AAA)"

test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 1>&2')" = "$(echo)"

test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 1>&2' 2>&1)" = "$(echo AAA && echo 123)"

test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 1>&2 > /dev/null' 2>&1)" = "$(echo 123)"

test "$($YDSH_BIN -c 'eval sh -c "echo AAA && echo 123 1>&2" 1>&2 2> /dev/null' 2>&1)" = "$(echo AAA)"


$YDSH_BIN -c "eval sh -c 'echo AAA && echo 123 1>&2' &> $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123)"

$YDSH_BIN -c "eval sh -c 'echo AAA && echo 123 1>&2' >& $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123)"

$YDSH_BIN -c "eval sh -c 'echo AAA && echo 123 1>&2' &>> $TARGET"
test "$(cat $TARGET)" = "$(echo AAA && echo 123 && echo AAA && echo 123)"


# piped
test "$($YDSH_BIN -c '__puts -1 AAA -2 123 | grep AAA')" = "$(echo AAA)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1> /dev/null | grep AAA' 2>&1)" = "$(echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1> /dev/null | grep AAA 2> /dev/null' 2>&1)" = "$(echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 1> /dev/null 2>&1 | grep AAA' 2>&1)" = "$(echo)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 2>&1 | grep 123' 2>&1)" = "$(echo 123)"

test "$($YDSH_BIN -c '__puts -1 AAA -2 123 2>&1 1> /dev/null | grep AAA' 2>&1)" = "$(echo)"


cleanup_tmpdir
exit 0