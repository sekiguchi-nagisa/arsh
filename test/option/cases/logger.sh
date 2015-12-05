#!/usr/bin/env bash

# test logger
# if not set USE_LOGGING, do nothing

YDSH_BIN=$1

YDSH_TRACE_TOKEN=on YDSH_DUMP_EXEC=on $YDSH_BIN -c 'echo hello logger | grep hello'
if [ $? != 0 ]; then
    exit 1
fi

# specify appender
YDSH_TRACE_TOKEN=on YDSH_APPENDER=/dev/stdout $YDSH_BIN -c 'var a = 0; exit $a'

exit $?