# arsh regex-performance benchmark

Support for measuring the arsh regex engine with
[rust-leipzig/regex-performance](https://github.com/rust-leipzig/regex-performance), together with
the most common C++ regex engines as comparison targets.

The upstream tool drives a fixed set of 18 patterns against a large text file and reports the
time and score of each registered engine. This directory provides:

| file                      | description                                                             |
|---------------------------|-------------------------------------------------------------------------|
| `arsh.cpp` / `arsh.cmake` | adapter and build support for the arsh `regex` engine                   |
| `srell.cpp` / `srell.cmake` | adapter and build support for [SRELL](https://github.com/upa-url/srell) |
| `quickjs.c` / `quickjs.cmake` | adapter and build support for QuickJS `libregexp`                   |
| `hermes.cpp` / `hermes.cmake` | adapter and build support for the Hermes regex engine             |
| `pcre2.c` / `pcre2.cmake` | adapter and build support for PCRE2 (interpreted and JIT)              |
| `nodejs.cpp` / `nodejs.cmake` | adapter and build support for the Node.js regexp engine (V8)        |
| `boost.cmake`             | build support for Boost.Regex                                           |
| `memory_tracker.cpp` / `memory_tracker.h` | heap accounting shared by the adapters (see "Memory measurement") |
| `regex-performance.patch` | patch registering the engines into the upstream harness                |
| `run.arsh`                | script which fetches, patches, builds and runs the benchmark           |
| `rematch.cpp` + `rematch_*.{cpp,c,h}` | standalone matcher embedding the same engines (see "Standalone matcher") |
| `rematch_test.cpp`        | integration test for `rematch`                                          |
| `CMakeLists.txt`          | standalone project building `rematch` / `rematch_test`                  |
| `build_rematch.arsh`      | script which builds `rematch` from the fetched engines                  |

## Quick start

Build arsh first (the adapter links the static libraries from an existing arsh build), then run:

```sh
$ mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4
$ cd ..
$ arsh tools/regex-bench/run.arsh --output results.csv
```

By default only the arsh engine is measured. To compare against other engines:

```sh
$ arsh tools/regex-bench/run.arsh --std-regex --srell --quickjs --hermes --pcre2 --nodejs --boost
```

* `--std-regex` adds `std::regex` (the C++ standard library, system compiler).
* `--srell` adds SRELL; the sources are cloned automatically (header-only).
* `--quickjs` adds QuickJS `libregexp`; the sources are cloned and compiled automatically.
* `--hermes` adds Hermes; the sources are cloned and the regex engine is built automatically.
* `--pcre2` adds PCRE2, both the interpreter (`pcre`) and the JIT (`pcre-jit`); the pinned
  release is downloaded and built automatically with JIT support.
* `--nodejs` adds the Node.js regexp engine (V8); it runs the `node` executable found on `PATH`,
  so Node.js has to be installed.
* `--boost` adds Boost.Regex; the latest Boost release is downloaded and built automatically.

`run.arsh` performs the following steps:

1. clones `rust-leipzig/regex-performance` into `.regex-bench/regex-performance` (pinned commit),
2. applies `regex-performance.patch` and copies the adapters into it,
3. builds the arsh `regex` libraries (and downloads/builds SRELL, QuickJS, Hermes, PCRE2 and Boost if requested),
4. builds `regex_perf`,
5. runs it against the bundled `3200.txt`.

`--prepare` stops after step 3 for SRELL, QuickJS and Hermes (no arsh build and no `regex_perf`)
and prints their resolved locations. It is used by `build_rematch.arsh` to build the standalone
`rematch` tool from the same engine trees.

`--cxx` selects the C++ compiler used for the arsh libraries and for `regex_perf` (the C
compiler of the same toolchain is used as well, e.g. `clang++-18` selects `clang-18`):

```sh
$ arsh tools/regex-bench/run.arsh --cxx clang++
$ arsh tools/regex-bench/run.arsh --cxx g++-14 --std-regex --output gcc.csv
```

A compiler builds into its own directories (`arsh-build-gcc`, `regex-perf-build-gcc`, ...), so the
compilers do not overwrite each other's CMake cache and `g++`/`clang++` results can be produced
from the same work directory. Without `--cxx` the ambient compiler (the CMake default) is used and
the original directory names (`arsh-build`, `regex-perf-build`) are kept.

The comparison engines that `run.arsh` downloads and builds itself (Hermes, PCRE2, Boost) are
prepared once per work directory and shared, so both compiler runs measure the same engine
libraries.

## Standalone matcher

`rematch` is a small standalone program that embeds the SRELL / QuickJS / Hermes engines used
by the benchmark and selects one with `-e`. It is a version of the arsh `redump -m` dump tool
and prints the same output (`input: ...` followed by one `(offset=, size=)` line per capture
group, or `failed`), using UTF-8 byte offsets:

```
rematch -e <hermes|quickjs|srell> -m <input> <pattern> [modifiers]
```

The optional `modifiers` argument is the same flag string `redump` accepts (`u`, `v`, `i`,
`m`, `s`); the engines that do not implement `v` (unicode sets) fall back to the closest mode.
It is useful for cross-checking arsh against the other engines on a single pattern.

`rematch` is built as its own CMake project (`CMakeLists.txt` in this directory), separately
from the top-level arsh build. It reuses the engine sources prepared by `run.arsh` and the
arsh regex libraries of an existing arsh build. The convenience script
`build_rematch.arsh` performs the whole flow (prepare the engines, build the arsh libraries,
configure and build `rematch` and its `rematch_test` integration test):

```sh
$ arsh tools/regex-bench/build_rematch.arsh
prepare engines: tools/regex-bench/run.arsh --prepare
...
built: <repo>/build-rematch/rematch
```

When at least one engine is enabled, the `rematch_test` target is also built; it runs an
engine-independent case against every enabled engine. The test links the arsh test support
libraries (`test_common` / `process` / `platform`) and GoogleTest from the arsh build
directory, so those targets must exist there (the script builds them).

| option             | description                                                     |
|--------------------|-----------------------------------------------------------------|
| `--build DIR`      | rematch build directory (default: `build-rematch`)              |
| `--build-type T`   | CMake build type (default: `Release`)                           |
| `--work DIR`       | regex-bench work directory (default: `.regex-bench`)            |
| `--arsh-build DIR` | arsh build directory (default: `<work>/arsh-build`)             |
| `--no-prepare`     | skip the engine preparation step, reuse the current work tree   |
| `--srell-dir DIR`  | use an existing SRELL source directory                          |
| `--quickjs-dir DIR`| use an existing QuickJS source directory                        |
| `--hermes-dir DIR` | use an existing Hermes source directory                         |
| `--hermes-build-dir DIR` | use an existing Hermes build directory                    |

If an engine's sources are not found, that engine is skipped with a configuration warning. To
point at different locations, or to disable an engine, configure the project directly with the
same cache variables used by the benchmark:

```sh
cmake -S tools/regex-bench -B build-rematch \
      -DARSH_SOURCE_DIR=<arsh repo> -DARSH_BUILD_DIR=<arsh build> \
      -DREMATCH_ENABLE_SRELL=ON   -DSRELL_SOURCE_DIR=<srell/single-header> \
      -DREMATCH_ENABLE_QUICKJS=ON -DQUICKJS_SOURCE_DIR=<quickjs> \
      -DREMATCH_ENABLE_HERMES=ON  -DHERMES_SOURCE_DIR=<hermes> \
                                  -DHERMES_BUILD_DIR=<hermes-build>
cmake --build build-rematch --target rematch
```

Examples:

```sh
$ rematch -e srell   -m '12a' '(a)' ''
input: `12a'
(offset=2, size=1)
(offset=2, size=1)

$ rematch -e quickjs -m 'ABC' 'abc' 'i'
input: `ABC'
(offset=0, size=3)

$ rematch -e hermes  -m '' '.'
input: `'
failed
```

## Options

| option                | description                                              |
|-----------------------|----------------------------------------------------------|
| `--repeat N`          | number of repetitions (default: 5)                       |
| `--output F`          | write the measured data into a CSV file                  |
| `--input F`           | use a custom input file instead of `3200.txt`            |
| `--build TYPE`        | arsh build type (default: `Release`)                     |
| `--cxx CXX`           | C++ compiler used to build arsh and the benchmark        |
| `--work DIR`          | working directory (default: `.regex-bench`)              |
| `--keep`              | keep the fetched work directory                          |
| `--setup`             | only fetch and patch, do not build or run                |
| `--prepare`           | only fetch and build the comparison engines, then stop   |
| `--std-regex`         | add `std::regex` as a comparison target                  |
| `--srell`             | add SRELL as a comparison target                         |
| `--srell-dir DIR`     | use an existing SRELL source directory                   |
| `--quickjs`           | add QuickJS (`libregexp`) as a comparison target         |
| `--quickjs-dir DIR`   | use an existing QuickJS source directory                 |
| `--hermes`            | add Hermes as a comparison target                        |
| `--hermes-dir DIR`    | use an existing Hermes source directory                  |
| `--hermes-build-dir DIR` | use an existing Hermes build directory                |
| `--boost`             | add Boost.Regex as a comparison target                   |
| `--boost-dir DIR`     | use an existing Boost installation (`prefix`)            |
| `--boost-version VER` | Boost version to fetch (default: latest, e.g. `1.92.0`)  |
| `--pcre2`             | add PCRE2 (`pcre` and `pcre-jit`) as comparison targets  |
| `--pcre2-dir DIR`     | use an existing PCRE2 build directory                    |
| `--nodejs`            | add the Node.js regexp engine as a comparison target     |
| `--nodejs-bin FILE`   | use a specific Node.js executable (default: `node`)      |

When a Boost installation is already available, pass `--boost-dir $(brew --prefix boost)` (macOS)
or the `--prefix` used for `./b2 install`. SRELL builds from the bundled single-header variant, so
`--srell-dir <repo>/srell-src/single-header` works as well.

Hermes is large, so `--hermes` clones it and builds only the regex engine and the support
libraries it needs. When a Hermes tree is already available, pass both `--hermes-dir <source>` and
`--hermes-build-dir <build>` to skip the clone and the build.

`--pcre2` downloads the pinned PCRE2 release and builds the 8-bit static library with JIT support
and position independent code enabled (`PCRE2_STATIC_PIC=ON`, `--pcre2-dir <build>` reuses an
existing build). The PIC flag is required because `regex_perf` is linked as a PIE executable by
default, and an archive built without PIC fails to link in that case with
`relocation R_X86_64_32S ... can not be used when making a PIE object` (clang++ always links PIE,
so the failure shows up with `--cxx clang++`). A build directory whose cache lacks
`PCRE2_STATIC_PIC=ON` is discarded and rebuilt. It registers two targets: `pcre` (interpreter)
and `pcre-jit`. PCRE2 is matched in its default 8-bit mode, like the upstream harness; the
interpreter is markedly slower than the JIT on several patterns.

`--nodejs` runs the installed Node.js executable as a child process (`--nodejs-bin <file>` selects
a specific one), so Node.js must be present. The `node` executable is looked up on `PATH` with
`find_program`, so a mise-managed Node.js works as long as the shims are on `PATH`. Since the engine
lives in a separate process, its memory columns are always `0`.

## Memory measurement

Every registered engine also reports its heap memory usage, split into two numbers:

* `instance`: the live bytes of the compiled regex instance itself.
* `runtime`: the peak extra bytes allocated while scanning the input.

They are printed next to the timings and written to the CSV (`<engine> [mem_inst]` and
`<engine> [mem_run]` columns). Both are in bytes; the printed columns are per-pattern and the
total is summed over the 18 patterns.

The measurement is provided by `memory_tracker.cpp`, which replaces the global
malloc/calloc/realloc/free and C++ operator new/delete and accounts every live block by its usable
size (the allocator's real footprint). Because all engines are linked into the same executable,
each adapter measures a delta around its own work:

```cpp
const size_t base = memory_tracker_live();
/* build the compiled regex */
res->mem_instance = memory_tracker_live() - base;

memory_tracker_reset_peak();
const size_t scan_base = memory_tracker_live();
/* run all the matches */
res->mem_runtime = memory_tracker_peak() - scan_base;
```

The tracker is enabled on glibc (Linux) and is a no-op elsewhere, in which case both numbers stay
`0`. It tracks heap allocations only: memory obtained directly from the OS (e.g. `mmap`) or held
on the stack is not included.

Because the numbers are heap deltas around an engine's own calls, a one-time module-level cache
that a library initializes on first use is attributed to the first pattern that triggers it (for
example, the arsh Unicode property-name map, which is built the first time a `\p{...}` pattern is
compiled).

## CI

`.github/workflows/regex-bench.yml` builds and measures this directory (and the standalone
`rematch`) on GitHub Actions. It runs on `workflow_dispatch` and once a week (Friday 00:00 UTC),
and on each run it:

1. builds arsh and runs the regex suites (`regex_test` / `regex_e2e_test`),
2. fetches/builds the engines shared with `rematch` (`run.arsh --prepare`),
3. builds `rematch` and runs `rematch_test` (`build_rematch.arsh --no-prepare --arsh-build build`),
4. runs `run.arsh --std-regex --srell --quickjs --hermes` with `--cxx g++` and with
   `--cxx clang++`, and uploads both CSVs as one artifact.

The engine trees under `.regex-bench` are cached (keyed by `run.arsh`, which pins the engine
revisions), so only the first run pays for the Hermes clone and build. The benchmark itself takes
several minutes because the `cppstd` and arsh rows include patterns like `(.*?,){13}z`.

## Manual integration

If you already have a copy of `regex-performance`, apply the changes by hand:

```sh
$ git apply /path/to/arsh/tools/regex-bench/regex-performance.patch
$ cp /path/to/arsh/tools/regex-bench/{arsh,srell}.cpp      src/
$ cp /path/to/arsh/tools/regex-bench/quickjs.c              src/
$ cp /path/to/arsh/tools/regex-bench/hermes.cpp             src/
$ cp /path/to/arsh/tools/regex-bench/nodejs.cpp             src/
$ cp /path/to/arsh/tools/regex-bench/pcre2.c                src/
$ cp /path/to/arsh/tools/regex-bench/memory_tracker.{h,cpp}  src/
$ cp /path/to/arsh/tools/regex-bench/{arsh,srell,quickjs,hermes,boost,nodejs,pcre2}.cmake src/

$ mkdir build && cd build
$ cmake .. \
      -DINCLUDE_ARSH=local -DARSH_SOURCE_DIR=/path/to/arsh -DARSH_BUILD_DIR=/path/to/arsh/build \
      -DINCLUDE_SRELL=local -DSRELL_SOURCE_DIR=/path/to/srell/single-header \
      -DINCLUDE_QUICKJS=local -DQUICKJS_SOURCE_DIR=/path/to/quickjs \
      -DINCLUDE_HERMES=local -DHERMES_SOURCE_DIR=/path/to/hermes \
                             -DHERMES_BUILD_DIR=/path/to/hermes/build \
      -DINCLUDE_BOOST=system -DBOOST_ROOT=/path/to/boost \
      -DINCLUDE_PCRE2=local -DPCRE2_BUILD_DIR=/path/to/pcre2/build \
      -DINCLUDE_NODEJS=local -DNODEJS_EXECUTABLE=/path/to/node \
      -DINCLUDE_CPPSTD=system \
      -DINCLUDE_HYPERSCAN=disabled # ... any other engine you want to skip
$ make regex_perf
$ ./src/regex_perf -f ../3200.txt -o results.csv
```

## Notes

* The arsh adapter maps the leading `(?i)` modifier (used by two benchmark patterns) to the arsh
  `i` flag, since arsh only accepts flags via `Flag::parse`.
* arsh only accepts the `u`/`v` modes; benchmark subjects are matched in Unicode (`u`) mode.
* Match counting emulates the global (`g`) flag by repeatedly calling `regex::match` and
  advancing the input, which is the same semantics the other engines use.
* SRELL, QuickJS `libregexp`, Hermes and the Node.js engine are the closest comparison targets for
  arsh here: all are ECMAScript-compatible Unicode engines, just like arsh. Boost.Regex
  additionally supports the
  `(?i)` and `\p{...}` syntax used by the benchmark patterns. `std::regex` rejects `(?i)` inline
  modifiers and is therefore reported as a failure (`999999`) for those patterns, matching
  upstream behavior.
* The QuickJS adapter compiles `libregexp.c` / `libunicode.c` from the QuickJS tree and provides
  the three `lre_*` callbacks that libregexp expects from its host.
* The Hermes adapter compiles the pattern once with `UTF16RegexTraits` and executes it with
  `ASCIIRegexTraits` when the subject is 7-bit ASCII (or `UTF16RegexTraits` otherwise), mirroring
  what the VM does. Hermes' backtrack limit makes it report pattern `(.*?,){13}z` as a failure
  (`999999`), like the engines that cannot finish it. The adapter links the static libraries from
  an existing Hermes build tree and requires `HERMES_SOURCE_DIR` and `HERMES_BUILD_DIR`.
* The PCRE2 adapter (`pcre2.c`) replaces the one shipped with the harness so that the instance and
  the scan are measured with the heap tracker; it registers `pcre` and `pcre-jit` and requires
  `PCRE2_BUILD_DIR` pointing at a PCRE2 build with JIT support.
* The Node.js adapter (`nodejs.cpp`) runs the `node` executable as a child process and parses the
  timings and the match count it prints; only the scan is timed, and its memory columns are `0`
  because the heap tracker cannot see the other process.
* The arsh adapter (`arsh.cpp`) is registered once as `arsh`; it creates a fresh `MatchContext`
  for every scan, which mirrors normal use.
