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

## Testing

`test/regex/cases` holds the golden output tests (`redump`) and
`test/regex/test262regex` runs the ECMAScript Test262 regex cases.

The engine is additionally checked by a differential test against the regexp engine used by Node.js
(V8), which serves as the reference implementation of the ECMAScript RegExp semantics. It runs the
same patterns and subjects through both engines, only in the unicode (`u`) and unicode sets (`v`)
modes, and translates the UTF-16 offsets reported by JavaScript into the UTF-8 byte offsets arsh
reports. See `test/regex/difftest/README.md` for the corpus format and how cases that depend on the
Unicode data version or on newer syntax are handled.

```sh
$ ctest -R regex_difftest --output-on-failure
```
