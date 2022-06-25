[![License](https://img.shields.io/badge/license-Apache%202-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Coverage Status](https://coveralls.io/repos/github/sekiguchi-nagisa/ydsh/badge.svg?branch=master)](https://coveralls.io/github/sekiguchi-nagisa/ydsh?branch=master)
[![Actions Status](https://github.com/sekiguchi-nagisa/ydsh/workflows/GitHub%20Actions/badge.svg)](https://github.com/sekiguchi-nagisa/ydsh/actions)
[![CircleCI](https://circleci.com/gh/sekiguchi-nagisa/ydsh.svg?style=shield)](https://circleci.com/gh/sekiguchi-nagisa/ydsh)

# ydsh

A statically typed scripting language with shell-like features.

Currently, under heavy development. Language specification is subject to change without notice.

## Build Requirement

* Linux x86-64
* cmake 3.8 or later
* git (for fetching external projects)
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
* clang (6, 7, 8, 9, 10, 11, 12, 13, 14)

### Other Tested platform

* Linux
    * Arm64
* macOS
    * x86-64
    * Arm64
* Windows x64
    * Cygwin
    * WSL

## How to use

1. build and install

```sh
$ git clone https://github.com/sekiguchi-nagisa/ydsh.git
$ cd ydsh && mkdir build && cd build
$ cmake ..    # default install dir is /usr/local/bin
$ cmake --build ..
$ sudo cmake --build .. -- install
```

2. create rcfile (`ydshrc`)

```sh
$ ./ydsh ../tools/script/genrc.ds
```

3. run in interactive mode

```sh
$ ydsh
```
