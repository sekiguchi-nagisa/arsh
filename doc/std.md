# Standard Library Interface Definition
## Any type
```
function %OP_STR($this : Any) : String

function %OP_INTERP($this : Any) : String
```

## Int type
```
function %OP_PLUS($this : Int) : Int

function %OP_MINUS($this : Int) : Int

function %OP_NOT($this : Int) : Int

function %OP_ADD($this : Int, $target : Int) : Int

function %OP_SUB($this : Int, $target : Int) : Int

function %OP_MUL($this : Int, $target : Int) : Int

function %OP_DIV($this : Int, $target : Int) : Int

function %OP_MOD($this : Int, $target : Int) : Int

function %OP_EQ($this : Int, $target : Int) : Boolean

function %OP_NE($this : Int, $target : Int) : Boolean

function %OP_LT($this : Int, $target : Int) : Boolean

function %OP_GT($this : Int, $target : Int) : Boolean

function %OP_LE($this : Int, $target : Int) : Boolean

function %OP_GE($this : Int, $target : Int) : Boolean

function %OP_AND($this : Int, $target : Int) : Int

function %OP_OR($this : Int, $target : Int) : Int

function %OP_XOR($this : Int, $target : Int) : Int

function abs($this : Int) : Int

function %OP_TO_FLOAT($this : Int) : Float
```

## Float type
```
function %OP_PLUS($this : Float) : Float

function %OP_MINUS($this : Float) : Float

function %OP_ADD($this : Float, $target : Float) : Float

function %OP_SUB($this : Float, $target : Float) : Float

function %OP_MUL($this : Float, $target : Float) : Float

function %OP_DIV($this : Float, $target : Float) : Float

function %OP_EQ($this : Float, $target : Float) : Boolean

function %OP_NE($this : Float, $target : Float) : Boolean

function %OP_LT($this : Float, $target : Float) : Boolean

function %OP_GT($this : Float, $target : Float) : Boolean

function %OP_LE($this : Float, $target : Float) : Boolean

function %OP_GE($this : Float, $target : Float) : Boolean

function isNan($this : Float) : Boolean

function isInf($this : Float) : Boolean

function isFinite($this : Float) : Boolean

function isNormal($this : Float) : Boolean

function round($this : Float) : Float

function trunc($this : Float) : Float

function floor($this : Float) : Float

function ceil($this : Float) : Float

function abs($this : Float) : Float

function %OP_TO_INT($this : Float) : Int

function compare($this : Float, $target : Float) : Int
```

## Boolean type
```
function %OP_NOT($this : Boolean) : Boolean

function %OP_EQ($this : Boolean, $target : Boolean) : Boolean

function %OP_NE($this : Boolean, $target : Boolean) : Boolean
```

## String type
```
function %OP_EQ($this : String, $target : String) : Boolean

function %OP_NE($this : String, $target : String) : Boolean

function %OP_LT($this : String, $target : String) : Boolean

function %OP_GT($this : String, $target : String) : Boolean

function %OP_LE($this : String, $target : String) : Boolean

function %OP_GE($this : String, $target : String) : Boolean

function size($this : String) : Int

function empty($this : String) : Boolean

function count($this : String) : Int

function chars($this : String) : Array<String>

function width($this : String) : Int

function %OP_GET($this : String, $index : Int) : String

function charAt($this : String, $index : Int) : String

function slice($this : String, $start : Int, $stop : Int) : String

function from($this : String, $start : Int) : String

function to($this : String, $stop : Int) : String

function startsWith($this : String, $target : String) : Boolean

function endsWith($this : String, $target : String) : Boolean

function indexOf($this : String, $target : String) : Int

function lastIndexOf($this : String, $target : String) : Int

function contains($this : String, $target : String) : Boolean

function split($this : String, $delim : String) : Array<String>

function replace($this : String, $target : String, $rep : String) : String

function toInt($this : String) : Option<Int>

function toFloat($this : String) : Option<Float>

function %OP_ITER($this : String) : StringIter

function %OP_MATCH($this : String, $re : Regex) : Boolean

function %OP_UNMATCH($this : String, $re : Regex) : Boolean

function realpath($this : String) : Option<String>

function lower($this : String) : String

function upper($this : String) : String
```

## UnixFD type
```
function %OP_INIT($this : UnixFD, $path : String) : UnixFD

function close($this : UnixFD) : Void

function dup($this : UnixFD) : UnixFD

function %OP_BOOL($this : UnixFD) : Boolean

function %OP_NOT($this : UnixFD) : Boolean
```

## Error type
```
function %OP_INIT($this : Error, $message : String) : Error

function message($this : Error) : String

function backtrace($this : Error) : Void

function name($this : Error) : String
```

## Job type
```
function in($this : Job) : UnixFD

function out($this : Job) : UnixFD

function %OP_GET($this : Job, $index : Int) : UnixFD

function poll($this : Job) : Boolean

function wait($this : Job) : Int

function raise($this : Job, $s : Signal) : Void

function detach($this : Job) : Void

function size($this : Job) : Int

function pid($this : Job, $index : Int) : Option<Int>

function status($this : Job, $index : Int) : Option<Int>
```

## StringIter type
```
function %OP_NEXT($this : StringIter) : String

function %OP_HAS_NEXT($this : StringIter) : Boolean
```

## Regex type
```
function %OP_INIT($this : Regex, $str : String, $flag : String) : Regex

function %OP_MATCH($this : Regex, $target : String) : Boolean

function %OP_UNMATCH($this : Regex, $target : String) : Boolean

function match($this : Regex, $target : String) : Array<Option<String>>

function replace($this : Regex, $target : String, $repl : String) : String
```

## Signal type
```
function name($this : Signal) : String

function value($this : Signal) : Int

function message($this : Signal) : String

function kill($this : Signal, $pid : Int) : Void

function %OP_EQ($this : Signal, $target : Signal) : Boolean

function %OP_NE($this : Signal, $target : Signal) : Boolean
```

## Signals type
```
function %OP_GET($this : Signals, $s : Signal) : Func<Void,[Signal]>

function %OP_SET($this : Signals, $s : Signal, $action : Func<Void,[Signal]>) : Void

function signal($this : Signals, $key : String) : Option<Signal>

function list($this : Signals) : Array<Signal>
```

## Module type
```
function scriptName($this : Module) : String

function scriptDir($this : Module) : String

function func($this : Module, $expr : String) : Func<Option<Any>>

function fullname($this : Module, $name : String) : Option<String>
```

## Array type
```
function %OP_GET($this : Array<T0>, $index : Int) : T0

function get($this : Array<T0>, $index : Int) : Option<T0>

function %OP_SET($this : Array<T0>, $index : Int, $value : T0) : Void

function remove($this : Array<T0>, $index : Int) : T0

function peek($this : Array<T0>) : T0

function push($this : Array<T0>, $value : T0) : Void

function pop($this : Array<T0>) : T0

function shift($this : Array<T0>) : T0

function unshift($this : Array<T0>, $value : T0) : Void

function insert($this : Array<T0>, $index : Int, $value : T0) : Void

function add($this : Array<T0>, $value : T0) : Array<T0>

function addAll($this : Array<T0>, $value : Array<T0>) : Array<T0>

function swap($this : Array<T0>, $index : Int, $value : T0) : T0

function slice($this : Array<T0>, $from : Int, $to : Int) : Array<T0>

function from($this : Array<T0>, $from : Int) : Array<T0>

function to($this : Array<T0>, $to : Int) : Array<T0>

function copy($this : Array<T0>) : Array<T0>

function reverse($this : Array<T0>) : Array<T0>

function sort($this : Array<T0>) : Array<T0> where T0 : _Value

function sortWith($this : Array<T0>, $comp : Func<Boolean,[T0,T0]>) : Array<T0>

function join($this : Array<T0>, $delim : String) : String

function forEach($this : Array<T0>, $consumer : Func<Void,[T0]>) : Void

function indexOf($this : Array<T0>, $target : T0) : Int where T0 : _Value

function lastIndexOf($this : Array<T0>, $target : T0) : Int where T0 : _Value

function contains($this : Array<T0>, $target : T0) : Boolean where T0 : _Value

function size($this : Array<T0>) : Int

function empty($this : Array<T0>) : Boolean

function clear($this : Array<T0>) : Void

function %OP_ITER($this : Array<T0>) : Array<T0>

function %OP_NEXT($this : Array<T0>) : T0

function %OP_HAS_NEXT($this : Array<T0>) : Boolean

function %OP_CMD_ARG($this : Array<T0>) : Array<String>
```

## Map type
```
function %OP_GET($this : Map<T0,T1>, $key : T0) : T1

function %OP_SET($this : Map<T0,T1>, $key : T0, $value : T1) : Void

function put($this : Map<T0,T1>, $key : T0, $value : T1) : Option<T1>

function default($this : Map<T0,T1>, $key : T0, $value : T1) : T1

function size($this : Map<T0,T1>) : Int

function empty($this : Map<T0,T1>) : Boolean

function get($this : Map<T0,T1>, $key : T0) : Option<T1>

function remove($this : Map<T0,T1>, $key : T0) : Boolean

function swap($this : Map<T0,T1>, $key : T0, $value : T1) : T1

function addAll($this : Map<T0,T1>, $value : Map<T0,T1>) : Map<T0,T1>

function copy($this : Map<T0,T1>) : Map<T0,T1>

function clear($this : Map<T0,T1>) : Void

function %OP_ITER($this : Map<T0,T1>) : Map<T0,T1>

function %OP_NEXT($this : Map<T0,T1>) : Tuple<T0,T1>

function %OP_HAS_NEXT($this : Map<T0,T1>) : Boolean
```

## Tuple type
```
function %OP_CMD_ARG($this : Tuple<>) : Array<String>
```

