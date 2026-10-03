# arsh regex-performance benchmark

Support for measuring the arsh regex engine with
[rust-leipzig/regex-performance](https://github.com/rust-leipzig/regex-performance), together with
the most common C++ regex engines as comparison targets.

The upstream tool drives a fixed set of 18 patterns against a large text file and reports the
time and score of each registered engine. This directory provides

| file                      | description                                                             |
|---------------------------|-------------------------------------------------------------------------|
| `arsh.cpp` / `arsh.cmake` | adapter and build support for the arsh `regex` engine (`arsh` and `arsh_unsafe`) |
| `srell.cpp` / `srell.cmake` | adapter and build support for [SRELL](https://github.com/upa-url/srell) |
| `quickjs.c` / `quickjs.cmake` | adapter and build support for QuickJS `libregexp`                   |
| `boost.cmake`             | build support for Boost.Regex                                           |
| `regex-performance.patch` | patch registering the engines into the upstream harness                |
| `run.arsh`                | script which fetches, patches, builds and runs the benchmark           |

## Quick start

Build arsh first (the adapter links the static libraries from an existing arsh build), then run:

```sh
$ mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
$ cd ..
$ arsh tools/regex-bench/run.arsh --output results.csv
```

By default only the arsh engine is measured. To compare against other engines:

```sh
$ arsh tools/regex-bench/run.arsh --std-regex --srell --quickjs --boost
```

* `--std-regex` adds `std::regex` (the C++ standard library, system compiler).
* `--srell` adds SRELL; the sources are cloned automatically (header-only).
* `--quickjs` adds QuickJS `libregexp`; the sources are cloned and compiled automatically.
* `--boost` adds Boost.Regex; the latest Boost release is downloaded and built automatically.

`run.arsh` performs the following steps:

1. clones `rust-leipzig/regex-performance` into `.regex-bench/regex-performance` (pinned commit),
2. applies `regex-performance.patch` and copies the adapters into it,
3. builds the arsh `regex` libraries (and downloads/builds SRELL, QuickJS and Boost if requested),
4. builds `regex_perf`,
5. runs it against the bundled `3200.txt`.

## Options

| option                | description                                              |
|-----------------------|----------------------------------------------------------|
| `--repeat N`          | number of repetitions (default: 5)                       |
| `--output F`          | write the measured data into a CSV file                  |
| `--input F`           | use a custom input file instead of `3200.txt`            |
| `--build TYPE`        | arsh build type (default: `Release`)                     |
| `--work DIR`          | working directory (default: `.regex-bench`)              |
| `--keep`              | keep the fetched work directory                          |
| `--setup`             | only fetch and patch, do not build or run                |
| `--std-regex`         | add `std::regex` as a comparison target                  |
| `--srell`             | add SRELL as a comparison target                         |
| `--srell-dir DIR`     | use an existing SRELL source directory                   |
| `--quickjs`           | add QuickJS (`libregexp`) as a comparison target         |
| `--quickjs-dir DIR`   | use an existing QuickJS source directory                 |
| `--boost`             | add Boost.Regex as a comparison target                   |
| `--boost-dir DIR`     | use an existing Boost installation (`prefix`)            |
| `--boost-version VER` | Boost version to fetch (default: latest, e.g. `1.92.0`)  |

When a Boost installation is already available, pass `--boost-dir $(brew --prefix boost)` (macOS)
or the `--prefix` used for `./b2 install`. SRELL builds from the bundled single-header variant, so
`--srell-dir <repo>/srell-src/single-header` works as well.

## Manual integration

If you already have a copy of `regex-performance`, apply the changes by hand:

```sh
$ git apply /path/to/arsh/tools/regex-bench/regex-performance.patch
$ cp /path/to/arsh/tools/regex-bench/{arsh,srell}.cpp      src/
$ cp /path/to/arsh/tools/regex-bench/quickjs.c              src/
$ cp /path/to/arsh/tools/regex-bench/{arsh,srell,quickjs,boost}.cmake src/

$ mkdir build && cd build
$ cmake .. \
      -DINCLUDE_ARSH=local -DARSH_SOURCE_DIR=/path/to/arsh -DARSH_BUILD_DIR=/path/to/arsh/build \
      -DINCLUDE_SRELL=local -DSRELL_SOURCE_DIR=/path/to/srell/single-header \
      -DINCLUDE_QUICKJS=local -DQUICKJS_SOURCE_DIR=/path/to/quickjs \
      -DINCLUDE_BOOST=system -DBOOST_ROOT=/path/to/boost \
      -DINCLUDE_CPPSTD=system \
      -DINCLUDE_PCRE2=disabled # ... any other engine you want to skip
$ make regex_perf
$ ./src/regex_perf -f ../3200.txt -o results.csv
```

## Notes

* The arsh adapter maps the leading `(?i)` modifier (used by two benchmark patterns) to the arsh
  `i` flag, since arsh only accepts flags via `Flag::parse`.
* arsh only accepts the `u`/`v` modes; benchmark subjects are matched in Unicode (`u`) mode.
* Match counting emulates the global (`g`) flag by repeatedly calling `regex::match` and
  advancing the input, which is the same semantics the other engines use.
* SRELL and QuickJS `libregexp` are the closest comparison targets for arsh here: both are
  ECMAScript-compatible Unicode engines, just like arsh. Boost.Regex additionally supports the
  `(?i)` and `\p{...}` syntax used by the benchmark patterns. `std::regex` rejects `(?i)` inline
  modifiers and is therefore reported as a failure (`999999`) for those patterns, matching
  upstream behavior.
* The QuickJS adapter compiles `libregexp.c` / `libunicode.c` from the QuickJS tree and provides
  the three `lre_*` callbacks that libregexp expects from its host.
* `arsh.cpp` registers two arsh entries: `arsh` creates a fresh `MatchContext` for every scan
  (safe, mirrors normal use), while `arsh_unsafe` creates the context (and its loop-state
  buffers) once and reuses it across the repeated scans, only resetting the input position. The
  unsafe variant skips per-scan context construction and UTF-8 validation of the subject, so the
  gap between the two shows how much of a scanned time is context setup rather than matching.
  It is only valid because every scan uses the same regex and text.
