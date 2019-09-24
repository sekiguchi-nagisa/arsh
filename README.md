
[![License](https://img.shields.io/badge/license-Apache%202-blue.svg)](#license)
[![Build Status](https://travis-ci.org/sekiguchi-nagisa/ydsh.svg?branch=master)](https://travis-ci.org/sekiguchi-nagisa/ydsh)
[![Coverage Status](https://coveralls.io/repos/github/sekiguchi-nagisa/ydsh/badge.svg?branch=master)](https://coveralls.io/github/sekiguchi-nagisa/ydsh?branch=master)
[![Actions Status](https://github.com/sekiguchi-nagisa/ydsh/workflows/main/badge.svg)](https://github.com/sekiguchi-nagisa/ydsh/actions)

# ydsh
A statically typed shell language focusing on scripting usage.

Currently, under heavy development.
Language specification is subject to change without notice. 

## Build Requirement
* Linux x64
* cmake 3.7 or later
* autotools, libtool (for building re2c)
* make/ninja
* gcc/clang (need gnu++14 support)
* libpcre

### Optional Requirement
* fzf (for history search)

### Tested Compiler
* gcc (5, 6, 7, 8, 9)
* clang (3.6, 3.7, 3.8, 3.9, 4, 5, 6, 7, 8)

## How to use

```sh
$ cmake .
$ make && make install
$ ydsh
```
