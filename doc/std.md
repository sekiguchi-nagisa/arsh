# Standard Library Interface Definition
## Any type
```
function %OP_STR($this) : String for Any

function %OP_INTERP($this) : String for Any
```

## Int type
```
function %OP_PLUS($this) : Int for Int

function %OP_MINUS($this) : Int for Int

function %OP_NOT($this) : Int for Int

function %OP_ADD($this, $target : Int) : Int for Int

function %OP_SUB($this, $target : Int) : Int for Int

function %OP_MUL($this, $target : Int) : Int for Int

function %OP_DIV($this, $target : Int) : Int for Int

function %OP_MOD($this, $target : Int) : Int for Int

function %OP_EQ($this, $target : Int) : Boolean for Int

function %OP_NE($this, $target : Int) : Boolean for Int

function %OP_LT($this, $target : Int) : Boolean for Int

function %OP_GT($this, $target : Int) : Boolean for Int

function %OP_LE($this, $target : Int) : Boolean for Int

function %OP_GE($this, $target : Int) : Boolean for Int

function %OP_AND($this, $target : Int) : Int for Int

function %OP_OR($this, $target : Int) : Int for Int

function %OP_XOR($this, $target : Int) : Int for Int

function abs($this) : Int for Int

function %OP_TO_FLOAT($this) : Float for Int
```

## Float type
```
function %OP_PLUS($this) : Float for Float

function %OP_MINUS($this) : Float for Float

function %OP_ADD($this, $target : Float) : Float for Float

function %OP_SUB($this, $target : Float) : Float for Float

function %OP_MUL($this, $target : Float) : Float for Float

function %OP_DIV($this, $target : Float) : Float for Float

function %OP_EQ($this, $target : Float) : Boolean for Float

function %OP_NE($this, $target : Float) : Boolean for Float

function %OP_LT($this, $target : Float) : Boolean for Float

function %OP_GT($this, $target : Float) : Boolean for Float

function %OP_LE($this, $target : Float) : Boolean for Float

function %OP_GE($this, $target : Float) : Boolean for Float

function isNan($this) : Boolean for Float

function isInf($this) : Boolean for Float

function isFinite($this) : Boolean for Float

function isNormal($this) : Boolean for Float

function round($this) : Float for Float

function trunc($this) : Float for Float

function floor($this) : Float for Float

function ceil($this) : Float for Float

function abs($this) : Float for Float

function %OP_TO_INT($this) : Int for Float

function compare($this, $target : Float) : Int for Float
```

## Boolean type
```
function %OP_NOT($this) : Boolean for Boolean

function %OP_EQ($this, $target : Boolean) : Boolean for Boolean

function %OP_NE($this, $target : Boolean) : Boolean for Boolean
```

## String type
```
function %OP_EQ($this, $target : String) : Boolean for String

function %OP_NE($this, $target : String) : Boolean for String

function %OP_LT($this, $target : String) : Boolean for String

function %OP_GT($this, $target : String) : Boolean for String

function %OP_LE($this, $target : String) : Boolean for String

function %OP_GE($this, $target : String) : Boolean for String

function size($this) : Int for String

function empty($this) : Boolean for String

function count($this) : Int for String

function chars($this) : Array<String> for String

function width($this) : Int for String

function %OP_GET($this, $index : Int) : String for String

function charAt($this, $index : Int) : String for String

function slice($this, $start : Int, $stop : Int) : String for String

function from($this, $start : Int) : String for String

function to($this, $stop : Int) : String for String

function startsWith($this, $target : String) : Boolean for String

function endsWith($this, $target : String) : Boolean for String

function indexOf($this, $target : String) : Int for String

function lastIndexOf($this, $target : String) : Int for String

function contains($this, $target : String) : Boolean for String

function split($this, $delim : String) : Array<String> for String

function replace($this, $target : String, $rep : String) : String for String

function toInt($this) : Option<Int> for String

function toFloat($this) : Option<Float> for String

function %OP_ITER($this) : StringIter for String

function %OP_MATCH($this, $re : Regex) : Boolean for String

function %OP_UNMATCH($this, $re : Regex) : Boolean for String

function realpath($this) : Option<String> for String

function lower($this) : String for String

function upper($this) : String for String
```

## UnixFD type
```
function %OP_INIT($this, $path : String) : UnixFD for UnixFD

function close($this) : Void for UnixFD

function dup($this) : UnixFD for UnixFD

function %OP_BOOL($this) : Boolean for UnixFD

function %OP_NOT($this) : Boolean for UnixFD
```

## Error type
```
function %OP_INIT($this, $message : String) : Error for Error

function message($this) : String for Error

function show($this) : Void for Error

function name($this) : String for Error
```

## Job type
```
function in($this) : UnixFD for Job

function out($this) : UnixFD for Job

function %OP_GET($this, $index : Int) : UnixFD for Job

function poll($this) : Boolean for Job

function wait($this) : Int for Job

function raise($this, $s : Signal) : Void for Job

function detach($this) : Void for Job

function size($this) : Int for Job

function pid($this, $index : Int) : Option<Int> for Job

function status($this, $index : Int) : Option<Int> for Job
```

## StringIter type
```
function %OP_NEXT($this) : String for StringIter

function %OP_HAS_NEXT($this) : Boolean for StringIter
```

## Regex type
```
function %OP_INIT($this, $str : String, $flag : String) : Regex for Regex

function %OP_MATCH($this, $target : String) : Boolean for Regex

function %OP_UNMATCH($this, $target : String) : Boolean for Regex

function match($this, $target : String) : Array<Option<String>> for Regex

function replace($this, $target : String, $repl : String) : String for Regex
```

## Signal type
```
function name($this) : String for Signal

function value($this) : Int for Signal

function message($this) : String for Signal

function kill($this, $pid : Int) : Void for Signal

function %OP_EQ($this, $target : Signal) : Boolean for Signal

function %OP_NE($this, $target : Signal) : Boolean for Signal
```

## Signals type
```
function %OP_GET($this, $s : Signal) : Func<Void,[Signal]> for Signals

function %OP_SET($this, $s : Signal, $action : Func<Void,[Signal]>) : Void for Signals

function signal($this, $key : String) : Option<Signal> for Signals

function list($this) : Array<Signal> for Signals
```

## Module type
```
function scriptName($this) : String for Module

function scriptDir($this) : String for Module

function func($this, $expr : String) : Func<Option<Any>> for Module

function fullname($this, $name : String) : Option<String> for Module
```

## Array type
```
function %OP_GET($this, $index : Int) : T0 for Array<T0>

function get($this, $index : Int) : Option<T0> for Array<T0>

function %OP_SET($this, $index : Int, $value : T0) : Void for Array<T0>

function remove($this, $index : Int) : T0 for Array<T0>

function peek($this) : T0 for Array<T0>

function push($this, $value : T0) : Void for Array<T0>

function pop($this) : T0 for Array<T0>

function shift($this) : T0 for Array<T0>

function unshift($this, $value : T0) : Void for Array<T0>

function insert($this, $index : Int, $value : T0) : Void for Array<T0>

function add($this, $value : T0) : Array<T0> for Array<T0>

function addAll($this, $value : Array<T0>) : Array<T0> for Array<T0>

function swap($this, $index : Int, $value : T0) : T0 for Array<T0>

function slice($this, $from : Int, $to : Int) : Array<T0> for Array<T0>

function from($this, $from : Int) : Array<T0> for Array<T0>

function to($this, $to : Int) : Array<T0> for Array<T0>

function copy($this) : Array<T0> for Array<T0>

function reverse($this) : Array<T0> for Array<T0>

function sort($this) : Array<T0> where T0 : _Value for Array<T0>

function sortWith($this, $comp : Func<Boolean,[T0,T0]>) : Array<T0> for Array<T0>

function join($this, $delim : String) : String for Array<T0>

function forEach($this, $consumer : Func<Void,[T0]>) : Void for Array<T0>

function indexOf($this, $target : T0) : Int where T0 : _Value for Array<T0>

function lastIndexOf($this, $target : T0) : Int where T0 : _Value for Array<T0>

function contains($this, $target : T0) : Boolean where T0 : _Value for Array<T0>

function size($this) : Int for Array<T0>

function empty($this) : Boolean for Array<T0>

function clear($this) : Void for Array<T0>

function %OP_ITER($this) : Array<T0> for Array<T0>

function %OP_NEXT($this) : T0 for Array<T0>

function %OP_HAS_NEXT($this) : Boolean for Array<T0>

function %OP_CMD_ARG($this) : Array<String> for Array<T0>
```

## Map type
```
function %OP_GET($this, $key : T0) : T1 for Map<T0,T1>

function %OP_SET($this, $key : T0, $value : T1) : Void for Map<T0,T1>

function put($this, $key : T0, $value : T1) : Option<T1> for Map<T0,T1>

function putIfAbsent($this, $key : T0, $value : T1) : T1 for Map<T0,T1>

function size($this) : Int for Map<T0,T1>

function empty($this) : Boolean for Map<T0,T1>

function get($this, $key : T0) : Option<T1> for Map<T0,T1>

function remove($this, $key : T0) : Boolean for Map<T0,T1>

function swap($this, $key : T0, $value : T1) : T1 for Map<T0,T1>

function addAll($this, $value : Map<T0,T1>) : Map<T0,T1> for Map<T0,T1>

function copy($this) : Map<T0,T1> for Map<T0,T1>

function clear($this) : Void for Map<T0,T1>

function %OP_ITER($this) : Map<T0,T1> for Map<T0,T1>

function %OP_NEXT($this) : Tuple<T0,T1> for Map<T0,T1>

function %OP_HAS_NEXT($this) : Boolean for Map<T0,T1>
```

## Tuple type
```
function %OP_CMD_ARG($this) : Array<String> for Tuple<>
```

