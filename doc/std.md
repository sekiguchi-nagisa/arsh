# Standard Library Interface Definition
## Any type
```
function %OP_STR($this : Any) : String

function %OP_INTERP($this : Any) : String
```

## Byte type
```
function %OP_PLUS($this : Byte) : Byte

function %OP_MINUS($this : Byte) : Byte

function %OP_NOT($this : Byte) : Byte
```

## Int16 type
```
function %OP_PLUS($this : Int16) : Int16

function %OP_MINUS($this : Int16) : Int16

function %OP_NOT($this : Int16) : Int16
```

## Uint16 type
```
function %OP_PLUS($this : Uint16) : Uint16

function %OP_MINUS($this : Uint16) : Uint16

function %OP_NOT($this : Uint16) : Uint16
```

## Int32 type
```
function %OP_PLUS($this : Int32) : Int32

function %OP_MINUS($this : Int32) : Int32

function %OP_NOT($this : Int32) : Int32

function %OP_ADD($this : Int32, $target : Int32) : Int32

function %OP_SUB($this : Int32, $target : Int32) : Int32

function %OP_MUL($this : Int32, $target : Int32) : Int32

function %OP_DIV($this : Int32, $target : Int32) : Int32

function %OP_MOD($this : Int32, $target : Int32) : Int32

function %OP_EQ($this : Int32, $target : Int32) : Boolean

function %OP_NE($this : Int32, $target : Int32) : Boolean

function %OP_LT($this : Int32, $target : Int32) : Boolean

function %OP_GT($this : Int32, $target : Int32) : Boolean

function %OP_LE($this : Int32, $target : Int32) : Boolean

function %OP_GE($this : Int32, $target : Int32) : Boolean

function %OP_AND($this : Int32, $target : Int32) : Int32

function %OP_OR($this : Int32, $target : Int32) : Int32

function %OP_XOR($this : Int32, $target : Int32) : Int32
```

## Uint32 type
```
function %OP_PLUS($this : Uint32) : Uint32

function %OP_MINUS($this : Uint32) : Uint32

function %OP_NOT($this : Uint32) : Uint32

function %OP_ADD($this : Uint32, $target : Uint32) : Uint32

function %OP_SUB($this : Uint32, $target : Uint32) : Uint32

function %OP_MUL($this : Uint32, $target : Uint32) : Uint32

function %OP_DIV($this : Uint32, $target : Uint32) : Uint32

function %OP_MOD($this : Uint32, $target : Uint32) : Uint32

function %OP_EQ($this : Uint32, $target : Uint32) : Boolean

function %OP_NE($this : Uint32, $target : Uint32) : Boolean

function %OP_LT($this : Uint32, $target : Uint32) : Boolean

function %OP_GT($this : Uint32, $target : Uint32) : Boolean

function %OP_LE($this : Uint32, $target : Uint32) : Boolean

function %OP_GE($this : Uint32, $target : Uint32) : Boolean

function %OP_AND($this : Uint32, $target : Uint32) : Uint32

function %OP_OR($this : Uint32, $target : Uint32) : Uint32

function %OP_XOR($this : Uint32, $target : Uint32) : Uint32
```

## Int64 type
```
function %OP_PLUS($this : Int64) : Int64

function %OP_MINUS($this : Int64) : Int64

function %OP_NOT($this : Int64) : Int64

function %OP_ADD($this : Int64, $target : Int64) : Int64

function %OP_SUB($this : Int64, $target : Int64) : Int64

function %OP_MUL($this : Int64, $target : Int64) : Int64

function %OP_DIV($this : Int64, $target : Int64) : Int64

function %OP_MOD($this : Int64, $target : Int64) : Int64

function %OP_EQ($this : Int64, $target : Int64) : Boolean

function %OP_NE($this : Int64, $target : Int64) : Boolean

function %OP_LT($this : Int64, $target : Int64) : Boolean

function %OP_GT($this : Int64, $target : Int64) : Boolean

function %OP_LE($this : Int64, $target : Int64) : Boolean

function %OP_GE($this : Int64, $target : Int64) : Boolean

function %OP_AND($this : Int64, $target : Int64) : Int64

function %OP_OR($this : Int64, $target : Int64) : Int64

function %OP_XOR($this : Int64, $target : Int64) : Int64
```

## Uint64 type
```
function %OP_PLUS($this : Uint64) : Uint64

function %OP_MINUS($this : Uint64) : Uint64

function %OP_NOT($this : Uint64) : Uint64

function %OP_ADD($this : Uint64, $target : Uint64) : Uint64

function %OP_SUB($this : Uint64, $target : Uint64) : Uint64

function %OP_MUL($this : Uint64, $target : Uint64) : Uint64

function %OP_DIV($this : Uint64, $target : Uint64) : Uint64

function %OP_MOD($this : Uint64, $target : Uint64) : Uint64

function %OP_EQ($this : Uint64, $target : Uint64) : Boolean

function %OP_NE($this : Uint64, $target : Uint64) : Boolean

function %OP_LT($this : Uint64, $target : Uint64) : Boolean

function %OP_GT($this : Uint64, $target : Uint64) : Boolean

function %OP_LE($this : Uint64, $target : Uint64) : Boolean

function %OP_GE($this : Uint64, $target : Uint64) : Boolean

function %OP_AND($this : Uint64, $target : Uint64) : Uint64

function %OP_OR($this : Uint64, $target : Uint64) : Uint64

function %OP_XOR($this : Uint64, $target : Uint64) : Uint64
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

function size($this : String) : Int32

function empty($this : String) : Boolean

function count($this : String) : Int32

function %OP_GET($this : String, $index : Int32) : String

function charAt($this : String, $index : Int32) : String

function slice($this : String, $start : Int32, $stop : Int32) : String

function from($this : String, $start : Int32) : String

function to($this : String, $stop : Int32) : String

function startsWith($this : String, $target : String) : Boolean

function endsWith($this : String, $target : String) : Boolean

function indexOf($this : String, $target : String) : Int32

function lastIndexOf($this : String, $target : String) : Int32

function split($this : String, $delim : String) : Array<String>

function replace($this : String, $target : String, $rep : String) : String

function toInt32($this : String) : Option<Int32>

function toUint32($this : String) : Option<Uint32>

function toInt64($this : String) : Option<Int64>

function toUint64($this : String) : Option<Uint64>

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
constructor($this : UnixFD, $path : String) : Void

function close($this : UnixFD) : Void

function dup($this : UnixFD) : UnixFD

function %OP_BOOL($this : UnixFD) : Boolean

function %OP_NOT($this : UnixFD) : Boolean
```

## Error type
```
constructor($this : Error, $message : String) : Void

function message($this : Error) : String

function backtrace($this : Error) : Void

function name($this : Error) : String
```

## Job type
```
function in($this : Job) : UnixFD

function out($this : Job) : UnixFD

function %OP_GET($this : Job, $index : Int32) : UnixFD

function poll($this : Job) : Boolean

function wait($this : Job) : Int32

function raise($this : Job, $s : Signal) : Void

function detach($this : Job) : Void

function size($this : Job) : Int32

function pid($this : Job, $index : Int32) : Int32
```

## StringIter type
```
function %OP_NEXT($this : StringIter) : String

function %OP_HAS_NEXT($this : StringIter) : Boolean
```

## Regex type
```
constructor($this : Regex, $str : String) : Void

function %OP_MATCH($this : Regex, $target : String) : Boolean

function %OP_UNMATCH($this : Regex, $target : String) : Boolean

function match($this : Regex, $target : String) : Array<String>
```

## Signal type
```
function name($this : Signal) : String

function value($this : Signal) : Int32

function message($this : Signal) : String

function kill($this : Signal, $pid : Int32) : Void

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

## Array type
```
constructor($this : Array<T0>) : Void

function %OP_GET($this : Array<T0>, $index : Int32) : T0

function get($this : Array<T0>, $index : Int32) : Option<T0>

function %OP_SET($this : Array<T0>, $index : Int32, $value : T0) : Void

function remove($this : Array<T0>, $index : Int32) : T0

function peek($this : Array<T0>) : T0

function push($this : Array<T0>, $value : T0) : Void

function pop($this : Array<T0>) : T0

function shift($this : Array<T0>) : T0

function unshift($this : Array<T0>, $value : T0) : Void

function insert($this : Array<T0>, $index : Int32, $value : T0) : Void

function add($this : Array<T0>, $value : T0) : Array<T0>

function extend($this : Array<T0>, $value : Array<T0>) : Array<T0>

function swap($this : Array<T0>, $index : Int32, $value : T0) : T0

function slice($this : Array<T0>, $from : Int32, $to : Int32) : Array<T0>

function from($this : Array<T0>, $from : Int32) : Array<T0>

function to($this : Array<T0>, $to : Int32) : Array<T0>

function copy($this : Array<T0>) : Array<T0>

function reverse($this : Array<T0>) : Array<T0>

function sort($this : Array<T0>) : Array<T0>

function sortWith($this : Array<T0>, $comp : Func<Boolean,[T0,T0]>) : Array<T0>

function join($this : Array<T0>, $delim : String) : String

function size($this : Array<T0>) : Int32

function empty($this : Array<T0>) : Boolean

function clear($this : Array<T0>) : Void

function %OP_ITER($this : Array<T0>) : Array<T0>

function %OP_NEXT($this : Array<T0>) : T0

function %OP_HAS_NEXT($this : Array<T0>) : Boolean

function %OP_CMD_ARG($this : Array<T0>) : Array<String>
```

## Map type
```
constructor($this : Map<T0,T1>) : Void

function %OP_GET($this : Map<T0,T1>, $key : T0) : T1

function %OP_SET($this : Map<T0,T1>, $key : T0, $value : T1) : Void

function put($this : Map<T0,T1>, $key : T0, $value : T1) : Option<T1>

function default($this : Map<T0,T1>, $key : T0, $value : T1) : T1

function size($this : Map<T0,T1>) : Int32

function empty($this : Map<T0,T1>) : Boolean

function get($this : Map<T0,T1>, $key : T0) : Option<T1>

function find($this : Map<T0,T1>, $key : T0) : Boolean

function remove($this : Map<T0,T1>, $key : T0) : Boolean

function swap($this : Map<T0,T1>, $key : T0, $value : T1) : T1

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

