
[![License](https://img.shields.io/badge/license-Apache%202-blue.svg)](#license)
[![Build Status](https://travis-ci.org/sekiguchi-nagisa/ydsh.svg?branch=master)](https://travis-ci.org/sekiguchi-nagisa/ydsh)
[![Coverage Status](https://coveralls.io/repos/github/sekiguchi-nagisa/ydsh/badge.svg?branch=master)](https://coveralls.io/github/sekiguchi-nagisa/ydsh?branch=master)

# ydsh
yet another dsh

Currently, under heavy development.
Language specification is subject to change without notice. 

## Build Requirement
* Linux x64
* cmake 2.8 or later
* autotools (for building re2c)
* make/ninja
* expect (for testing)
* gcc/clang (need c++11 support, need GNU extension)
* libpcre

### Tested Compiler
* gcc (4.8, 4.9, 5, 6, 7, 8)
* clang (3.5, 3.6, 3.7, 3.8, 3.9, 4, 5, 6)

## How to use

```
$ cmake .
$ make
$ ./ydsh
```
