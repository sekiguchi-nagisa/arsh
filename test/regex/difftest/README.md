# regex differential test

Differential test of the arsh regex engine against the regexp engine used by Node.js (V8), which
is treated as the reference implementation of the ECMAScript RegExp semantics.

The oracle is a reference, not an authority: where V8 itself departs from ECMA-262 the divergence is
recorded as a known bug instead of being copied, see
[When the oracle is wrong](#when-the-oracle-is-wrong).

```sh
$ ctest -R regex_difftest --output-on-failure
```

The test is only built when a `node` executable is found, and when it is new enough for the
corpus. Pass `-DNODE_PROGRAM=<path to node>` to choose a specific one; without Node.js there is no
oracle, so the target is skipped at configure time with a warning. A `node` older than
`NODE_MIN_VERSION` is treated the same way, so an old interpreter lying around on `PATH` cannot
silently turn the run into a no-op.

## Node.js version

The corpus uses `v` mode set operations, inline modifiers and properties of strings, so a Node.js
that does not implement them cannot serve as the oracle. The minimum version is therefore a
configure-time setting:

| setting            | default | meaning                                                        |
|--------------------|---------|----------------------------------------------------------------|
| `NODE_MIN_VERSION` | `26`    | oldest acceptable Node.js (`major`, `major.minor` or full version) |
| `NODE_PROGRAM`     | (found on `PATH`) | the `node` executable used as the oracle             |

```sh
$ cmake -DNODE_PROGRAM=/path/to/node -DNODE_MIN_VERSION=26 ..
```

The check happens twice, because the two failure modes look different:

- **At configure time** `node --version` is run and compared with `NODE_MIN_VERSION`. A version
  that is too old is reported as a warning and the target is not created, so the build does not
  carry a test that can only skip.
- **At run time** the version from the oracle banner (`process.versions.node`) is compared with the
  same requirement again, and a `node` that is too old makes the oracle fail to start. This catches
  a binary that was swapped after configure time (a `PATH` change, a different `node` in front of
  the one that was found), which would otherwise skip every case and report a green run.

Only the numeric `major.minor.patch` is compared: the `v` prefix of `node --version` is accepted,
a pre-release / build suffix is ignored, and a missing component counts as `0` (`26` equals
`26.0.0`). A version string with no number at all is rejected instead of being read as `0.0.0`, so
a malformed banner never passes the check.

The skip is loud rather than silent: the reason is printed once as a warning, and every case is
reported as skipped with that reason.

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
| assertion + capture in a loop  | the loop's `ResetCaptures` and the assertion's `SetCapture` are interleaved on the same stack |
| backreference in a loop        | the capture value read on each iteration                       |
| multi code point literals      | the radix-tree instruction reads the state from `back()`       |
| look-behind containing a loop  | a loop run backwards                                           |
| astral characters              | code point vs. code unit iteration                             |

`RegexDiffCorpus.loopCoverage` asserts that this file keeps covering those groups, so trimming the
corpus back to a handful of loops fails the build.

## Deep nesting (`deep-nesting.jsonl`)

`cleanupLookAround` walks the backtrack stack and truncates it at the `LookAround` entry, so the
distance it has to walk and the number of entries it drops both grow with the nesting depth of the
pattern. The same is true for an assertion that sits inside many non-capturing groups, and for a
look-behind chain over astral text, where every level steps back one code point.

`deep-nesting.jsonl` therefore builds the same shape at several depths (2, 4, 8, 16, 32, 64) with
`(?=` / `(?<=` / `(?:` / `(` repetition, and adds a few wide patterns (30 capture groups, with an
assertion reaching the last one, and bounded repeats over them) so the stack is pushed both deep
and wide. Every case is still a plain comparison against the oracle, so a truncation that drops
either too much or too little shows up as a wrong capture.

`RegexDiffCorpus.featureCoverage` asserts that the depth ladders stay present in both
`deep-nesting.jsonl` and `loops.jsonl`.

## Features without a byte oriented counterpart

`lookaround`, `back reference` and `unicode sets` are the parts of the ECMAScript grammar that a
byte oriented engine has no equivalent for, and each of them has its own case file. The cases are
not just the happy path: they are written around the places where the implementation of those
features has to talk to the rest of the VM.

### Assertions (`assertion.jsonl`, `known-bugs.jsonl`)

`assertion.jsonl` covers the four assertion kinds, the nesting of one inside another, captures
taken inside an assertion, assertions inside a loop, and a look-behind over astral text (a
look-behind has to step back over a code point, not over a code unit).

The same cases turned up a divergence that is filed in `known-bugs.jsonl` rather than fixed here:
group 1 of

```json
{"pattern": "(?:(?=(a))x|(a))", "input": "a", "flags": "u"}
```

is reported as `[0,1)` where V8 reports it unset. The mechanism is described in
[Capture leak out of a lookaround](#capture-leak-out-of-a-lookaround).

### Capture leak out of a lookaround

The VM keeps the captures it has to restore on the same backtrack stack as the retry points, and an
assertion pushes a `LookAround` entry that `EndLookAround` is expected to pop together with
everything above it. `cleanupLookAround` does that for the negative case, but for a *successful*
assertion it also discards the `SetCapture` entries its body pushed. Those entries are the only
record of the capture values from before the assertion ran, so when an enclosing alternative is
abandoned afterwards, the backtrack pass restores the alternatives it recorded but not the capture
group the assertion had set: the value leaks into the final match even though the alternative that
produced it never participated.

Two things follow from that and both are pinned by the entries:

- the leak is invisible while nothing backtracks over it (`(?=(a))\1` is correct, because the
  capture is meant to survive), which is why it only shows up as a *wrong group value* on an
  otherwise successful match;
- a capture set inside `(?!...)` is a different case and is already reset immediately, so the
  entries for the negative assertions document correct behaviour and would only fail if that reset
  regressed.

The recorded entries cover the shapes the leak shows up in: the assertion in a plain alternation,
inside `?` / `*` / `+`, in a capturing group, side by side with a named group, with a second capture
after it, with a look-behind, and in a `v` / `vi` case.

### Back references (`backreference.jsonl`)

Besides the plain `(a)\1` shape, the file covers a reference to a group that has not participated
yet (which matches the empty string), a reference to a group skipped by its alternative, an
optional group, a group that matched the empty string, numeric indices above 9, named groups
(`\k<x>`), a reference inside a loop and inside an assertion, and back references whose comparison
depends on case folding (including `ſ`/`s`/`S` and the Kelvin sign).

### Unicode sets (`unicode-sets.jsonl`)

Set operations have the largest cross product, since `StrSetBuilder::intersect` / `sub` match an
emoji bitmask, a radix tree and a code point set against the same three kinds on the other side. The
file therefore covers nested classes (`[[a-z]--[aeiou]]`), chained operations, `\q{...}` class
strings (with escaped punctuation, empty alternatives, astral text and quantifiers), properties of
strings (`\p{RGI_Emoji}`, `Basic_Emoji`, the flag/keycap/modifier/tag sequences), negation, and the
interaction of `i` with both operators. The cases that ECMA-262 rejects but a naive parser accepts
(`[^...]` containing a string, a complement of a property of strings, the reserved double
punctuators) are in `invalid.jsonl` next to the other grammar errors.

`RegexDiffCorpus.featureCoverage` asserts that these files keep covering the groups above, in the
same spirit as `loopCoverage`.
