#!/usr/bin/env bash

# for builtin read

YDSH_BIN=$1

# read command status
# if read success, return 0
# if read fail (error or end of file), return 1
echo hello | $YDSH_BIN -c 'read; assert($? == 0); read; assert($? == 1); exit 0'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# no splitting.
# terminate to newline
echo -e '   hello\n   world   \t   ' |
    $YDSH_BIN -c 'read; assert($REPLY == "hello"); read; assert($REPLY == "world")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# no splitting.
# if not specify separator, use IFS
echo -e ' \t  hello world \t \t  ' |
  $YDSH_BIN -c 'read; assert($REPLY == "hello world"); assert($reply.empty())'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# no splitting.
# specify separator.
echo '1hello1world ' |
  $YDSH_BIN -c 'read -f 1; assert($REPLY == "1hello1world ")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# no splitting.
# specify mutiple separator
# if separator contains space, ignore first and last spaces
echo '  1hello1world ' |
  $YDSH_BIN -c 'read -f " 1"; assert($REPLY == "1hello1world")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# use IFS
# remove first and last spaces
echo -e '   \t hello   world    ' |
  $YDSH_BIN -c \
  'read a b; assert($reply.size() == 2 &&
                    $reply["a"] == "hello" &&
                    $reply["b"] == "world"); assert($REPLY.empty())'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# use IFS
# remove first and last spaces
# splitted variables are less than specified them, set empty string.
echo -e '   \t hello   world    ' |
  $YDSH_BIN -c \
  'read a b c; assert($reply.size() == 3 &&
                      $reply["a"] == "hello" &&
                      $reply["b"] == "world" &&
                      $reply["c"].empty())'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# use IFS
# remove first and last spaces
echo -e '   \t hello   world  !!  ' |
  $YDSH_BIN -c \
  'read a b; assert($reply.size() == 2 &&
                      $reply["a"] == "hello" &&
                      $reply["b"] == "world  !!")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify separator
echo -e 'hello1world' |
  $YDSH_BIN -c \
  'read -f 1 a b; assert($reply.size() == 2 &&
                      $reply["a"] == "hello" &&
                      $reply["b"] == "world")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify separator
echo 'hello\1world' |
  $YDSH_BIN -c \
  'read -f 1 a b; assert($reply.size() == 2 &&
                      $reply["a"] == "hello1world" &&
                      $reply["b"].empty())'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify multiple separator
echo -e 'hello1world2!!' |
  $YDSH_BIN -c \
  'read -f 12 a b c; assert($reply.size() == 3 &&
                      $reply["a"] == "hello" &&
                      $reply["b"] == "world" &&
                      $reply["c"] == "!!")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify multiple separator
echo -e 'hello1world2!!' |
  $YDSH_BIN -c \
  'read -f 12 a b; assert($reply.size() == 2 &&
                      $reply["a"] == "hello" &&
                      $reply["b"] == "world2!!")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify multiple separator
# if separator contains spaces, remove spaces
echo -e '   hello  1  world22!!  ' |
  $YDSH_BIN -c \
  'read -f " 21" a b c; assert($reply.size() == 3 &&
                               $reply["a"] == "hello" &&
                               $reply["b"] == "world" &&
                               $reply["c"] == "2!!")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify multiple separator
# if separator contains spaces, remove spaces
echo -e '   hello  21  world22!!  ' |
  $YDSH_BIN -c \
  'read -f " 21" a b c; assert($reply.size() == 3 &&
                               $reply["a"] == "hello" &&
                               $reply["b"] == "" &&
                               $reply["c"] == "world22!!")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# splitting
# specify multiple separator
# if separator contains spaces, remove spaces
echo '   hello  \21  world22!!  ' |
  $YDSH_BIN -c \
  'read -f " 21" a b c; assert($reply.size() == 3 &&
                               $reply["a"] == "hello" &&
                               $reply["b"] == "2" &&
                               $reply["c"] == "world22!!")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# raw mode
echo '   hello\ world  ' |
  $YDSH_BIN -c \
  'read -r a b; assert($reply.size() == 2 &&
                       $reply["a"] == "hello\\" &&
                       $reply["b"] == "world")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# raw mode
echo -e '   hello\\\nworld  ' |
  $YDSH_BIN -c \
  'read -r; assert($REPLY == "hello\\")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# raw mode
echo '   hello\1world  ' |
  $YDSH_BIN -c \
  'read -r -f " 1" a b; assert($reply.size() == 2 &&
                               $reply["a"] == "hello\\" &&
                               $reply["b"] == "world")'

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# invalid file descriptor
test "$($YDSH_BIN -c 'read -u 89899' 2>&1)" = "-ydsh: read: 89899: Bad file descriptor"

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi


# timeout
test "$($YDSH_BIN -c 'read -t -1' 2>&1)" = "-ydsh: read: -1: invalid timeout specification"

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi

test "$($YDSH_BIN -c 'read -t 9999999999999999' 2>&1)" = "-ydsh: read: 9999999999999999: invalid timeout specification"

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi

$YDSH_BIN -c 'read -t 1'

if [ $? != 1 ]; then
    echo $LINENO
    exit 1
fi

# invalid option
test "$($YDSH_BIN -c 'read -q' 2>&1)" = "$(cat << EOF
-ydsh: read: -q: invalid option
read: read [-r] [-p prompt] [-f field separator] [-u fd] [-t timeout] [name ...]
EOF
)"

if [ $? != 0 ]; then
    echo $LINENO
    exit 1
fi

exit 0

