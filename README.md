[![Build Status](https://travis-ci.org/sekiguchi-nagisa/ydsh.svg?branch=master)](https://travis-ci.org/sekiguchi-nagisa/ydsh)

# ydsh
yet another dsh

Currently, under heavy development.
Language specification is subject to change without notice. 

## Build Requirement

* Linux x64
* cmake 2.8+
* make
* expect (for testing)
* clang3.4 or later (need c++11 support)
* libdbus 1.8.x
* libxml2 (for D-Bus introspection)

## How to use

```
$ ./setup.sh
$ cd build
$ make
```
if not need D-Bus support,
```
$ cmake .. -DUSE_DBUS=off
```
