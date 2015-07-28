[![Build Status](https://travis-ci.org/sekiguchi-nagisa/ydsh.svg?branch=master)](https://travis-ci.org/sekiguchi-nagisa/ydsh)

# ydsh
yet another dsh

Currently, under heavy development.
Language specification is subject to change without notice. 

## Build Requirement

* Linux x64
* cmake 2.8+
* make
* g++ (need c++11 support)
* libedit (need unicode support. see http://thrysoee.dk/editline/)
* libdbus 1.8.x
* libxerces-c 3.1 (for D-Bus introspection)

## How to use

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```
if not need D-Bus support,
```
$ cmake .. -DUSE_DBUS=off
```

if not need iface_gen, (D-Bus interface definition generater)
```
$ cmake .. -DUSE_IFACE_GEN=off
```
