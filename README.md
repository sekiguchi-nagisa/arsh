[![Build Status](https://travis-ci.org/sekiguchi-nagisa/ydsh.svg?branch=master)](https://travis-ci.org/sekiguchi-nagisa/ydsh)

# ydsh
yet another dsh

Currently, under heavy development.
Language specification is subject to change without notice. 

## Build Requirement

* Linux x64
* cmake 2.8 or later
* make
* expect (for testing)
* gcc/clang (need c++11 support)
* libdbus 1.6.x or later
* libxml2 (for D-Bus introspection)

### Tested Compiler
<<<<<<< HEAD
* gcc (4.8, 4.9, 5)
* clang (3.5, 3.6, 3.7, 3.8)
=======
* gcc 4.8
* clang 3.5 3.6 3.7
>>>>>>> master

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
