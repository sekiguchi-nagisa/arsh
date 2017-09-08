#!/usr/bin/env bash

# test cd and pwd command

YDSH_BIN=$1

DIR="$PWD/cwd-test$$-temp$$"
mkdir $DIR

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
$YDSH_BIN -c "cd $DIR/link; import-env PWD; "'assert $PWD == "'"$DIR/link"'"'

$YDSH_BIN -c "cd $DIR/link; "'assert "$(pwd)" == "'"$DIR/link"'"'

$YDSH_BIN -c "cd -L $DIR/link; "'assert "$(pwd -L)" == "'"$DIR/link"'"'

$YDSH_BIN -c "assert(cd $DIR/link); "'assert "$(pwd -P)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd $DIR/link); assert(cd ../); "'assert "$(pwd -P)" == "'"$DIR"'"'

$YDSH_BIN -c "assert(cd $DIR/link); assert(cd ../); "'assert "$(pwd -L)" == "'"$DIR"'"'

$YDSH_BIN -c "assert(cd $DIR/link); assert(cd ../); import-env OLDPWD; "'assert $OLDPWD == "'"$DIR/link"'"'

# without symbolic link
$YDSH_BIN -c "assert(cd -P $DIR/link); import-env PWD; "'assert $PWD == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert "$(pwd)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert "$(pwd -P)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); "'assert "$(pwd -L)" == "'"$DIR/$TARGET"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); assert(cd ../); "'assert "$(pwd -L)" == "'"$DIR/work"'"'

$YDSH_BIN -c "assert(cd -P $DIR/link); assert(cd ../); import-env OLDPWD; "'assert $OLDPWD == "'"$DIR/$TARGET"'"'

cleanup_tmpdir
