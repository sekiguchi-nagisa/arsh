#!/usr/bin/env bash

# test logger
# if not set USE_LOGGING, do nothing

YDSH_BIN=$1

if $YDSH_BIN --feature | grep 'USE_LOGGING'; then
    :
else
    echo 'not set USE_LOGGING, do nothing'
    exit 0
fi


ereport() {
    echo trap error in $1
    exit 1
}

trap 'ereport $LINENO' ERR

YDSH_TRACE_TOKEN=on YDSH_DUMP_EXEC=on $YDSH_BIN -c 'echo hello logger | grep hello'

# specify appender
YDSH_TRACE_TOKEN=on YDSH_APPENDER=/dev/stdout $YDSH_BIN -c 'var a = 0; exit $a'

# specify appender (not found)
YDSH_TRACE_TOKEN=on YDSH_APPENDER=/dev/hogehode $YDSH_BIN -c 'var a = 0; exit $a'

exit 0