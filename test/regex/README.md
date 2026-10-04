# regex test tools

## `redump`

`dump.cpp` builds the `redump` helper used by the end-to-end tests under `cases/`. It parses a
pattern with the arsh regex engine and dumps either the parsed tree (`-d`), the compiled
bytecode (default) or the first match (`-m`).

## `rematch`

`rematch` is a standalone version of `redump -m` which embeds the JavaScript-compatible regex
engines used by [`tools/regex-bench`](../../tools/regex-bench) and selects one with `-e`:

```
rematch -e <hermes|quickjs|srell> -m <input> <pattern> [modifiers]
```

It supports only the `-m` option of `redump` and prints the same output (`input: ...` followed
by one `(offset=, size=)` line per capture group, or `failed`), using UTF-8 byte offsets. The
optional `modifiers` argument is the same flag string `redump` accepts (`u`, `v`, `i`, `m`,
`s`); the engines that do not implement `v` (unicode sets) fall back to the closest mode.

### Building

All three engines are enabled by default. Their sources are taken from the `.regex-bench`
work directory populated by `tools/regex-bench/run.arsh` (SRELL single-header, QuickJS and the
Hermes source/build trees), so a plain configure builds `rematch` with every engine:

```sh
cmake -S . -B build
cmake --build build --target rematch
```

The convenience script `build_rematch.arsh` performs the whole flow using the regex-bench
work directory as the source of the engines: it prepares (fetches and builds) the engines
first, then configures and builds `rematch`:

```sh
$ arsh test/regex/build_rematch.arsh
prepare engines: tools/regex-bench/run.arsh --prepare
...
built: <repo>/build-rematch/test/regex/rematch
```

| option             | description                                                     |
|--------------------|-----------------------------------------------------------------|
| `--build DIR`      | build directory (default: `build-rematch`)                      |
| `--build-type T`   | CMake build type (default: `Release`)                           |
| `--work DIR`       | regex-bench work directory (default: `.regex-bench`)            |
| `--no-prepare`     | skip the engine preparation step, reuse the current work tree   |
| `--srell-dir DIR`  | use an existing SRELL source directory                          |
| `--quickjs-dir DIR`| use an existing QuickJS source directory                        |
| `--hermes-dir DIR` | use an existing Hermes source directory                         |
| `--hermes-build-dir DIR` | use an existing Hermes build directory                    |

If an engine's sources are not found, that engine is skipped with a configuration warning. To
point at different locations, or to disable an engine, override the same cache variables used
by `tools/regex-bench`:

```sh
cmake -S . -B build \
      -DREMATCH_ENABLE_SRELL=ON    -DSRELL_SOURCE_DIR=<srell/single-header> \
      -DREMATCH_ENABLE_QUICKJS=ON  -DQUICKJS_SOURCE_DIR=<quickjs> \
      -DREMATCH_ENABLE_HERMES=ON   -DHERMES_SOURCE_DIR=<hermes> \
                                   -DHERMES_BUILD_DIR=<hermes-build>
cmake --build build --target rematch
```

When at least one engine is enabled, the `rematch_test` target is also built; it runs an
engine-independent case against every enabled engine.

### Examples

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
