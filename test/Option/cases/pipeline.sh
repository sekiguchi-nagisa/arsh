#!/usr/bin/env bash


YDSH_BIN=$1

$YDSH_BIN << EOF

exit 67

EOF

if [ $? != 67 ]; then
    exit 1
fi

exit 0