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

function abs(): Int for Int

function %OP_TO_FLOAT(): Float for Int
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

function count(): Int for String

function chars(): [String] for String

function words(): [String] for String

function width(eaw: Int?): Int for String

function %OP_GET(index: Int): String for String

function charAt(index: Int): String for String

function slice(start: Int, stop: Int?): String for String

function startsWith(target: String): Bool for String

function endsWith(target: String): Bool for String

function indexOf(target: String, index: Int?): Int for String

function lastIndexOf(target: String): Int for String

function contains(target: String): Bool for String

function split(delim: String): [String] for String

function replace(target: String, rep: String): String for String

function sanitize(repl: String?): String for String

function toInt(radix: Int?): Int? for String

function toFloat(): Float? for String

function %OP_ITER(): StringIter for String

function %OP_MATCH(re: Regex): Bool for String

function %OP_UNMATCH(re: Regex): Bool for String

function realpath(): String for String

function basename(): String for String

function dirname(): String for String

function lower(): String for String

function upper(): String for String

function quote(): String for String
```

## FD type
```
function %OP_INIT(path: String): FD for FD

function close(): Void for FD

function dup(): FD for FD

function value(): Int for FD

function lock(): FD for FD

function unlock(): FD for FD

function %OP_BOOL(): Bool for FD

function %OP_NOT(): Bool for FD

function %OP_ITER(): Reader for FD
```

## Error type
```
function %OP_INIT(message: String, status: Int?): Error for Error

function message(): String for Error

function show(): Void for Error

function name(): String for Error

function status(): Int for Error

function lineno(): Int for Error

function source(): String for Error
```

## Job type
```
function in(): FD for Job

function out(): FD for Job

function %OP_GET(index: Int): FD for Job

function poll(): Bool for Job

function wait(): Int for Job

function raise(s: Signal): Void for Job

function detach(): Void for Job

function size(): Int for Job

function pid(index: Int): Int? for Job

function status(index: Int): Int? for Job
```

## StringIter type
```
function %OP_NEXT(): String for StringIter
```

## Regex type
```
function %OP_INIT(str: String, flag: String?): Regex for Regex

function isCaseless(): Bool for Regex

function isMultiLine(): Bool for Regex

function isDotAll(): Bool for Regex

function %OP_MATCH(target: String): Bool for Regex

function %OP_UNMATCH(target: String): Bool for Regex

function match(target: String): [String?] for Regex

function replace(target: String, repl: String): String for Regex
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
function %OP_INIT(): LineEditor for LineEditor

function readLine(p: String?): String? for LineEditor

function setCompletion(comp: ((Module, String) -> [String])?): Void for LineEditor

function setPrompt(prompt: ((String) -> String)?): Void for LineEditor

function setHistory(hist: [String]?): Void for LineEditor

function setHistSync(sync: ((String, [String]) -> Void)?): Void for LineEditor

function bind(key: String, action: String): Void for LineEditor

function bindings(): [String : String] for LineEditor

function action(name: String, type: String, action: (String, [String]?) -> String?): Void for LineEditor

function actions(): [String] for LineEditor

function config(name: String, value: Any): Void for LineEditor

function configs(): [String : Any] for LineEditor
```

## CLI type
```
function name(): String for CLI

function setName(arg0: String): Void for CLI

function parse(args: [String]): Int for CLI

function parseOrExit(args: [String]): Int for CLI

function usage(message: String?, verbose: Bool?): String for CLI
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

function addAll(value: [T0]): [T0] for [T0]

function swap(index: Int, value: T0): T0 for [T0]

function slice(from: Int, to: Int?): [T0] for [T0]

function copy(): [T0] for [T0]

function reverse(): [T0] for [T0]

function sort(): [T0] where T0 : Value_ for [T0]

function sortWith(comp: (T0, T0) -> Bool): [T0] for [T0]

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

function addAll(value: [T0 : T1]): [T0 : T1] for [T0 : T1]

function copy(): [T0 : T1] for [T0 : T1]

function clear(): Void for [T0 : T1]

function %OP_ITER(): [T0 : T1] for [T0 : T1]

function %OP_NEXT(): (T0, T1) for [T0 : T1]
```

