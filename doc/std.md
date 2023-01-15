# Standard Library Interface Definition
## Any type
```
function %OP_STR() : String for Any

function %OP_INTERP() : String for Any
```

## Int type
```
function %OP_PLUS() : Int for Int

function %OP_MINUS() : Int for Int

function %OP_NOT() : Int for Int

function %OP_ADD($target : Int) : Int for Int

function %OP_SUB($target : Int) : Int for Int

function %OP_MUL($target : Int) : Int for Int

function %OP_DIV($target : Int) : Int for Int

function %OP_MOD($target : Int) : Int for Int

function %OP_EQ($target : Int) : Boolean for Int

function %OP_NE($target : Int) : Boolean for Int

function %OP_LT($target : Int) : Boolean for Int

function %OP_GT($target : Int) : Boolean for Int

function %OP_LE($target : Int) : Boolean for Int

function %OP_GE($target : Int) : Boolean for Int

function %OP_AND($target : Int) : Int for Int

function %OP_OR($target : Int) : Int for Int

function %OP_XOR($target : Int) : Int for Int

function abs() : Int for Int

function %OP_TO_FLOAT() : Float for Int
```

## Float type
```
function %OP_PLUS() : Float for Float

function %OP_MINUS() : Float for Float

function %OP_ADD($target : Float) : Float for Float

function %OP_SUB($target : Float) : Float for Float

function %OP_MUL($target : Float) : Float for Float

function %OP_DIV($target : Float) : Float for Float

function %OP_EQ($target : Float) : Boolean for Float

function %OP_NE($target : Float) : Boolean for Float

function %OP_LT($target : Float) : Boolean for Float

function %OP_GT($target : Float) : Boolean for Float

function %OP_LE($target : Float) : Boolean for Float

function %OP_GE($target : Float) : Boolean for Float

function isNaN() : Boolean for Float

function isInf() : Boolean for Float

function isFinite() : Boolean for Float

function isNormal() : Boolean for Float

function round() : Float for Float

function trunc() : Float for Float

function floor() : Float for Float

function ceil() : Float for Float

function abs() : Float for Float

function %OP_TO_INT() : Int for Float

function compare($target : Float) : Int for Float
```

## Boolean type
```
function %OP_NOT() : Boolean for Boolean

function %OP_EQ($target : Boolean) : Boolean for Boolean

function %OP_NE($target : Boolean) : Boolean for Boolean
```

## String type
```
function %OP_EQ($target : String) : Boolean for String

function %OP_NE($target : String) : Boolean for String

function %OP_LT($target : String) : Boolean for String

function %OP_GT($target : String) : Boolean for String

function %OP_LE($target : String) : Boolean for String

function %OP_GE($target : String) : Boolean for String

function size() : Int for String

function empty() : Boolean for String

function count() : Int for String

function chars() : Array<String> for String

function words() : Array<String> for String

function width($eaw : Option<Int>) : Int for String

function %OP_GET($index : Int) : String for String

function charAt($index : Int) : String for String

function slice($start : Int, $stop : Option<Int>) : String for String

function startsWith($target : String) : Boolean for String

function endsWith($target : String) : Boolean for String

function indexOf($target : String, $index : Option<Int>) : Int for String

function lastIndexOf($target : String) : Int for String

function contains($target : String) : Boolean for String

function split($delim : String) : Array<String> for String

function replace($target : String, $rep : String) : String for String

function sanitize($repl : Option<String>) : String for String

function toInt($radix : Option<Int>) : Option<Int> for String

function toFloat() : Option<Float> for String

function %OP_ITER() : StringIter for String

function %OP_MATCH($re : Regex) : Boolean for String

function %OP_UNMATCH($re : Regex) : Boolean for String

function realpath() : Option<String> for String

function lower() : String for String

function upper() : String for String
```

## UnixFD type
```
function %OP_INIT($path : String) : UnixFD for UnixFD

function close() : Void for UnixFD

function dup() : UnixFD for UnixFD

function value() : Int for UnixFD

function lock() : Void for UnixFD

function unlock() : Void for UnixFD

function %OP_BOOL() : Boolean for UnixFD

function %OP_NOT() : Boolean for UnixFD

function %OP_ITER() : Reader for UnixFD
```

## Error type
```
function %OP_INIT($message : String, $status : Option<Int>) : Error for Error

function message() : String for Error

function show() : Void for Error

function name() : String for Error

function status() : Int for Error

function lineno() : Int for Error

function source() : String for Error
```

## Job type
```
function in() : UnixFD for Job

function out() : UnixFD for Job

function %OP_GET($index : Int) : UnixFD for Job

function poll() : Boolean for Job

function wait() : Int for Job

function raise($s : Signal) : Void for Job

function detach() : Void for Job

function size() : Int for Job

function pid($index : Int) : Option<Int> for Job

function status($index : Int) : Option<Int> for Job
```

## StringIter type
```
function %OP_NEXT() : String for StringIter
```

## Regex type
```
function %OP_INIT($str : String, $flag : Option<String>) : Regex for Regex

function isCaseless() : Boolean for Regex

function isMultiLine() : Boolean for Regex

function isDotAll() : Boolean for Regex

function %OP_MATCH($target : String) : Boolean for Regex

function %OP_UNMATCH($target : String) : Boolean for Regex

function match($target : String) : Array<Option<String>> for Regex

function replace($target : String, $repl : String) : String for Regex
```

## Signal type
```
function name() : String for Signal

function value() : Int for Signal

function message() : String for Signal

function kill($pid : Int) : Void for Signal

function trap($handler : Option<Func<Void,[Signal]>>) : Func<Void,[Signal]> for Signal

function %OP_EQ($target : Signal) : Boolean for Signal

function %OP_NE($target : Signal) : Boolean for Signal
```

## Signals type
```
function %OP_GET($key : String) : Signal for Signals

function get($key : String) : Option<Signal> for Signals

function list() : Array<Signal> for Signals
```

## Module type
```
function _scriptName() : String for Module

function _scriptDir() : String for Module

function _func($expr : String) : Func<Option<Any>> for Module

function _fullname($name : String) : Option<String> for Module
```

## Reader type
```
function %OP_NEXT() : String for Reader
```

## Command type
```
function call($argv : Array<String>) : Boolean for Command
```

## LineEditor type
```
function %OP_INIT() : LineEditor for LineEditor

function readLine($p : Option<String>) : Option<String> for LineEditor

function setCompletion($comp : Option<Func<Array<String>,[Module,String]>>) : Void for LineEditor

function setPrompt($prompt : Option<Func<String,[String]>>) : Void for LineEditor

function setHistory($hist : Option<Func<Option<String>,[String,String]>>) : Void for LineEditor

function setColor($setting : String) : Void for LineEditor

function bind($key : String, $action : String) : Void for LineEditor

function bindings() : Map<String,String> for LineEditor
```

## Array type
```
function %OP_GET($index : Int) : T0 for Array<T0>

function get($index : Int) : Option<T0> for Array<T0>

function %OP_SET($index : Int, $value : T0) : Void for Array<T0>

function remove($index : Int) : T0 for Array<T0>

function peek() : T0 for Array<T0>

function push($value : T0) : Void for Array<T0>

function pop() : T0 for Array<T0>

function shift() : T0 for Array<T0>

function unshift($value : T0) : Void for Array<T0>

function insert($index : Int, $value : T0) : Void for Array<T0>

function add($value : T0) : Array<T0> for Array<T0>

function addAll($value : Array<T0>) : Array<T0> for Array<T0>

function swap($index : Int, $value : T0) : T0 for Array<T0>

function slice($from : Int, $to : Option<Int>) : Array<T0> for Array<T0>

function copy() : Array<T0> for Array<T0>

function reverse() : Array<T0> for Array<T0>

function sort() : Array<T0> where T0 : Value_ for Array<T0>

function sortWith($comp : Func<Boolean,[T0,T0]>) : Array<T0> for Array<T0>

function join($delim : Option<String>) : String for Array<T0>

function indexOf($target : T0, $index : Option<Int>) : Int where T0 : Value_ for Array<T0>

function lastIndexOf($target : T0) : Int where T0 : Value_ for Array<T0>

function contains($target : T0) : Boolean where T0 : Value_ for Array<T0>

function size() : Int for Array<T0>

function empty() : Boolean for Array<T0>

function clear() : Void for Array<T0>

function %OP_ITER() : Array<T0> for Array<T0>

function %OP_NEXT() : T0 for Array<T0>
```

## Map type
```
function %OP_GET($key : T0) : T1 for Map<T0,T1>

function %OP_SET($key : T0, $value : T1) : Void for Map<T0,T1>

function put($key : T0, $value : T1) : Option<T1> for Map<T0,T1>

function putIfAbsent($key : T0, $value : T1) : T1 for Map<T0,T1>

function size() : Int for Map<T0,T1>

function empty() : Boolean for Map<T0,T1>

function get($key : T0) : Option<T1> for Map<T0,T1>

function remove($key : T0) : Boolean for Map<T0,T1>

function swap($key : T0, $value : T1) : T1 for Map<T0,T1>

function addAll($value : Map<T0,T1>) : Map<T0,T1> for Map<T0,T1>

function copy() : Map<T0,T1> for Map<T0,T1>

function clear() : Void for Map<T0,T1>

function %OP_ITER() : Map<T0,T1> for Map<T0,T1>

function %OP_NEXT() : Tuple<T0,T1> for Map<T0,T1>
```

