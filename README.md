[![License](https://img.shields.io/badge/license-Apache%202-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Coverage Status](https://coveralls.io/repos/github/sekiguchi-nagisa/arsh/badge.svg?branch=master)](https://coveralls.io/github/sekiguchi-nagisa/arsh?branch=master)
[![Actions Status](https://github.com/sekiguchi-nagisa/arsh/workflows/GitHub%20Actions/badge.svg)](https://github.com/sekiguchi-nagisa/arsh/actions)
[![CircleCI](https://circleci.com/gh/sekiguchi-nagisa/arsh.svg?style=shield)](https://circleci.com/gh/sekiguchi-nagisa/arsh)
[![build result](https://build.opensuse.org/projects/home:nsekiguchi/packages/arsh/badge.svg?type=percent)](https://build.opensuse.org/package/show/home:nsekiguchi/arsh)

# arsh

A statically typed scripting language with shell-like features.

Currently, under heavy development. Language specification is subject to change without notice.

## Build Requirement

* Linux x86-64
* cmake 3.15 or later
* git (for fetching external projects)
* Python 3.7 or later (for building `re2c`)
* make/ninja
* gcc/clang (need gnu++17 support)
* libpcre2-8 10.30 or later
    * need UTF-8 and Unicode property support

### Optional Requirement

* fzf (for history search)
* bash-completion (for tab-completion)

### Tested Compiler

* gcc (9, 10, 11, 12, 13, 14)
* clang (11, 12, 13, 14, 15, 16, 17, 18, 19)

### Other Tested platform

* Linux
    * Arm64
* macOS
    * x86-64
    * Arm64
* Windows x64
    * Cygwin
    * MSYS2
    * WSL

## How to use

1. build and install

* install from source

```sh
$ git clone https://github.com/sekiguchi-nagisa/arsh.git
$ cd arsh && mkdir build && cd build
$ cmake ..    # default install dir is /usr/local/bin
$ make -j4
$ sudo make install
```

* [install from package](https://software.opensuse.org//download.html?project=home%3Ansekiguchi&package=arsh)
    * Debian 11, 12
    * Ubuntu 22.04, 24.04
    * Fedora 40, 41
    * openSUSE Tumbleweed

2. run in interactive mode

```sh
$ arsh
```
