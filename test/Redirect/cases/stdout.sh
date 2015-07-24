#!/usr/bin/env bash


DIR="$(mktemp -d 2> /dev/null || mktemp -d -t ghjre9)"

cleanup_tmpdir() {
    rm -rf $DIR
}

trap 'echo trap error; cleanup_tmpdir; exit 1' ERR


TARGET=$DIR/hoge.txt


YDSH_BIN=$1

$YDSH_BIN -c '__puts -1 AAA' | grep AAA

# builtin command
$YDSH_BIN -c "__puts -1 ABC > $TARGET"
test "$(cat $TARGET)" = "$(echo ABC)"

$YDSH_BIN -c "__puts -1 123 1> $TARGET"
test "$(cat $TARGET)" = "$(echo 123)"

$YDSH_BIN -c "__puts -1 DEF >> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF)"

$YDSH_BIN -c "__puts -1 GHI 1>> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF && echo GHI)"


# external command
$YDSH_BIN -c "sh -c 'echo ABC' > $TARGET"
test "$(cat $TARGET)" = "$(echo ABC)"

$YDSH_BIN -c "sh -c 'echo 123' 1> $TARGET"
test "$(cat $TARGET)" = "$(echo 123)"

$YDSH_BIN -c "sh -c 'echo DEF' >> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF)"

$YDSH_BIN -c "sh -c 'echo GHI' 1>> $TARGET"
test "$(cat $TARGET)" = "$(echo 123 && echo DEF && echo GHI)"


cleanup_tmpdir
exit 0