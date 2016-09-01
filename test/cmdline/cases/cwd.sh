#!/usr/bin/env bash

# test cd and pwd command

YDSH_BIN=$1

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

TARGET=work/actual
(cd $DIR && mkdir -p ./$TARGET && ln -s ./$TARGET ./link)

# follow symbolic link
$YDSH_BIN -c "cd $DIR/link; "'assert $PWD == "'"$DIR/link"'"'

$YDSH_BIN -c "cd $DIR/link; "'assert "$(pwd)" == "'"$DIR/link"'"'

$YDSH_BIN -c "cd -L $DIR/link; "'assert "$(pwd -L)" == "'"$DIR/link"'"'

$YDSH_BIN -c "assert(cd $DIR/link); "'assert "$(pwd -P)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd $DIR/link); assert(cd ../); "'assert "$(pwd -P)" == "'"$DIR"'"'

$YDSH_BIN -c "assert(cd $DIR/link); assert(cd ../); "'assert "$(pwd -L)" == "'"$DIR"'"'

$YDSH_BIN -c "assert(cd $DIR/link); assert(cd ../); "'assert $OLDPWD == "'"$DIR/link"'"'

# without symbolic link
$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert $PWD == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert "$(pwd)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert "$(pwd -P)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert "$(pwd -L)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); assert(cd ../); "'assert "$(pwd -L)" == "'"$DIR/work"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); assert(cd ../); "'assert $OLDPWD == "'"$DIR/$TARGET"'"'