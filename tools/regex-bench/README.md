# arsh regex-performance benchmark

Support for measuring the arsh regex engine with
[rust-leipzig/regex-performance](https://github.com/rust-leipzig/regex-performance).

The upstream tool drives a fixed set of 18 patterns against a large text file and reports the
time and score of each registered engine. This directory provides

| file                       | description                                                              |
|----------------------------|--------------------------------------------------------------------------|
| `arsh.cpp`                 | adapter implementing `arsh_find_all` on top of the arsh `regex` library  |
| `arsh.cmake`               | CMake fragment which links the prebuilt arsh static libraries            |
| `regex-performance.patch`  | patch registering the arsh engine into the upstream harness              |
| `run.arsh`                 | script which fetches, patches, builds and runs the benchmark             |

## Quick start

Build arsh first (the adapter links the static libraries from an existing arsh build), then run:

```sh
$ mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
$ cd ..
$ arsh tools/regex-bench/run.arsh --output results.csv
```

`run.arsh` performs the following steps:

1. clones `rust-leipzig/regex-performance` into `.regex-bench/regex-performance` (pinned commit),
2. applies `regex-performance.patch` and copies `arsh.cpp` / `arsh.cmake` into it,
3. builds the arsh `regex` libraries,
4. builds `regex_perf` with `-DINCLUDE_ARSH=local`,
5. runs it against the bundled `3200.txt`.

Options:

| option         | description                                        |
|----------------|----------------------------------------------------|
| `--repeat N`   | number of repetitions (default: 5)                 |
| `--output F`   | write the measured data into a CSV file            |
| `--input F`    | use a custom input file instead of `3200.txt`      |
| `--build TYPE` | arsh build type (default: `Release`)               |
| `--work DIR`   | working directory (default: `.regex-bench`)        |
| `--keep`       | keep the fetched work directory                    |
| `--setup`      | only fetch and patch, do not build or run          |

Only the arsh engine is built by default. To also benchmark the upstream engines, drop the
corresponding `-DINCLUDE_*=disabled` flags from `run.arsh` and install their dependencies.

## Manual integration

If you already have a copy of `regex-performance`, apply the changes by hand:

```sh
$ git apply /path/to/arsh/tools/regex-bench/regex-performance.patch
$ cp /path/to/arsh/tools/regex-bench/arsh.cpp  src/
$ cp /path/to/arsh/tools/regex-bench/arsh.cmake src/

$ mkdir build && cd build
$ cmake .. \
      -DINCLUDE_ARSH=local \
      -DARSH_SOURCE_DIR=/path/to/arsh \
      -DARSH_BUILD_DIR=/path/to/arsh/build \
      -DINCLUDE_PCRE2=disabled # ... any other engine you want to skip
$ make regex_perf
$ ./src/regex_perf -f ../3200.txt
```

## Notes

* The adapter maps the leading `(?i)` modifier (used by two benchmark patterns) to the arsh
  `i` flag, since arsh only accepts flags via `Flag::parse`.
* arsh only accepts the `u`/`v` modes; benchmark subjects are matched in Unicode (`u`) mode.
* Match counting emulates the global (`g`) flag by repeatedly calling `regex::match` and
  advancing the input, which is the same semantics the other engines use.
