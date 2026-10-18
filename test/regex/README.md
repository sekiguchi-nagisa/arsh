# regex test tools

## `redump`

`dump.cpp` builds the `redump` helper used by the end-to-end tests under `cases/`. It parses a
pattern with the arsh regex engine and dumps either the parsed tree (`-d`), the compiled
bytecode (default) or the first match (`-m`).

## `rematch`

The standalone `rematch` tool (a version of `redump -m` embedding the JavaScript-compatible
engines used by [`tools/regex-bench`](../../tools/regex-bench)) lives under `tools/regex-bench`
and is built with [`tools/regex-bench/build_rematch.arsh`](../../tools/regex-bench); see its
README for details.
