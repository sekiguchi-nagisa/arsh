# supported regular expression features (ECMAScript 2025)

| **feature**                         | **syntax** | **match** | **match (ignore case)** | **match (within look-behind)** |
|-------------------------------------|------------|-----------|-------------------------|--------------------------------|
| \1                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| (pattern)                           | ✔️         | ✔️        | ✔️                      | ✔️                             |
| (pattern) (with quantifier)         | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \d                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \D                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \w                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \W                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \s                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \S                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| []                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [abc]                               | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [A-Z]                               | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [^]                                 | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [^abc]                              | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [^A-Z]                              | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [operand1&&operand2]                | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [operand1&&operand2] (with string)  | ✔️         | ✔️        | ✔️                      | ️✔️                            |
| [operand1--operand2]                | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [operand1--operand2] (with string)  | ✔️         | ✔️        | ✔️                      | ️✔️                            |
| [^operand1&&operand2]               | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [^operand1&&operand2] (with string) | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [^operand1--operand2]               | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [^operand1--operand2] (with string) | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [\q{substring}]                     | ✔️         | ✔️        | ✔️                      | ✔️                             |
| [\q{substring}] (char only)         | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \f                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \n                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \r                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \t                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \v                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \cA                                 | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \0                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \xHH                                | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \uHHHH                              | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \u{HHH}                             | ✔️         | ✔️        | ✔️                      | ✔️                             |
| syntax escape                       | ✔️         | ✔️        | ✔️                      | ✔️                             |
| pattern1\|pattern2                  | ✔️         | ✔️        | -                       | -                              |
| ^                                   | ✔️         | ✔️        | ✔️                      | ✔️                             |
| ^ (multiline)                       | ✔️         | ✔️        | ✔️                      | ✔️                             |
| $                                   | ✔️         | ✔️        | ✔️                      | ✔️                             |
| $ (multiline)                       | ✔️         | ✔️        | ✔️                      | ✔️                             |
| abc                                 | ✔️         | ✔️        | ✔️                      | ✔️                             |
| (?=pattern)                         | ✔️         | ✔️        | -                       | ✔️                             |
| (?!pattern)                         | ✔️         | ✔️        | -                       | ✔️                             |
| (?\<=pattern)                       | ✔️         | ✔️        | -                       | ✔️                             |
| (?\<!pattern)                       | ✔️         | ✔️        | -                       | ✔️                             |
| (?ims-ims:pattern)                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \k\<name>                           | ✔️         | ✔️        | ✔️                      | ✔️                             |
| (?\<name>pattern)                   | ✔️         | ✔️        | ✔️                      | ✔️                             |
| (?:pattern)                         | ✔️         | ✔️        | ✔️                      | ✔️                             |
| ?                                   | ✔️         | ✔️        | -                       | ✔️                             |
| *                                   | ✔️         | ✔️        | -                       | ✔️                             |
| +                                   | ✔️         | ✔️        | -                       | ✔️                             |
| {count}                             | ✔️         | ✔️        | -                       | ✔️                             |
| {min,}                              | ✔️         | ✔️        | -                       | ✔️                             |
| {min,max}                           | ✔️         | ✔️        | -                       | ✔️                             |
| ??                                  | ✔️         | ✔️        | -                       | ✔️                             |
| *?                                  | ✔️         | ✔️        | -                       | ✔️                             |
| +?                                  | ✔️         | ✔️        | -                       | ✔️                             |
| {count}?                            | ✔️         | ✔️        | -                       | ✔️                             |
| {min,}?                             | ✔️         | ✔️        | -                       | ✔️                             |
| {min,max}?                          | ✔️         | ✔️        | -                       | ✔️                             |
| \p{loneProperty}                    | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \P{loneProperty}                    | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \p{property=value}                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \P{property=value}                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \p{RGI_Emoji}                       | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \p{RGI_Emoji} (in char class)       | ✔️         | ✔️        | ✔️                      | ✔️                             |
| .                                   | ✔️         | ✔️        | ✔️                      | ✔️                             |
| . (dot-all)                         | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \b                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \B                                  | ✔️         | ✔️        | ✔️                      | ✔️                             |
| \X (extension)                      | ✔️         | ✔️        | ✔️                      | ✔️                             |

## regex JIT compiler (experimental)

On Linux x86-64 with a compiler that supports `preserve_none` and `musttail` (Clang, GCC 14+), an
experimental copy-and-patch JIT compiler is built in. It is **off by default** and is enabled at run
time via the `ARSH_REGEX_JIT` environment variable:

```bash
ARSH_REGEX_JIT=1 ./arsh script.arsh
```

When the variable is unset (or the JIT is not built in), the interpreter is used, and the results
are identical: the JIT is validated against the interpreter over the whole opcode set.

### how it works

`tools/gen_jit_stencil` extracts one native "stencil" per regex opcode from a translation unit
compiled with `-fno-pic -fno-pie -mcmodel=large`. Each stencil has the signature
`int32_t(JitContext &, const Inst *) noexcept` and is called with the `preserve_none` convention, so
"run the next instruction" becomes a bare tail `jmp` instead of a call. A stencil reads its operands
from the bytecode it was copied from; only the control flow edges are patched into the machine code.
For branch targets that are not the successor, a trampoline resolves the target through the
instruction offset table, which keeps loop back edges stack-flat.

Backtracking does not return to the driver either: a failing stencil tail-calls a second trampoline
which runs the backtrack stack and jumps straight to the resolved stencil. Because both edges are
tail calls, a deep backtracking run stays stack-flat too, and the driver is only re-entered when the
whole search attempt is exhausted (or the timer fires).

The generated code is cached on the `Regex` object and reused across matches.

### status

The implementation is functionally complete and byte-for-byte equivalent to the interpreter on the
test corpus. It is **not yet faster** than the interpreter: the interpreter already keeps the
bytecode pointer, the input, and the captures in registers across a compiled switch dispatch, so the
copy-and-patch call overhead currently offsets the benefit of removing the dispatch. The remaining
work is in the code layout (avoid re-loading the `JitContext` fields on every block, and inline the
frequent helpers) rather than in semantics, so the feature is documented as experimental.
