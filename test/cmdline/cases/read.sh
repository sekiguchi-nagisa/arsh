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
# if not specify separator, use default separator (tab, white space, newline)
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
# use default separator
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
# use default separator
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
# use default separator
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


exit 0

