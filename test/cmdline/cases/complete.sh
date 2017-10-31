#!/usr/bin/env bash

# test completer when having symbilic link

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
(cd $DIR && mkdir -p ./$TARGET && ln -s ./$TARGET ./link && cd ./link && touch hogehuga && chmod +x hogehuga)

# follow symbolic link
$YDSH_BIN -c "cd $DIR; assert "'"$(complete ./link/)" == "hogehuga"'

$YDSH_BIN -c "cd $DIR; assert "'$(complete ./link/../).size() == 2'

$YDSH_BIN -c "cd $DIR; assert "'$(complete ./link/../)[0] == "link/"'

$YDSH_BIN -c "cd $DIR; assert "'$(complete ./link/../)[1] == "work/"'


cleanup_tmpdir