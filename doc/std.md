# Standard Library Interface Definition
## Any type
```
function %OP_STR(): String for Any

function %OP_INTERP(): String for Any
```

## Int type
```
function %OP_PLUS(): Int for Int

function %OP_MINUS(): Int for Int

function %OP_NOT(): Int for Int

function %OP_ADD(target: Int): Int for Int

function %OP_SUB(target: Int): Int for Int

function %OP_MUL(target: Int): Int for Int

function %OP_DIV(target: Int): Int for Int

function %OP_MOD(target: Int): Int for Int

function %OP_EQ(target: Int): Bool for Int

function %OP_NE(target: Int): Bool for Int

function %OP_LT(target: Int): Bool for Int

function %OP_GT(target: Int): Bool for Int

function %OP_LE(target: Int): Bool for Int

function %OP_GE(target: Int): Bool for Int

function %OP_AND(target: Int): Int for Int

function %OP_OR(target: Int): Int for Int

function %OP_XOR(target: Int): Int for Int

function %OP_LSHIFT(target: Int): Int for Int

function %OP_RSHIFT(target: Int): Int for Int

function %OP_URSHIFT(target: Int): Int for Int

function abs(): Int for Int

function %OP_TO_FLOAT(): Float for Int

function compare(target: Int): Int for Int
```

## Float type
```
function %OP_PLUS(): Float for Float

function %OP_MINUS(): Float for Float

function %OP_ADD(target: Float): Float for Float

function %OP_SUB(target: Float): Float for Float

function %OP_MUL(target: Float): Float for Float

function %OP_DIV(target: Float): Float for Float

function %OP_EQ(target: Float): Bool for Float

function %OP_NE(target: Float): Bool for Float

function %OP_LT(target: Float): Bool for Float

function %OP_GT(target: Float): Bool for Float

function %OP_LE(target: Float): Bool for Float

function %OP_GE(target: Float): Bool for Float

function isNaN(): Bool for Float

function isInf(): Bool for Float

function isFinite(): Bool for Float

function isNormal(): Bool for Float

function round(): Float for Float

function trunc(): Float for Float

function floor(): Float for Float

function ceil(): Float for Float

function abs(): Float for Float

function %OP_TO_INT(): Int for Float

function compare(target: Float): Int for Float
```

## Bool type
```
function %OP_NOT(): Bool for Bool

function %OP_EQ(target: Bool): Bool for Bool

function %OP_NE(target: Bool): Bool for Bool

function compare(target: Bool): Int for Bool
```

## String type
```
function %OP_EQ(target: String): Bool for String

function %OP_NE(target: String): Bool for String

function %OP_LT(target: String): Bool for String

function %OP_GT(target: String): Bool for String

function %OP_LE(target: String): Bool for String

function %OP_GE(target: String): Bool for String

function size(): Int for String

function empty(): Bool for String

function ifEmpty(default: String?): String? for String

function count(): Int for String

function chars(): [String] for String

function words(): [String] for String

function width(eaw: Int?): Int for String

function %OP_GET(index: Int): String for String

function charAt(index: Int): String for String

function slice(start: Int, end: Int?): String for String

function startsWith(target: String): Bool for String

function endsWith(target: String): Bool for String

function indexOf(target: String, index: Int?): Int for String

function lastIndexOf(target: String): Int for String

function contains(target: String): Bool for String

function split(delim: String): [String] for String

function replace(target: String, rep: String, once: Bool?): String for String

function validate(): Bool for String

function sanitize(repl: String?): String for String

function toInt(radix: Int?): Int? for String

function toFloat(): Float? for String

function compare(target: String): Int for String

function %OP_ITER(): StringIter for String

function %OP_MATCH(re: Regex): Bool for String

function %OP_UNMATCH(re: Regex): Bool for String

function realpath(): String for String

function basename(): String for String

function dirname(): String for String

function lower(): String for String

function upper(): String for String

function foldCase(full: Bool?, turkic: Bool?): String for String

function quote(): String for String
```

## FD type
```
type FD(path: String)

function close(): Void for FD

function dup(): FD for FD

function value(): Int for FD

function lock(): FD for FD

function unlock(): FD for FD

function cloexec(set: Bool?): Void for FD

function %OP_BOOL(): Bool for FD

function %OP_NOT(): Bool for FD

function %OP_ITER(): Reader for FD
```

## ProcSubst type
```
function job(): Job for ProcSubst
```

## Throwable type
```
function message(): String for Throwable

function show(): Void for Throwable

function name(): String for Throwable

function status(): Int for Throwable

function lineno(): Int for Throwable

function source(): String for Throwable

function suppressed(): [Throwable] for Throwable
```

## Error type
```
type Error(message: String, status: Int?)
```

## Job type
```
function in(): FD for Job

function out(): FD for Job

function %OP_GET(index: Int): FD for Job

function poll(): Bool for Job

function wait(): Int for Job

function kill(signal: Signal): Void for Job

function disown(): Void for Job

function size(): Int for Job

function pid(index: Int): Int? for Job

function status(index: Int): Int? for Job
```

## Jobs type
```
function %OP_GET(key: String): Job for Jobs

function get(key: String): Job? for Jobs

function count(): Int for Jobs
```

## StringIter type
```
function %OP_NEXT(): String for StringIter
```

## Regex type
```
type Regex(str: String, flag: String?)

function isCaseless(): Bool for Regex

function isMultiLine(): Bool for Regex

function isDotAll(): Bool for Regex

function %OP_MATCH(target: String): Bool for Regex

function %OP_UNMATCH(target: String): Bool for Regex

function match(target: String): RegexMatch? for Regex

function replace(target: String, repl: String, once: Bool?): String for Regex
```

## RegexMatch type
```
function start(): Int for RegexMatch

function end(): Int for RegexMatch

function count(): Int for RegexMatch

function group(index: Int): String? for RegexMatch

function named(name: String): String? for RegexMatch

function names(): [String] for RegexMatch
```

## Signal type
```
function name(): String for Signal

function value(): Int for Signal

function message(): String for Signal

function kill(pid: Int): Void for Signal

function trap(handler: ((Signal) -> Void)?): (Signal) -> Void for Signal

function %OP_EQ(target: Signal): Bool for Signal

function %OP_NE(target: Signal): Bool for Signal

function compare(target: Signal): Int for Signal
```

## Signals type
```
function %OP_GET(key: String): Signal for Signals

function get(key: String): Signal? for Signals

function list(): [Signal] for Signals
```

## Module type
```
function _scriptName(): String for Module

function _scriptDir(): String for Module

function _func(expr: String): () -> Any? for Module

function _fullname(name: String): String? for Module
```

## Reader type
```
function %OP_NEXT(): String for Reader
```

## Command type
```
function call(argv: [String]): Bool for Command
```

## LineEditor type
```
type LineEditor()

function readLine(p: String?): String? for LineEditor

function setCompletion(comp: ((Module, String) -> Candidates)?): Void for LineEditor

function setPrompt(prompt: ((String) -> String)?): Void for LineEditor

function setHistory(hist: [String]?): Void for LineEditor

function setHistSync(sync: ((String, [String]) -> Void)?): Void for LineEditor

function bind(key: String, action: String?): Void for LineEditor

function bindings(): [String : String] for LineEditor

function action(name: String, type: String, action: ((String, [String]?) -> String?)?): Void for LineEditor

function actions(): [String] for LineEditor

function config(name: String, value: Any): Void for LineEditor

function configs(): [String : Any] for LineEditor
```

## CLI type
```
function name(): String for CLI

function setName(arg0: String): Void for CLI

function parse(args: [String]): Int for CLI

function usage(message: String?, verbose: Bool?): String for CLI
```

## Candidates type
```
type Candidates(values: [String]?)

function size(): Int for Candidates

function %OP_GET(index: Int): String for Candidates

function hasSpace(index: Int): Bool for Candidates

function add(can: String, desc: String?, space: Int?): Candidates for Candidates

function addAll(other: Candidates): Candidates for Candidates
```

## Array type
```
function %OP_GET(index: Int): T0 for [T0]

function get(index: Int): T0? for [T0]

function %OP_SET(index: Int, value: T0): Void for [T0]

function remove(index: Int): T0 for [T0]

function removeRange(from: Int, to: Int?): Void for [T0]

function peek(): T0 for [T0]

function push(value: T0): Void for [T0]

function pop(): T0 for [T0]

function shift(): T0 for [T0]

function unshift(value: T0): Void for [T0]

function insert(index: Int, value: T0): Void for [T0]

function add(value: T0): [T0] for [T0]

function addAll(other: [T0]): [T0] for [T0]

function swap(index: Int, value: T0): T0 for [T0]

function slice(start: Int, end: Int?): [T0] for [T0]

function copy(): [T0] for [T0]

function reverse(): [T0] for [T0]

function sort(): [T0] where T0 : Value_ for [T0]

function sortBy(comp: (T0, T0) -> Int): [T0] for [T0]

function searchSorted(target: T0): Int for [T0]

function searchSortedBy(target: T0, comp: (T0, T0) -> Int): Int for [T0]

function join(delim: String?): String for [T0]

function indexOf(target: T0, index: Int?): Int where T0 : Value_ for [T0]

function lastIndexOf(target: T0): Int where T0 : Value_ for [T0]

function contains(target: T0): Bool where T0 : Value_ for [T0]

function trap(handler: (Signal) -> Void): Void where T0 : Signal for [T0]

function size(): Int for [T0]

function empty(): Bool for [T0]

function clear(): Void for [T0]

function %OP_ITER(): [T0] for [T0]

function %OP_NEXT(): T0 for [T0]
```

## Map type
```
function %OP_GET(key: T0): T1 for [T0 : T1]

function %OP_SET(key: T0, value: T1): Void for [T0 : T1]

function put(key: T0, value: T1): T1? for [T0 : T1]

function putIfAbsent(key: T0, value: T1): T1 for [T0 : T1]

function size(): Int for [T0 : T1]

function empty(): Bool for [T0 : T1]

function get(key: T0): T1? for [T0 : T1]

function remove(key: T0): T1? for [T0 : T1]

function swap(key: T0, value: T1): T1 for [T0 : T1]

function addAll(other: [T0 : T1]): [T0 : T1] for [T0 : T1]

function copy(): [T0 : T1] for [T0 : T1]

function clear(): Void for [T0 : T1]

function %OP_ITER(): [T0 : T1] for [T0 : T1]

function %OP_NEXT(): (T0, T1) for [T0 : T1]
```

