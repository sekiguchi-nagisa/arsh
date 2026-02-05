# supported regular expression features (ECMAScript 2025)

| **feature**            | **syntax** | **match** | **match (ignore case)** |
|------------------------|------------|-----------|-------------------------|
| \1                     | ❌          | ❌         | ❌                       |
| (pattern)              | ❌          | ❌         | ❌                       |
| \d                     | ✔️         | ❌         | ❌                       |
| \D                     | ✔️         | ❌         | ❌                       |
| \w                     | ✔️         | ❌         | ❌                       |
| \W                     | ✔️         | ❌         | ❌                       |
| \s                     | ✔️         | ❌         | ❌                       |
| []                     | ❌          | ❌         | ❌                       |
| [abc]                  | ❌          | ❌         | ❌                       |
| [A-Z]                  | ❌          | ❌         | ❌                       |
| [^]                    | ❌          | ❌         | ❌                       |
| [^abc]                 | ❌          | ❌         | ❌                       |
| [^A-Z]                 | ❌          | ❌         | ❌                       |
| [operand1&&operand2]   | ❌          | ❌         | ❌                       |
| [operand1--operand2]   | ❌          | ❌         | ❌                       |
| [\q{substring}]        | ❌          | ❌         | ❌                       |
| \f                     | ✔️         | ❌         | ❌                       |
| \n                     | ✔️         | ❌         | ❌                       |
| \r                     | ✔️         | ❌         | ❌                       |
| \t                     | ✔️         | ❌         | ❌                       |
| \v                     | ✔️         | ❌         | ❌                       |
| \cA                    | ✔️         | ❌         | ❌                       |
| \0                     | ❌          | ❌         | ❌                       |
| \xHH                   | ✔️         | ❌         | ❌                       |
| \uHHHH                 | ✔️         | ❌         | ❌                       |
| \u{HHH}                | ✔️         | ❌         | ❌                       |
| syntax escape (\^, ..) | ❌          | ❌         | ❌                       |
| pattern1\|pattern2     | ❌          | ❌         | ❌                       |
| ^                      | ✔️         | ❌         | ❌                       |
| $                      | ✔️         | ❌         | ❌                       |
| abc                    | ✔️         | ❌         | ❌                       |
| (?=pattern)            | ❌          | ❌         | ❌                       |
| (?!pattern)            | ❌          | ❌         | ❌                       |
| (?<=pattern)           | ❌          | ❌         | ❌                       |
| (?<!pattern)           | ❌          | ❌         | ❌                       |
| (?ims-ims:pattern)     | ❌          | ❌         | ❌                       |
| \k<name>               | ✔️         | ❌         | ❌                       |
| (?<name>pattern)       | ❌          | ❌         | ❌                       |
| (?:pattern)            | ❌          | ❌         | ❌                       |
| ?                      | ❌          | ❌         | ❌                       |
| *                      | ❌          | ❌         | ❌                       |
| +                      | ❌          | ❌         | ❌                       |
| {count}                | ❌          | ❌         | ❌                       |
| {min,}                 | ❌          | ❌         | ❌                       |
| {min,max}              | ❌          | ❌         | ❌                       |
| ??                     | ❌          | ❌         | ❌                       |
| *?                     | ❌          | ❌         | ❌                       |
| +?                     | ❌          | ❌         | ❌                       |
| {count}?               | ❌          | ❌         | ❌                       |
| {min,}?                | ❌          | ❌         | ❌                       |
| {min,max}?             | ❌          | ❌         | ❌                       |
| \p{loneProperty}       | ✔️         | ❌         | ❌                       |
| \P{loneProperty}       | ✔️         | ❌         | ❌                       |
| \p{property=value}     | ✔️         | ❌         | ❌                       |
| \P{property=value}     | ✔️         | ❌         | ❌                       |
| \p{RGI_Emoji}          | ✔️         | ❌         | ❌                       |
| \P{RGI_Emoji}          | ✔️         | ❌         | ❌                       |
| .                      | ✔️         | ❌         | ❌                       |
| \b                     | ✔️         | ❌         | ❌                       |
| \B                     | ✔️         | ❌         | ❌                       |
| \X (extension)         | ❌          | ❌         | ❌                       |