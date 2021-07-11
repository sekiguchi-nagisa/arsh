
[![License](https://img.shields.io/badge/license-Apache%202-blue.svg)](#license)
[![Build Status](https://travis-ci.com/sekiguchi-nagisa/ydsh.svg?branch=master)](https://travis-ci.com/sekiguchi-nagisa/ydsh)
[![Coverage Status](https://coveralls.io/repos/github/sekiguchi-nagisa/ydsh/badge.svg?branch=master)](https://coveralls.io/github/sekiguchi-nagisa/ydsh?branch=master)
[![Actions Status](https://github.com/sekiguchi-nagisa/ydsh/workflows/GitHub%20Actions/badge.svg)](https://github.com/sekiguchi-nagisa/ydsh/actions)

# ydsh
A statically typed shell language focusing on scripting usage.

Currently, under heavy development.
Language specification is subject to change without notice. 

## Build Requirement
* Linux x86-64
* cmake 3.8 or later
* autotools, libtool (for building re2c)
  * if cmake 3.12 or later, no longer need theme
* make/ninja
* gcc/clang (need gnu++17 support)
* libpcre2-8 10.30 or later
  * need UTF-8 and unicode property support

### Optional Requirement
* fzf (for history search)

### Tested Compiler
* gcc (7, 8, 9, 10, 11)
* clang (6, 7, 8, 9, 10, 11, 12)

### Other Tested platform
* Linux
  * x86
  * AArch64
* macOS
  * x86-64
* Windows x64
  * Cygwin
  * WSL

## How to use

```sh
$ cmake .
$ make && make install
$ echo source edit > ~/.ydshrc
$ ydsh
```

