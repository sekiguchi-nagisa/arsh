# regex differential test

Differential test of the arsh regex engine against the regexp engine used by Node.js (V8), which
is treated as the reference implementation of the ECMAScript RegExp semantics.

The oracle is a reference, not an authority: where V8 itself departs from ECMA-262 the divergence is
recorded as a known bug instead of being copied, see
[When the oracle is wrong](#when-the-oracle-is-wrong).

```sh
$ ctest -R regex_difftest --output-on-failure
```

The test is only built when a `node` executable is found. Pass `-DNODE_PROGRAM=<path to node>` to
choose a specific one; without Node.js there is no oracle, so the target is skipped at configure
time with a warning.

## How it works

Each case is compiled and matched twice: once by arsh (in process, through `runArsh`) and once by
the Node.js oracle (`oracle.js`). A single `node` process is started for the whole run and answers
one JSON line per case, which keeps a full corpus run in about a second.

`oracle.js` adds the `d` (hasIndices) flag to every pattern, so `RegExp.prototype.exec` reports the
capture offsets as well as the matched text.

### Modes

Both engines are only driven in the **unicode** (`u`) and **unicode sets** (`v`) modes. arsh matches
its UTF-8 input by code point, and only those modes define the same code point semantics in
JavaScript; the byte oriented `BMP` mode has no JavaScript equivalent. A case whose `flags` do not
select one of the two modes fails the `RegexDiffCorpus.unicodeModeOnly` test instead of being
compared.

### Offsets

arsh reports capture offsets as **UTF-8 byte** offsets, while JavaScript reports **UTF-16 code
unit** indices, so the oracle side is translated by `Utf16Index` before the results are compared. A
supplementary code point is one code point (4 UTF-8 bytes) for arsh but two code units for
JavaScript. A reported boundary that falls inside a surrogate pair cannot be translated and is
reported as an error rather than rounded, since no code point boundary can be there in these modes.

### Unicode version

`\p{Emoji}`, `\p{Assigned}` and the other properties whose tables grow with every Unicode release
may legitimately differ between the arsh build and the Node.js build. A case whose outcome depends
on that data is marked `"unicode": "versioned"` and is skipped unless both engines bundle the same
Unicode version. The skip message prints both versions, so it is clear why cases were skipped:

```
Unicode version dependent case (arsh=18.0.0, node=17.0)
```

Every other case is version independent and is always compared.

### Newer syntax

Syntax that older Node.js releases do not understand (`v` mode set operations, inline modifiers,
properties of strings) is skipped rather than failed: a case lists what it needs in `requires`, and
the oracle reports the features it supports in its opening banner. This keeps the corpus runnable
on older Node.js builds while still exercising the feature where it exists.

## Corpus

The cases live in `cases/*.jsonl`, one JSON object per line:

```json
{"pattern": "[\\p{L}--[a-z]]", "input": "abc", "flags": "v", "requires": ["unicode-sets"], "unicode": "stable"}
```

| field      | required | meaning                                                                    |
|------------|----------|----------------------------------------------------------------------------|
| `pattern`  | yes      | UTF-8 pattern                                                              |
| `input`    | yes      | UTF-8 subject                                                              |
| `flags`    | no       | `regex::Flag` flag string, default `u`                                     |
| `requires` | no       | oracle features the case needs (`unicode-sets`, `modifiers`, `property-of-strings`) |
| `unicode`  | no       | `stable` (default) or `versioned` (skipped on a Unicode version mismatch)   |
| `xfail`    | no       | the case documents a known divergence, see below                           |

Because the arsh side runs in process, a case may contain a NUL byte in the pattern or the input.

## When the oracle is wrong

V8 is the reference implementation in practice, but it is not normative. ECMA-262 is, and where the
two disagree the arsh behaviour is kept and the divergence is recorded instead of being adopted.
This matters most for `i` (caseless) combined with `v` mode set operations, where
`MaybeSimpleCaseFolding` (§22.2.2.9.5) folds the *pattern* side and `Canonicalize` (§22.2.2.7.1)
canonicalizes the *input* side.

A concrete case is `[a--\q{a}]` against `"A"` with the `vi` flags. The left and right operand denote
the identical CharSet `{"a"}`, so the difference is the empty set and the pattern cannot match
anything. V8 reports a match, which contradicts the identity `A -- A = ∅`; arsh and JavaScriptCore
both reject it. A second family involves `\p{...}` under `i`: V8 treats `\p{ASCII}` (already fold
closed) differently from the equivalent `[\x00-\x7F]` (fold applied), so two spellings of the same
CharSet disagree with each other.

`cases/known-bugs.jsonl` pins such a case down in both directions: `xfail: true` marks it, and
`xfailArsh` / `xfailOracle` hold the exact `MatchResult::toCompactString` each side produced when the
divergence was filed. The case then fails both when arsh starts to agree (the marker should be
dropped) and when a recorded result changes (the bug drifted), so it stays useful in both directions
without breaking the build:

| field         | meaning                                                                   |
|---------------|---------------------------------------------------------------------------|
| `xfail`       | the case is a known divergence; it must keep diverging exactly as recorded |
| `xfailArsh`   | the recorded arsh result, e.g. `nomatch` or `match [0,3)`                 |
| `xfailOracle` | the recorded oracle result                                                |

When filing a new entry, put a placeholder in both fields, run the test, and copy the
`recorded=... actual=...` values from the failure message. Do not add an `xfail` entry for a case
where arsh is the one that is wrong — that is an ordinary failing case and should be fixed.

### Caseless set operations

`i` combined with `&&` / `--` is the part of the set-operation lowering with the most cross product:
`StrSetBuilder::intersect` and `StrSetBuilder::sub` each match an emoji bitmask, a radix tree and a
code point set against the same three kinds on the other side, and `RGIEmojiSeq::CASE_IGNORE`
reaches all of those paths. `unicode-sets.jsonl` therefore carries `vi` cases covering each operand
kind (single code points, ranges, `\p{...}`, `\P{...}`, `\q{...}` string literals, properties of
strings, nested classes) on both sides of `&&` and `--`.

## Loop cases (`loops.jsonl`)

The backtrack stack saves and restores a loop's `LoopState` on every iteration and every retry, and
the entries for loops, captures and lookarounds are interleaved on the same stack. Changes in that
area (merging a loop's exit entry with its state entry, reusing the entry in place, or skipping the
save on the body) have repeatedly broken backtracking, so the loops are covered explicitly:

| group                          | what it exercises                                              |
|--------------------------------|----------------------------------------------------------------|
| nested loops                   | each nesting level restores its own `LoopState`                |
| deep nesting (built by `?:`/`(` repetition) | stack depth and restore order                     |
| greedy / lazy combinations     | the lazy path pops the saved `SetIns` and re-enters the body   |
| empty-matching bodies          | `(?:)*`, `(?:a?)*`, `(?:\|a)*`: iteration must not loop forever  |
| alternation preference         | the order in which alternatives are retried inside a loop      |
| bounded repeats                | `{n}`, `{n,m}` nested in and next to another loop              |
| captures across iterations     | a group must be reset when a later iteration does not set it   |
| lookaround in a loop           | `cleanupLookAround` pops entries the loop pushed               |
| backreference in a loop        | the capture value read on each iteration                       |
| multi code point literals      | the radix-tree instruction reads the state from `back()`       |
| look-behind containing a loop  | a loop run backwards                                           |
| astral characters              | code point vs. code unit iteration                             |

`RegexDiffCorpus.loopCoverage` asserts that this file keeps covering those groups, so trimming the
corpus back to a handful of loops fails the build.
