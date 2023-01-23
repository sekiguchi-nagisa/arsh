# Changelog

## [Unreleased]

### Added

#### Builtin

- various ``LineEditor`` improvements
    - support alt-arrow key (for mac)
        - ``^[^[[A``, ``^[^[[B``, ``^[^[[C``, ``^[^[[D``
    - add the following keybinding methods
        - ``bind``: change keybinding
        - ``bindings``: get read-only snapshot of current keybindings
            - modification of the result does not affect actual keybindings
        - ``actions``: get read-only snapshot of current edit actions
- add builtin ``disown`` command
- add directory stack related builtin commands
    - ``dirs``: show directory stack entries
    - ``pushd``: change current directory and save old onto the directory stack
    - ``popd``: remove an entry from directory stack
    - also add ``DIRSTACK`` global variable

### Changed

#### Core

- **Breaking Change**: in interactive mode or ``-c`` mode without shell-name, toplevel ``$0`` indicates ``argv[0]``
- **Breaking Change**: unbalanced brace expansions are semantic error
- **Breaking Change**: change token format error with semantic error
- show stack trace of ignored exceptions within finally/defer block

#### Builtin

- **Breaking Change**: change default key action of ``alt-left``, ``alt-right`` with ``backward-word``
  and ``forward-word``
- **Breaking Change**: now builtin commands support `-h`/`--help` options
    - except for `:`, `echo`, `eval`, `false`, `true`
- **Breaking Change**: check array size modification during ``Array#sortWith`` method
    - now throw ``InvalidOperation`` error
- builtin ``shctl info`` subcommand show more system constant information

### Fixed

- symbol range of ``textDocument/hover``, ``textDocument/definition``
- broken help message of builtin ulimit command
- invalid insertion position of completion candidate prefix
- broken analyzer state when reuse analyzer instance

## [0.28.1] - 2023-01-04

### Fixed

- bugfix code generation of try-finally/defer
    - fix broken stack unwind in try expression
    - now maintain try block level
- bugfix code generation of case expression with option type expression

## [0.28.0] - 2022-12-31

### Added

#### Core

- support mutual recursion of named function, method, user-defined command
    - now the following code is valid
      ```
      function even(n : Int) : Bool {
        if $n == 0 {
          return $true
        } else {
          return $odd($n - 1)
        }
      }
      
      function odd(n : Int) : Bool {
        if $n == 0 {
          return $false
        } else {
          return $even($n - 1)
        }
      }
      ```
    - ``even`` can refer backward defined functions that are defined immediately after it
- add anonymous user-defined command
    - anonymous user-defined command object is ``Command`` type
    - call command via ``Command#call`` method
      ```
      var cmd = (){
        echo $0: $@
      }
      $cmd.call(['name', 'arg1', 'arg2'])
      ```
- introduce dynamic registered user-defined commands
    - before lookup external command, lookup ``Command`` object from builtin ``DYNA_UDCS`` variable
        - builtin ``command`` command checks existence of dynamic registered commands, but does not call theme (
          builtin ``eval`` command can call these commands)
        - ``Module#_fullname`` method also supports theme
    - also complete dynamic registered command names
- add ``clobber`` runtime option
    - enable by default
    - if disabled, ``>`` and ``&>`` redirection failed if file exists
    - also add ``>|`` and ``&>|`` redirection (always overwrite file even if clobber is disabled)

#### Builtin

- ``Error`` type objects maintain exit status
    - now specify exit status to constructor
    - add ``Error#status`` method for get exit status
- add some options to builtin ``complete`` command
    - ``-q``: does not show completion candidates (but still set to ``COMPREPLY``)
    - ``-s``: append space to completion candidate when number of candidates is 1
- add ``LineEdtior`` type for line editing
    - ``readLine``: entry point of line editing
    - ``setCompletion``: specify completion callback
    - ``setPrompt``: specify prompt callback
    - ``setHistory``: specify history callback
    - ``setColor``: specify syntax highlight color theme
    - also support multi-line editing
    - CTRL-U, CTRL-K, CTRL-A, CTRL-E, CTRL-R are multi-line aware
    - CTRL-T is grapheme-aware
    - support ALT-Up, ALT-Down for multiline-aware history rotating
    - support ALT-Left, ALT-Right for alternative of home/end key
- add builtin ``LINE_EDIT`` variable for repl api
- add some methods to ``FD`` type
    - ``value``: get internal file descriptor number
    - ``lock``: place exclusive lock
    - ``unlock``: remove existing lock

#### API

- add ``DSState_readLine`` api for line editing
    - control line editing behavior via ``LINE_EDIT`` builtin variable

#### Misc

- update ``dscolorize``
    - add ``--html-lineno-table`` option
        - now generate line number as table
    - add ``--dump`` option
        - dump color setting

### Changed

#### Core

- **Breaking Change**: change evaluation order of ``TERM_HOOK``
    - now only called from ``DSState_delete`` or subshell exit
    - in interactive mode, does not call ``TERM_HOOK`` in uncaught exception
- **Breaking Change**: improve error checking of back-quote literal
    - now syntactically accept back-quote literal, but always report semantic error
    - now does not allow back-quote characters without escape within double-quoted string literal
- **Breaking Change**: change internal implementation of ``SCRIPT_DIR``, ``SCRIPT_NAME``
    - now ``SCRIPT_DIR`` and ``SCRIPT_NAME`` are always equivalent to ``Module#_scriptDir`` and ``Module#_scriptName``
    - in interactive mode, after change CWD, compile-time ``SCRIPT_DIR`` and run-time ``SCRIPT_DIR`` are different
        - run-time ``SCRIPT_DIR`` always indicates latest compile-time ``SCRIPT_DIR``
- **Breaking Change**: now does not ignore empty string in command arguments
  ```
  var a = ''
  echo $a a          # output is ' a'
  echo ${['', 'a']}  # output is ' a'
  ```
- **Breaking Change**: allow positional arguments up to INT32_MAX decimal
    - now ``"${34}"`` indicates positional argument (does not indicate int literal)
    - also allow redundant prefix ``0``, such ``000``, ``0009``
    - always synchronize current ``@`` content
- **Breaking Change**: ``#`` variable always indicates current ``@`` size
- **Breaking Change**: disallow relative glob pattern in source statement
- **Breaking Change**: overhaul io redirection
    - support ``<&`` operator
    - now ``&>`` and ``>&`` are different semantics
    - ``<``, ``>``, ``>>``, ``&>``, ``&>>`` operators only accept ``String`` expression
        - when redirect to ``FD`` object, use ``>&`` or ``<&`` instead
    - ``1>&2``and ``2>&1`` are recognized as ``1>& 2``, ``2>& 1``
    - now support ``[n]> word`` style notation.
        - ``[n]`` indicate decimal file descriptor numbers (only allow 0, 1, 2)
- **Breaking Change**: change user-defined method
    - now only allowed for user-defined type that defined at same module
    - cannot define same name method as field
- **Breaking Change**: overhaul exception handling
    - now only throw and catch derived types from ``Error`` type
- **Breaking Change**: improve smart cast.
    - also support option type auto-unwrap
    - supported like the following context
      ```
      ## auto down cast
      var e = -45 as Any
      if $e is Int {
        assert $e.abs() == 45
      }
      assert ($e is Int ? $e.abs() : 0) == 45
      assert $e is Int && $e.abs() == 45
      
      ## auto unwrap
      var o = '34'.toInt()
      if $o {
        assert $o == 34
      }
      assert ($o ? $o : 0) == 34
      assert $o && $o == 45
      ```
- **Breaking Change**: ``errraise`` option ignore SIGPIPE failure in left hand-side of pipe by default
    - if check SIGPIPE failure, set ``failsigpipe`` option
- **Breaking Change**: does not skip newline after command except for skippable newline context
    - now following code is syntax error
      ```
      while true  # does not skip newline after command
      { }
      ```
- **Breaking Change**: now check tilde expansion failure
    - check tilde expansion failure in runtime/compile time (also within glob/brace expansion)
    - now report runtime tilde expansion failure as ``TildeError``
- improve the following error messages
    - unclosed string, back-quote, regex literal
    - io redirection
- improve error message and line number within parenthesis

#### Builtin

- **Breaking Change**: change interface of ``CMD_FALLBACK``
    - now pass caller module context to fallback handler
- **Breaking Change**: only allow decimal integer in ``test`` command like bash
- **Breaking Change**: FD type constructor does not set close-on-exec flag
- **Breaking Change**: remove ``EDIT_HOOK`` builtin variable
- **Breaking Change**: change typechecking of equality operator of Func type
    - now allow subtype expression in right hand-side
- **Breaking Change**: change ``String#realpath`` behavior
    - now does not perform tilde expansion
    - now does not accept string having null characters
- improve runtime option recognition
    - now allow upper case, snake case, kebab case
      ```
      shctl set TRACE_ON_EXIT Null-Glob
      ```

#### Module

- save currently added history entry immediately
- rename edit module with repl
    - now import completion scripts
- re-implement bash completion wrapper in completion module
    - no longer need foreign completion wrapper

#### API

- **Breaking Change**: remove the following api
    - ``DSState_setExitStatus``
    - ``DSState_complete``
    - ``DSState_getCompletion``
    - ``DSState_lineEdit``
    - ``DSState_showNotification``

#### Misc

- **Breaking Change**: remove daemon mode in ``dscolorize``
- auto-generate rcfile at startup

### Fixed

- broken error check of glob in ``source!`` statement
- broken error check of runtime glob expansion when ``nullglob`` option is enabled
- fix number parsing in some builtin commands
    - now only allow decimal number except for explicitly requiring hex/octal numbers
    - allow redundant prefix 0
- crash indexing of source statements that have glob/brace expansion
- fix module symbol synchronization of ``DSState_loadModule`` api
- crash ``Signal#trap`` method when pass closure
- type name completion in constructor parameter
- broken lexing of dollar single quoted string literal
- escape sequence handling of renderPrompt function in prompt module
- skippable newline handling in some expressions
- SEVG of ``textDocument/documentSymbol``, ``textDocument/documentLink``
- fix ``textDocument/publishDiagnostics`` emission

## [0.27.1] - 2022-09-30

### Fixed

- accidentally close FD object within nested user-defined command
- pid range checking of ``Signal#kill`` method

## [0.27.0] - 2022-09-25

### Added

#### Core

- add ``time`` expression
    - like bash or zsh, ``time [pipeline]``
- support optional arguments in func, method, constructor call
    - can omit last argument that types are Option type
      ```
      function sum(a : Int, b : Int!) { return $a + ($b ?? 0) }
      assert $sum(23, 23) == 46
      assert $sum(34) == 34    # last parameter is Option type
      ```
- unpack key-value pair during map iteration
  ```
  for k, v in ['a' : 12, 'b': 34] {
    echo $k $v
  }
  ```

#### Builtin

- add builtin signal constants (POSIX.1-1990 standard signal)
    - ``SIGABRT``, ``SIGALRM``, ``SIGCHLD``, ``SIGCONT``, ``SIGFPE``, ``SIGHUP``,
    - ``SIGILL``, ``SIGINT``, ``SIGKILL``, ``SIGPIPE``, ``SIGQUIT``, ``SIGSEGV``,
    - ``SIGSTOP``, ``SIGTERM``, ``SIGTSTP``, ``SIGTTIN``, ``SIGTTOU``, ``SIGUSR1``, ``SIGUSR2``
- add ``String#sanitize`` method
    - replace invalid utf8 bytes and null characters
- add ``String#words`` method
    - split string with words (follow unicode word boundary)
- add the following regex flag check methods
    - ``isMultiLine``
    - ``isCaseless``
    - ``isDotAll``
- add builtin ``jobs`` command
    - show job information (except for disowned jobs)

#### API

- add the following line edit op
    - ``DS_EDIT_NEXT_CHAR_LEN``, ``DS_EDIT_PREV_CHAR_LEN``: for unicode-aware character length counting
    - ``DS_EDIT_NEXT_WORD_LEN``, ``DS_EDIT_PREV_WORD_LEN``: for unicode-aware word length counting
    - ``DS_EDIT_HIGHLIGHT``: for syntax highlighting
- add ``DSState_showNotification`` api for job termination notification

#### Module

- ``completion`` module autoload bash-completion script

### Changed

#### Core

- **Breaking Change**: remove signal literal. use signal constants instead
- **Breaking Change**: cancel code completion when user-defined completer throws an error
- **Breaking Change**: not allow explicit cast from ``Nothing`` type
- **Breaking Change**: change operator precedence of ``coproc``
    - like zsh, ``coproc [pipeline]``
- **Breaking Change**: in command argument, perform tilde expansion after ``=``
    ```shell
    dd if=~ of=/somewhere   # expand 'if=~' to 'if=$HOME'
    ```
    - also perform file name completion after ``=``
    - except for redirection target and source path
- **Breaking Change**: change command substitution behavior
    - always disable job-control in command substitution
    - propagate IO error as ``SystemError``
    - cancel command substitution by SIGINT
- **Breaking Change**: during sub-shell creation, not clear job entry
    - for builtin ``jobs`` command within sub-shell
- **Breaking Change**: after background job termination, not show signal message
    - not show signal messages after call ``Job#poll``, ``Job#wait``, builtin wait command
- **Breaking Change**: remove multi-return type due to unused
- **Breaking Change**: disable job control within subshell even if monitor option is enabled
- show job information via CTRL-Z/``fg``/``bg``
- escape unprintable character when throw some errors
- improve method lookup error messages
- change actual name of ``Boolean``, ``UnixFD`` type
    - ``Boolean`` -> ``Bool``
    - ``UnixFD`` -> ``FD``
    - old type name is still valid name (now defined as type alias)

#### Builtin

- **Breaking Change**: change signal related methods
    - remove ``Signals#[]=``, ``Signals#signal`` methods
    - add ``Signals#[]``, ``Signals#get`` methods for get corresponding signal
        - if corresponding signal is not found, ``Signals#[]`` method throws``KeyNotFoundError``
    - add ``Signal#trap`` method for get and set signal handler corresponding to signal
- **Breaking Change**: replace invalid utf8 byte with replacement character (U+FFFD) in the following
  String methods
    - ``charAt``
    - ``chars``
    - ``width``
    - iterator
- **Breaking Change**: remove ``String#to``, ``String#from``, ``Array#to``, ``Array#from`` methods
    - use slice method instead
- **Breaking Change**: ``Regex#replace`` method internally use ``pcre2_substitute`` api
    - now expand meta characters during replacement
- **Breaking Change**: ``Regex`` methods throw ``RegexMatchError`` instead of ``InvalidOperationError``
- **Breaking Change**: change ``String#toInt`` method behavior with other programming languages such golang, java
    - now ``'0xFFFFFFFFFFFFFFFF'.toInt()`` is out-of-range
- **Breaking Change**: rename ``isNan`` method with ``isNaN`` in ``Float`` type
- now some builtin variables are constants
    - ``ON_ASSERT``, ``ON_ERR``, ``ON_EXIT``
    - ``TRUE``, ``True``, ``true``
    - ``FALSE``, ``False``, ``false``
- for optional argument, change last parameter type with Option type in the following builtin methods
    - ``Regex#init``
    - ``String#slice``
    - ``Array#slice``
    - ``Array#join``
- specify east-asian width to ``String#width`` method
- specify start index to ``String#indexOf``, ``Array#indexOf`` methods
- specify radix to ``String#toInt`` method

#### API

- **Breaking Change**: change interface of ``DSState_complete``
    - now get completion candidates from ``DSState_getCompletion`` api
    - add ``DS_COMP_ATTR_NOSPACE`` attribute
- **Breaking Change**: change interface of ``DSState_lineEdit``
    - introduce ``DSLineEdit`` struct
- after ``DSState_createWithMode``, set ``LC_NUMERIC`` to ``C``

#### Interactive

- adjust space insertion behavior after inserting completion item
- ``CTRL-W`` op (delete previous word) is now unicode-aware
    - follow unicode word boundary
- support ``M-b``, ``M-f``, ``M-d``

#### Module

- **Breaking Change**: remove completion-wrapper. now use foreign ``bcrun`` instead
    - now support ``bash-completion``-aware completion scripts

### Fixed

- invalid string in ast dump
- not propagate unreachable code error from loaded module
- incorrect line number after call ``DSState_loadModule`` api
- not perform file name completion after ``:``
- broken hex escape sequence handling in dollar string and echo command
- line marker of nested pipeline
- broken lexer state in prefix assignment parsing
- ``Broken Pipe`` signal message after evaluation of last-pipe
- infix keyword completion
- PATH handling of sudo completion
- length error of brace expansion
- broken float number parsing (prefix spaces, locale dependent-format)

## [0.26.0] - 2022-06-30

### Added

#### Core

- allow anonymous function in local scope
    - can access upper scope variables (except for fields, temporary environmental variables)
- support brace expansion in command argument list and source statement
- add ``xtrace`` runtime option
    - trace execution of commands
    - also support ``-x`` command line option
- add experimental ``errraise`` runtime option
    - in statement context, if exit status of command is non-zero, raise ``ExecError``

#### Builtin

- allow ``UnixFD`` type in ``for-in`` expression
    - read each lines during iteration (for more efficient alternative of while-read pattern)
    ```
    ls | for $e in $STDIN {
      echo $e
    }
    ```
- pass module context (module descriptor) to user-defined completer (``COMP_HOOK``)
    - also pass module context to builtin ``complete`` command via ``-m`` option
- add the following methods to ``Error`` type
    - ``lineno``: get line number of occurred location
    - ``source``: get source name of occurred location

#### LSP

- support the following requests
    - ``textDocument/documentHighlight``
    - ``textDocument/documentLink``
    - ``textDocument/documentSymbol``

#### Misc

- add standalone syntax highlighter called ``dscolorize``
    - support the following output formats
        - ANSI color codes for true-color terminal, 256-color terminal
        - HTML

### Changed

#### Core

- **Breaking Change**: clear exit status when enter catch block
- cancel runtime glob/brace expansion by SIGINT
- cancel runtime code completion by SIGINT
- show sub-shell level when handle uncaught exception
- deprecate ``import-env``, ``export-env`` keywords. now use ``importenv``, ``exportenv`` instead.
- omit ``$`` sigil in parameter declarations (catch, function, constructor, for-in)
- improve token format error messages

#### Builtin

- **Breaking Change**: remove ``Array#forEach`` method due to unused
- **Breaking Change**: throw ``InvalidOperationError`` when call ``Module#_func`` method within user-defined completer
- **Breaking Change**: change error message of ``SystemError``
- **Breaking Change**: remove ``shctl show`` sub-command
    - now use ``shctl set`` sub-command instead
- add some options to ``shctl set`` sub-command
    - ``-d`` option for dumping current runtime options
    - ``-r`` option for restoring runtime options from dump
    - if options is not specified, show current runtime option setting
- ``shctl module`` sub-command now finds and prints full path of specified modules
- throw ``ArithmeticError`` when call ``Int#abs``, ``-`` to INT_MIN

#### LSP

- **Breaking Change**: change config section names in ``workspace/didChangeConfiguration``
- can disable semantic highlight

#### Module

- **Breaking Change**: pass completion context to completer module

#### Misc

- temporary disable 32bit support

### Fixed

- revert 'linenoise io error checking' due to broken error reporting
- directory detection of completion wrapper
- return type resolution of anonymous function that last statement is Nothing type
- file name completion in prefix assignment
- invalid ``SCRIPT_DIR`` in module name completion
- object destruction order of tuple, user-defined type, closure
- ``DSSatte_lineEdit`` does not set default prompt when internal ``EDIT_HOOK`` throw error
- emit diagnostics to wrong textDocument
- broken ``CHECK_RE_IF``, ``CHECKERR_RE_IF`` parsing
- invalid user-defined type name format in hover
- ``$`` in double-quoted string literal

## [0.25.0] - 2022-03-31

### Added

#### Core

- define no-return user-defined command
    - type of non-return user-defined command is ``Nothing``
  ```
  usage() : Nothing {
    echo 1>&2 [usage] $@
    exit 2
  }

  $1 :- usage require argument
  ```
- define custom error type like the follow
  ```
  typedef LogicError : Error
  assert (new LogicError("hello") is Error)
  ```
- support user-defined type
  ```
  typedef IntList($v : Int) {
    var value = $v
    var next = new IntList!()
  }
  ```
- support user-defined method
    - define method for arbitrary types (except for ``Void``, ``Nothing``) in current module scope
        - access receiver via ``this`` variable
    - also, lookup methods defined for super type (such as ``Any``, ``Error``)
    - in method call syntax, if field and method have the same name, give priority to method
  ```
  function factorial() : Int for Int {
    return $this == 0 ? 1 : $this * ($this - 1).factorial()
  }
  10.factorial()
  ```
- when call uninitialized method/constructor, throw ``IllegalAccessError``
- add ``defer`` statement
    - like swift, ``defer`` statement evaluated in end of scope (block, function, user-defined command)
    - preserve exit status during the evaluation of defer statement

#### Builtin

- add the following builtin constants
    - ``DATA_HOME``: indicates ``XDG_DATA_HOME/ydsh``
    - ``CONFIG_HOME``: indicates ``XDG_CONFIG_HOME/ydsh``
    - ``MODULE_HOME``: indicates ``XDG_DATA_HOME/ydsh/module``

#### API

- add ``DSState_config`` api for runtime system configuration query

#### LSP

- support the following methods/notifications
    - ``workspace/didChangeConfiguration``
        - now change server configuration at runtime
        - change command name/command argument completion setting
    - ``textDocument/semanticTokens/full``

#### Misc

- add ``CHECK_RE_IF``, ``CHECKERR_RE_IF`` directive to ``litecheck``

### Changed

#### Core

- **Breaking Change**: finally-less try expression is now syntax error (previously semantic error)
- **Breaking Change**: change typechecking of parameter expansion
    - change error message when pass ``Option<T>`` to command arguments
    - do not allow concatenation of ``Any`` type expression
- **Breaking Change**: change invalid value handling of string interpolation/parameter expansion
    - if contain invalid values, just ignore theme
- **Breaking Change**: change string interpolation/parameter expansion of ``Map`` type
    - expand like ``Array``, ``Tuple`` type
- **Breaking Change**: now follow XDG Base Directory Specification
    - now local module directory (aka ``MODULE_HOME``) indicates ``XDG_DATA_HOME/ydsh/module``
- improve error reporting
    - module private member access
    - read-only symbol/field access
    - no-return expression checking in finally-block
    - illegal concatenation of parameter expansion
    - only available in global scope
- preserve exit status during the evaluation of finally-block
- complete type template, such as ``Array``, ``Map``, ``Tuple``, ``Option``
- do not complete methods that do not satisfy type constraints

#### Builtin

- **Breaking Change**: change all method name of ``Module`` type due to prevent potential name conflict
    - ``fullname`` to ``_fullname``
    - ``func`` to ``_func``
    - ``scriptDir`` to ``_scriptDir``
    - ``scriptName`` to ``_scriptName``

#### Interactive

- **Breaking Change**: change default rcfile path to ``DATA_HOME/ydshrc``

#### Module

- **Breaking Change**: in ``edit`` module, change default ``HISTFILE`` to ``DATA_HOME/ydsh_history``

#### Completion

- brew: fix bash completion script path

### Fixed

- type error reporting of tuple/func type creation if size of these elements reaches limit
- ``textDocument/Hover``, ``textDocument/definition`` and ``textDocument/references`` do not work in large files
- broken code generation of named imported env variables
- broken parameter expansion of ``[UnixFD]`` type. previously the following code is failed
  ```
  assert diff ${[<(ls), <(ls)]}
  ```
- cannot define type alias for ``Void``, ``Nothing``
- error line marker of ``assert`` statement
- potential operand stack corruption when use ``break`` or ``continue`` expression within call arguments
- return status of ``DSState_loadModule`` api when detect symbol conflicts
- negative number or out-of-range number handling of ``SHLVL`` in startup time
- out-of-range unicode handling in dollar string and echo command
- broken code generation of finally-block within nested try-loop
- broken invisible character escaping of command arguments

## [0.24.0] - 2021-12-28

### Added

#### Core

- add the following runtime options
    - ``huponexit``: if on, when call ``exit`` command, send ``SIGHUP`` to managed jobs
    - ``assert``: if on, check assertion
        - now assertion is enabled/disabled at runtime
- support anonymous function
    - define anonymous function like the follow
      ```
      function($a : Int) => $a * $a
      ```
    - currently, only allow top-level scope
- add runtime compilation api
    - compile string (single expression) as anonymous function via ``Module#func`` method
    - compiled function can access global variables visible in receiver module
- complete infix keywords
    - `as`, `is`, `and`, `or`, `xor`, `with`
    - `in`, `elif`, `else`, `catch`, `finally`, `inlined`
- add subtype relation of func type
    - if `T0 <: T1`, `T2 <: T3` then `(T1) -> T2 <: (T0) -> T3`

#### Builtin

- add ``info`` sub-command to builtin ``shctl`` command
    - now show runtime configuration (also get via ``reply`` variable)
- add builtin ``MODULE`` variable for indicating current ``Module`` object
- add the following methods to ``Module`` type
    - ``scriptName``: get ``SCRIPT_NAME`` of module
    - ``scriptDir``: get ``SCRIPT_DIR`` of module
    - ``func``: compile string as single expression function
    - ``fullname``: resolve fully qualified command name
- add the following methods to ``String`` type
    - ``width``: count width of grapheme clusters
    - ``contains``: check if contains substring
- add the following methods to ``Array`` type
    - ``forEach``: apply function to each element
    - ``addAll``: add all elements of other ``Array`` object
    - ``indexOf``: get first index of element equivalent to specified object
    - ``lastIndexOf``: get last index of element equivalent to specified object
    - ``contains``: check if contains specified object
- add the following methods to ``Map`` type
    - ``addAll``: add all elements of other ``Map`` object
    - ``putIfAbsent``: put value if key does not found

#### LSP

- support the following methods/notifications
    - ``textDocument/publishDiagnostics``
    - ``textDocument/completion``

#### Misc

- add ``litecheck`` file checker like LLVM lit/FileCheck or littlecheck
    - support the following directives
        - ``RUN``
        - ``REQUIRE``
        - ``CHECK``, ``CHECK_IF``, ``CHECK_RE``
        - ``CHECKERR``, ``CHECKERR_IF``, ``CHECKERR_RE``
        - ``STATUS``
- experimental support linux on arm32

### Changed

#### Core

- **Breaking Change**: change exit status of command error
    - if command not found, set exit status to 127
    - if permission error, set exit status to 126
- **Breaking Change**: change operator precedence of ``throw`` expression.
    - now the precedence is equivalent to ``return``
- set ``PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK`` option if pcre2 10.38 or later
- remove redundant signal handler installation when recursively call interpreter
- not change signal handler for ``SIGKILL``, ``SIGSTOP``
    - internal sigaction does not accept these signals

#### Builtin

- **Breaking Change**: remove ``fullname`` subcommand of ``shctl``
    - now use ``Module#fullname`` method instead
- **Breaking Change**: slice methods of ``String``, ``Array`` type no longer raise any exceptions
    - like python, if slice index is out of range, round index within range
- **Breaking Change**: remove some ``Array``, ``Map`` methods
    - ``Array#extend``: use ``Array#addAll`` instead
    - ``Map#find``: use ``Map#get`` instead
    - ``Map#default``: use ``Map#putIfAbsent`` instead
- **Breaking Change**: rename ``Error#backtrace`` method with ``Error#show``

#### LSP

- reduce every-time rebuild per ``textDocument/didChange``
    - now build tasks run in background worker
- improve ``textDocument/hover``
    - support builtin variables
    - support tuple fields
    - support builtin methods
    - show command descriptions

### Fixed

- ``is-sourced`` sub-command of ``shctl``
- accidentally skip termination handler in loaded module
- not ignore non-regular files in file path search
- not complete statement when previous token is newline
- segmentation fault when invalid compare functions are supplied to ``Array#sortWith`` method
    - now replace internal ``std::stable_sort`` with merge sort
- cannot parse ``Float`` literal like `34.2e00`
- abort if error line has invisible characters
- length checking of pipeline

## [0.23.0] - 2021-09-30

### Added

#### Core

- ``source`` statement support inlined import
    - inlined imported global symbols are transitively imported from other modules
  ```
  source edit inlined   # module 'edit' is inlined imported
  ```

#### Builtin

- add ``Float#compare`` method
    - total order comparison function equivalent to Java (Double.compare)

#### Interactive

- auto-detect emoji sequence width before prompt rendering

#### LSP

- basic support the following methods/notifications
    - ``textDocument/didOpen``
    - ``textDocument/didChange``
    - ``textDocument/didClose``
    - ``textDocument/definition``
    - ``textDocument/references``
    - ``textDocument/hover``

### Changed

#### Core

- **Breaking Change**: fix error location in the following statement
    - variable declaration
    - function definition
    - command definition
    - type alias definition
    - source statement
- **Breaking Change**: statically determine user-defined command
    - eliminate runtime user-defined command lookup
    - disallow user-defined command call defined in backward of the call-site
- **Breaking Change**: change internal hash/equality function of ``Map<Float, T>`` object
    - now check equality by total order
    - change hash code
        - hash(-0.0) != hash(0.0)
        - hash(NAN) == hash(NAN)
- **Breaking Change**: fix string representation of ``Float`` object
    - ``inf`` => ``Infinity``
    - ``-inf`` => ``-Infinity``
    - ``nan`` => ``NaN``

#### Misc

- require CMake 3.8 or later

### Fixed

- infix keyword parsing in the following
    - `in`, `as`, `is`, `and`, `or`, `xor`, `with`, `elif`
- in completion, ignore keywords/commands starting with identifier if previous token is the following
    - `in`, `and`, `or`, `xor`, `elif`
- do not complete if previous token is a comment
- error message of for-expression
- raise error when access loaded module via ``DSState_loadModule`` api

## [0.22.0] - 2021-06-27

### Added

#### Core

- dollar string literal support unicode escape sequence (\u \U)

#### Builtin

- add ``String#chars`` method
    - split string as grapheme cluster array
- ``echo`` command support unicode escape sequence (\u \U)
- add builtin ``wait`` command
    - also support ``-n`` option

### Changed

#### Core

- **Breaking Change**: disallow signal sending to sibling jobs
- **Breaking Change**: disowned job object still maintains valid job id
    - job table still maintains job id of disowned job
    - job id of disowned job is no longer reassigned to newly attached job
- **Breaking Change**: escape sequence handling in dollar string literal
    - hex escape sequence (\xHH) require 1~2 hex digits (not exactly 2 hex digits)
    - octal escape sequence (\xnnn) require 1~3 octal digits (not exactly 3 octal digits)

#### Builtin

- **Breaking Change**: return value of ``Job#pid`` method
- **Breaking Change**: some string method handle grapheme cluster
    - ``String#count`` count grapheme clusters in string
    - ``String#charAt`` get grapheme cluster at specified position
    - iterate grapheme cluster in string
- **Breaking Change**: after call ``Job#wait``, not close internal fds
- propagate intrenal error as exception from regex method
    - ``left =~ right``, ``left !~ right``
    - ``Regex#match``, ``Regex#replace``
- use stable sort in ``Array#sortWith`` method

#### Misc

- change minimum required compiler version for gnu++17 support
    - gcc 7
    - clang 6
- update re2c to 2.1.1
- reactivate ydshd installation

### Fixed

- ``Job#status`` method return correct exit status when internal process already waited
- after call ``Job#poll`` method, if job is terminated, removed from job table
- remove redundant '/' in file name completion

## [0.21.0] - 2021-03-28

### Added

#### Core

- introduce fully qualified command name
    - builtin ``eval`` command can accept fully qualified command name
    - builtin ``command`` command support fully qualified command name
- add PowerShell like array literal
    - ``@( )``

#### Builtin

- add ``fullname`` subcommand to builtin ``shctl``
    - resolve fully qualified command name from specified module

#### Completion

- add and change the following completions
    - brew
    - sudo

### Changed

#### Core

- **Breaking Change**: not access private builtin variables
    - ``_cmd_fallback_handler``
    - ``_DEF_SIGINT``
- **Breaking Change**: source name of builtin variable
    - ``(embed)`` to ``(builtin)``
- **Breaking Change**: not skip last spaces when mismatched token is EOS
- **Breaking Change**: when access uninitialized user-defined command, throw ``IllegalAccessError``
- not complete hidden variables
- support completion in prefix assignment
- improve parser error message
    - remove meaningless error message when reach end of string
    - quote expected tokens
    - replace some no viable alternative error messages with more ituitive ones
    - show number of characters in error line
    - constructor lookup error message
- escape unprintable character when show command error message
- ignore newline within the following parenthesis
    - ``()``, ``[]``, ``${}``, ``$()``, ``@()``, ``<()``, ``>()``

#### Builtin

- **Breaking Change**: now use PCRE2
    - use ``PCRE2_ALT_BSUX | PCRE2_MATCH_UNSET_BACKREF | PCRE2_UTF | PCRE2_UCP`` option
    - some char-classes such as ``\s``, ``\w`` matches unicode characters
- builtin ``command`` and ``shctl fullname`` check uninitialized user-defined command

#### API

- **Breaking Change**: ``DSError`` maintains ``chars``

### Fixed

- not show error line marker when reach EOS and previous token size is 1
- not ignore null character in the following
    - subcommand
    - builtin ``command``
    - builtin ``exec``
- error line marker
    - constructor param type checking
    - variable declaration with type

## [0.20.1] - 2021-02-7

### Fixed

- bugfix ``Regex#replace`` method when replce with empty string
- segv when call uninitialized user-defined command in interactive mode

## [0.20.0] - 2020-12-31

### Added

#### Core

- allow function call in command arguments
    - ex. ``echo $func(34, "hey")``
- module/scope aware type alias
    - at named module import, implicity define module type alias
      ```
      source path as Path
      assert $Path is Path
      ```
    - define type alias in local scope
    - access type name defined in module
      ```
      source path as Path
      assert $COMP_HOOK! is Path.Completer
      ```
- semantic aware completion
    - complete field/method name
    - complete local variable name
    - complete type name
    - complete user-defined command name from current scope
    - complete subcommand name
- support bash style prefix assignment
    - defined environmental variables in the following expression scope
      ```
      IFS="" $(ls)
      ```
    - if no following expression, treat as just assignment
      ```
      IFS='AAA'
      ```
    - if right hand side expression contains ':~', expand tilde
      ```
      PATH=${PATH}:~/bin   # equivalent to ${PATH}:/home/${USER}/bin
      ```

#### Builtin

- add ``Int`` method
    - ``abs``
- add some ``Float`` methods
    - ``abs``
    - ``round``
    - ``floor``
    - ``ceil``
    - ``trunc``
    - ``isNormal``

#### Module

- add ``tilde`` function to ``path`` module
    - perform tilde expansion for arbitrary string

#### Completion

- add completions for
    - printenv
    - which
    - zypper
    - perf
- bash completion wrapper

### Changed

#### Core

- **Breaking Change**: use ``typedef`` keyword for type alias definition
    - ``alias`` keyword is still reserved keyword for future usage
- **Breaking Change**: when access undefined environmental variable, throw ``IllegalAccessError`` instead
  of ``SystemError``
- **Breaking Change**: evaluate script within separate module context
- **Breaking Change**: after runtime error, not discard symbol state
- **Breaking Change**: when modifying map object during iteration, always throw ``InvalidOperationError``
- **Breaking Change**: eliminate implicit tilde expansion like the follow
    - external command name completion
    - external command file path search
- allow ``=>`` in abbreviate type notation of Func type
- allow ``->`` in arm expression
- allow statement in for-init
- reimplement all completions on CodeCompletionHandler
- when access uninitialized global variable, throw ``IllegalAccessError``
    - after uncaught error happened in interactive mode, uninitialized variables may exist

#### Builtin

- **Breaking Change**: output format of ``shctl module`` subcommand
- **Breaking Change**: ``Float#toInt`` method behavior
    - previously undefined behavior, but now is the same way as Java
- allow negative number index in string ``[]``

#### API

- **Breaking Change**: ``DSState_loadAndEval`` api does not accept null file name
- **Breaking Change**: ``DSState_loadModule`` api evaluate script in root module context
    - when specified by ``DS_MOD_SEPARATE_CTX``, evaluate script in separate module context
    - may report ``DS_ERROR_KIND_FILE_ERROR``

### Fixed

- cannot load module when module path indicates anonymous pipe
- in user-defined command, cannot pass ``UnixFD`` object to external command
- code generation of ``finally`` block
- cannot save history when ~/.ydsh_history does not exist
- stack consumption of ``APPEND_MAP`` ins
- not treat escaped newline as space
- command argument parsing when following token is ``(``

## [0.19.1] - 2020-09-22

### Fixed

- remove ``cmd`` from test case for cygwin
- escape characters in env name completer
- escape handling in completer when charcter has already escaped

## [0.19.0] - 2020-09-20

### Added

#### Core

- add ``??=`` operator for Option type variable
    - ``left ??= right`` if ``left`` is invalid option value, assign ``right`` to ``left``
- ``Regex`` literal supports ``s`` flag
    - ``.`` character matches newline
- allow ``Regex`` constructor in constant expression
- introduce ``fastglob`` option
    - breaking traditional glob behavior, but takes more efficient directory search strategy
- module aware user-defined command lookup
    - command starting with ``_`` will be private command. private command is only called from its own module
    - in named import, call command defined in module as sub-command

#### Builtin

- add ``status`` method to ``Job`` type
    - get exit status of child processes in job
- add ``replace`` method to ``Regex`` type
    - replace all of matched strings
- add ``module`` sub-command to builtin ``shctl`` command
    - get full path of loaded script or module
- add builtin ``_exit`` command for force program termination without cleanup
    - call ``_exit`` function internally
- builtin ``complete`` command supports ``-A`` option
    - expose internal completion function
- add the following builtin variable
    - ``DATA_DIR``: indicate ``datadir/ydsh``, ex. /usr/share/ydsh
    - ``MODULE_DIR``: indicate system module directory, equivalent to ``$DATA_DIR/module``
    - ``YDSH_BIN``: indicate self executable path (in linux ``/proc/self/exe``)
        - if empty string, may be used as shared library

#### Completion

- add completions for
    - builtin commands
        - shctl
        - cd
        - pwd
        - complete
        - kill
        - command
        - eval
        - exec
    - git
    - ninja
    - fusermount
    - ydsh
    - sudo

#### API

- add ``DSState_initExecutablePath`` for set full path of current executable to ``YDSH_BIN``

### Changed

#### Core

- **Breaking Change**: perform regex syntax checking in typechecker. now regex syntax error is semantic error
- **Breaking Change**: not ignore previously raised exception in finally block
- **Breaking Change**: also enter finally block in exit or assertion failure
- **Breaking Change**: change install directory structure
    - ``share/ydsh``: system wide architecture-independent data
    - ``share/ydsh/module``: system modules
    - ``share/ydsh/completion``: completion modules
- glob in source statement always use ``fastglob`` mode

#### Builtin

- **Breaking Change**: ``Regex`` type constructor needs flag as second argument
    - ``new Regex('abc', 'im')``
- **Breaking Change**: remove ``CONFIG_DIR`` variable
- null character handling in builtin method/builtin command
    - ``Regex`` constructor does not accept strings having null characters
    - ``UnixFD`` constructor does not accept strings having null characters
    - not ignore null characters in builtin commands

#### Module

- ``completion``
    - when command is not found, does not kick corresponding completer
    - add ``compdef`` command for defining completer by decralative way

#### API

- **Breaking Change**: rename some public api
    - ``DSState_getExitStatus`` to ``DSState_exitStatus``
    - ``DSState_completionOp`` to ``DSState_complete``
    - ``DSState_lineEditOp`` to ``DSState_lineEdit``
- **Breaking Change**: return status of public api
- **Breaking Change**: change ``unsigned short`` to ``unsigned int``
- **Breaking Change**: remove ``DSState_configDir`` api

### Fixed

#### Core

- ``..`` pattern cannot match empty directory.
- always set new ``COMPREPLY`` variable even if completion result is empty
- not escape backslash in completer
- common super type resolution of case expression

#### Builtin

- fix executable file checking in ``command -v`` option
    - always ignore directoy
- segv in ``is-sourced`` sub-command of ``shctl``

#### API

- when abort symbol table, also abort loaded script path
- not crash public api when ``DSState`` parameter is null

## [0.18.2] - 2020-07-04

### Fixed

- cannot load multiple globbed modules when source statement on end of file

## [0.18.1] - 2020-06-20

### Fixed

- fix ``CONFIG_DIR`` variable path in RPM package

## [0.18.0] - 2020-06-20

### Added

#### Core

- more optimize DSObject memory layout
- add parameter expansion like binary string operator
    - ``left :- right``
        - if ``left`` is empty string, evaluate ``right``
    - ``left := right``
        - if ``left`` is empty string, assign ``right`` to ``left``
- support glob expansion in command arguments
    - only support ``?`` and ``*``
    - add ``nullglob`` and ``dotglob`` options
- relax syntax restriction of source statement, case expression
- improve constant expression
    - allow the following builtin variables in constant expression
        - ``YDSH_VERSION``
        - ``CONFIG_DIR``
        - ``SCRIPT_DIR``
        - ``SCRIPT_NAME``
        - ``OSTYPE``
        - ``MACHTYPE``
    - allow integer unary operator
    - string interpolation
- allow glob expansion in source statement
- ``break``, ``continue``, ``return`` are treated as expression
- eliminate fork of command or pipeline in command/process substitution, coproc, background job
- propagate ``SIGINT`` as SystemError in interactive mode
- report code generation error

#### Builtin

- add the following cast methods
    - ``Int#toFloat``
    - ``Float#toInt``
- add ``show``, ``set``, ``unset`` subcommand to builtin ``shctl``
    - support the following options
        - ``traceonexit``
        - ``monitor``
        - ``nullglob``
        - ``dotglob``
- add ``SCRIPT_NAME`` variable
    - indicating currently evaluating module script name
- builtin test command supports binary file operators, ``-ef``, ``-nt``, ``-ot``

#### API

- add ``DS_ERROR_KIND_CODEGEN_ERROR`` for code generation error reporting

### Changed

- ast dumper format
- type check error message
- remove ``--print-toplevel`` option
- remove ``DSState_setScriptDir`` api
- ``SCRIPT_DIR`` variable indicates currently evaluating module script directory
- not perform tilde expansion in the following public api
    - ``DSState_loadAndEval``
    - ``DSState_loadModule``
- environmental variable update rules at startup
    - change ``PWD`` / ``OLDPWD`` update rules at startup
        - if ``PWD`` is not set / not full path / not existing directory, set ``PWD`` to cwd
        - if cwd is removed, set ``PWD`` to ``.``
        - if ``OLDPWD`` is not set / not full path / not existing directory, set ``OLDPWD`` to ``PWD``
    - always set valid value to ``HOME``, ``LOGNAME``, ``USER``
- tilde expansion behavior
    - ``~+``, ``~-`` is not expanded when ``PWD`` / ``OLDPWD`` is invalid
    - use ``HOME`` env in ``~`` if env is set
- source statement does not allow null characters
- when specified ``--parse-only`` option, not perform module loading
- does not always handle/ignore ``SIGBUS``, ``SIGSEGV``, ``SIGILL``, ``SIGFPE`` signals due to undefined behavior
- operator precedence of ``throw`` expression

#### Misc

- improve AArch64 support (on Raspberry Pi 4)
    - reactivate some test cases in AArch64 build
- experimental support x86
    - tested in ubuntu bionic x86 in docker container
- add build script for UBSAN
- improve LTO support
- reactivate RPM debuginfo build

#### Build Requirement

- CMake 3.0 or later

### Fixed

- infinite loop of interactive mode in AArch64 build
- unnecessary module search in ``DSState_loadModule`` api specified by ``DS_MOD_FULLPATH``
- byte code dump of module
- module name completion when cwd is changed
- module loading when cwd is removed
- when load RC file, ``DS_OPTION_*`` are not set yet
- not expand symbolic link in module loading
- Ctrl-C handling in interactive mode

## [0.17.0] - 2020-03-31

### Added

#### Core

- merge ``Int64`` and ``Int32`` type into ``Int``
    - remove ``Int64`` related method
    - replace ``Int64`` with ``Int``
    - ``Int`` type represents ``int64_t``
- simplify variable declaration with constructor call
  ``var a = new T()`` to ``var a : T``
- introduce inlined object for avoiding small object allocation
    - ``Boolean``
    - ``Signal``
    - ``Int``
    - ``Float``
    - small string (up to 14 characters)
- optimize string concatenation
    - simplify code generation
    - avoids unnecessary memory allocation

#### API

- ``DSState_*`` api return ``$? & 0xFF``

#### Builtin

- add ``shctl`` command for runtime query/setting
- ``setenv`` command showes all environmental variables when has no args

#### Interactive

- before show prompt, insert newline when previous line is not terminated with newline
- auto-detect east asian ambiguous character width
- auto sync window size

### Changed

- drop support ``Int32`` type
- drop support ``Int64``, ``Long`` type
- drop support Int64 literal suffixed with ``l L``
- change ``-n``option behavior to the same as ``--compile-only`` option
- rename builtin *_env familly
    - ``check_env`` to ``checkenv``
    - ``set_env`` to ``setenv``
    - ``unset_env`` to ``unsetenv``
- change return type of ``Regex#match`` method
- change exit status of builtin ``exit`` command
    - parsed exit status is always ``status & 0xFF``
- merge ``String#toInt32`` and ``toInt64`` into ``toInt``

### Fixed

- newline handling in interactive mode
- SEGV when access aborted MethodHandle
- line number of command
- add missing error check to ``String#replace`` method

## [0.16.0] - 2019-12-31

### Added

#### Core

- support optional module import
    - use 'source!' keyword
- support type constraints for builtin method by 'where' keyword
- introduce abbreviated type notation of Func type
- allow last comma in multi element tuple literal
- add 'CMD_FALLBACK' for command-not-found handling

#### Builtin

- remove 'ps_intrp' command
- overhaul String#slice, String#to, String#from, Array#slice, Array#to, Array#from methods
    - allow start index equivalent to size
- disallow String#sort method when type parameter is not Value type

#### API

- merge 'DSState_prompt' with 'DSState_lineEditOp'
- drop 'varName' paramter from 'DSState_loadModule'

#### Module

- rename 'history' module with 'edit' module
- move prompt rendering function into 'prompt' module
    - add 'renderPrompt' function for bash style prompt rendering
    - add 'prompt' command for replacement of 'ps_intrp'
- add 'path' module
    - 'dirname'
    - 'basename'
    - 'home'
    - 'user'
- add 'PROMPT_HOOK' variable for primary prompt rendering
- add 'PROMPT_RENDERER' variable for custom prompt renderer
- add 'cnf' module

#### Language Server

- improve error message of json validation
- support the following LSP method/notification
    - initialized

### Changed

- PS1 and PS2 variables are no longer builtin, now defined in 'edit' module
- use abbreviated type notation in string representation
- string representation of single element tuple object
- toplevel printing format

#### Misc

- now install experimental 'ydshd' by default
- experimental support Linux AArch64

### Fixed

- history saving when HISTFILESIZE is less than HISTSIZE
- ignore module loading error when specify DS_MOD_IGNORE_ENOENT
- SEGV in String#join method
- not allow null characters in regex literal

## [0.15.0] - 2019-09-26

### Added

#### Core

- add builtin 'CONFIG_DIR' variable for indicating system config directory
- add builtin 'PIPESTATUS' variable for indicating the latest status of pipeline
- add builtin 'COMP_HOOK' variable for user-defined completer
- add builtin 'EDIT_HOOK' variable for user-defined line editing function
    - support CTRL-R for history search
- drop support Byte, Int16, Uint16 type
- change integer literal syntax
    - Int64 literal ends with 'l', 'L'
    - octal number starts with '0', '0O'
    - hex number starts with '0X'
- throw ArithmeticError when detect integer overflow
- auto unwrap option type value in case expression

#### Completer

- complete module name from module loading path
- correctly handle tilde expansion of file name completion
- support user-defined completer
- set completion result to 'COMPREPLY' variable
- complete keyword
- complete space when previous token is typing

#### Builtin

- add 'copy' method to Array type
- add 'copy' method to Map type
- add builtin umask command
- allow negative number index in some Array type method
- test command correctly handle null character
- toInt32, toInt64 supports octal number starts with '0', '0O' and hex number starts with '0X'

#### API

- replace history api with 'DSState_lineEditOp'
- replace completion api with 'DSState_completionOp'

### Changed

- string representation of Regex type
- max number of pipe chain is up to 250
- job table maintains enclosed command of process substitution
- not inherit parent process signal handler
- not allow user-defined signal handler for SIGBUS
- operator precedence
    - null coalescing is right associativity
    - throw expression
    - coproc
- builtin 'REPLY' variable is writable
- drop support integer literal ended with 'i32', 'i64', '_i32', '_i64'

### Fixed

- file descriptor leak after execve
- unicode handling in linenoise completion
- process group of enclosed command in command substitution
- history loading
- '&', '&!', '&|' token parsing
- not terminate subshell in some internal vm api
- line continuation checking
- 'typeof' parsing
- unprintable character handling in completer
- stack allocation of user-defined command invocation
- infinite loop of encoding function
- SIGSTOP/SIGTSTP handling of command substitution
- typechecking of case expression
- disable top level printing in module

## [0.14.0] - 2019-06-28

### Added

#### Core

- improve module system
    - private member support in module
        - variable and function name starts with underscore is private member in module
    - more stabilize
    - dump module
- introduce history module
    - expose history buffer to HISTORY variable
    - move some history related variables into module
    - user-defined history command
    - add HISTIGNORE

#### Builtin

- add 'join' method to Array

#### API

- add 'DSState_mode' function for execution mode inspection
- add 'DSState_getExitStatus' and 'DSState_setExitStatus'
- remove DS_OPTION_HISTORY
- rewrite history related api
- 'DSState_exec' can execute user-defined command and external command

### Changed

- '$?' is writable
- remove history related variable
    - HISTSIZE
    - HISTFILE
    - HISTFILESIZE
    - HISTCMD
- remove builtin history command
- no longer clear termination handlers after call _defaultHook

### Fixed

- typechecking of Array, Map, Tuple literal
- Ctrl-D handling in '--parse-only', '--check-only', '--compile-only' mode
- variable name completion
- End of String handling
- default pattern handling of case expression
- module loading
- line number of module
- exec_test runner

## [0.13.0] - 2019-03-28

### Added

#### Core

- Signal type is used in Map key
- improve case-expression
    - support Signal literal
    - support double quoted string literal
    - support Regex literal
- set environmental variable USER by default
- if ENOEXEC error happened in command execution, fallback to '/bin/sh'
- cleanup internal vm api
    - introduce 'callMethod' and 'callFunction' api

#### Builtin

- add some Array type methods
    - shift
    - unshift
    - reverse
    - sort
    - sortWith
- add 'message' method to Signal type
    - wrapper for 'strsignal'
- rewrite OP_STR, OP_INTERP, OP_CMD_ARG
    - use callMethod api

#### API

- improve error handling of script/module loading

#### Completor

- improve file name completion in 'with', 'source' keyword
- complete environmental variable names
- complete command name when previous token is '&', '&!', '&|'

#### Misc

- support CPackRPM
- support platform detection in test directive

### Changed

- not show signal terminated message in mid-pipeline
- debug logger format
- error message of circular reference error
- last pipe may cause SIGPIPE with child process
- delimiter handling of split method in String type

#### Build Requirements

- require gnu++14 support
    - gcc 5 or later
    - clang 3.6 or later
- cmake 3.7 or later

### Fixed

- case expression parsing
- segmentation fault in code completion
    - assert, import-env
- type checking of last pipe
- null character handling of io here
- null character handling of toplevel printing

## [0.12.0] - 2018-12-31

### Added

#### Core

- propagate fork-failure as SystemError
- allow nested option type
    - nested option type is simplified to single option type ex. T!! -> T!
    - break expression returns option type value
- add case-expression
    - use 'case' keyword
    - support the following pattern
        - string
        - int
- show signal message when terminated by signal

#### Builtin

- support builtin ulimit command
- add some String methods
    - replace
    - lower
    - upper

#### API

- add DSState_loadModule api
- improve error handling of DSState_loadAndEval

#### Interactive

- rewrite RC file loading
    - fix race condition
    - improve error message

#### Misc

- experimental LSP server (early stage)

### Changed

- fix '--version' option output. no longer show copyright year
- set exit status to 1, when throw exception
- when press CTRL-D, call 'exit'
- not restore exit status when unwinding within signal handler
- no longer change debug logging policy at runtime
- change DSError definition
- 'else' keyword is not allowed as command name
- no longer need expect command

### Fixed

- map literal behavior
- exit status
- module loading order
- fix TOCTOU race condition of RC file loading
- error handling of invalid module file loading
- stdin restoring in last pipe
- toplevel printing of last pipe
- error line printing

## [0.11.0] - 2018-08-28

### Added

- stabilize module system
    - node/byte code dumper correctly work when use source statement
    - correctly work at all execution modes
    - module aware error reporting
    - search system config dir and local config dir
- builtin read command can accept /dev/fd/* style description (when use -u option)
- builtin test command can accept /dev/fd/* style description (when use -t option)
- support process substitution
- introduce config dir
    - ${CMAKE_INSTALL_PREFIX}/etc/ydsh
    - ~/.ydsh
- introduce libydsh
- atexit module
    - now set multiple termination handler
- improve assertion messages of interactive test cases

### Changed

- drop support D-Bus related features
    - D-Bus object type
    - Proxy object type
    - ObjectPath type
    - Variant type
    - interface loading
- type inheritance of some type (due to remove Variant type)
- status-log format
- RC file loading
    - now use module system
- public api
    - DSError maintains error source name
    - DSState_loadAndEval interface
    - introduce DS_ERROR_KIND_FILE
    - DSState_setScriptDir behavior
- simple command in last pipeline is evaluated in subshell
- no longer ignore SIGPIPE by default
- UnixFD object handling
    - string representation
    - iohere no longer accepts UnixFD object
    - string concatenation is not allowed in command arguments
- replace git submodule with cmake-external project
    - google test
    - re2c
- no longer need libxml2, libdbus

### Fixed

- stack reservation of user-defined command
- current working directory handling
- TOCTOU race condition in script loading
- type checking of break expression

## [0.10.0] - 2018-04-30

### Added

- reactivate history saving when exit/assert
- builtin fg/bg command (only available when job control is enabled)
- stabilize job control
- in interactive mode, propagate received SIGHUP to managed jobs
- in interactive mode, when call builtin exit, send SIGHUP to managed jobs
- try expression
- user-defined termination handler (TERM_HOOK)
    - exit (also script end)
    - uncaught exception
    - assertion failure
- experimental module system support
- change debug logging policy at runtime

### Changed

- when specified '-e' option, '--trace-exit' option does not affect
- not maintain pending signal order (replace pending signal queue with bitset)
- builtin kill command sends signal to process group
- remove termination hook from public api
- Signals api
- typing rule of if expression
- AST dump format (remove RootNode)
- public api
    - DSCandidates interface
- use '!' for unary not op
- scripts containing null characters are now not acceptable
- change Job type method
    - remove suspend/resume method
    - remove boolean operator
    - change return type of wait method
    - rename kill with raise
    - add poll method
- method lookup mechanism
- stack overflow detection
    - introduce separate control stack and check depth of control stack

### Fixed

- job control signal (SIGCONT, SIGTTIN, etc) handling
- EOS token position
- line marker of method call, self assignment, indexer

## [0.9.0] - 2018-01-01

### Added

- builtin set_env/unset_env
- UnixFD object
    - dup/close
    - constructor (file open)
    - redirection target
- Unified pipeline
    - expression in pipelines
- Nothing type
- Asynchronous execution (experimental)
    - background job
    - disowned background job
    - co-process
- Job type
    - for asynchronous execution
    - some control method
        - wait
        - suspend
        - resume
        - kill
        - detach
        - etc..
- basic job-control feature (experimental)
    - job-table
    - builtin kill

### Changed

- throw exception when access environmental variable (after unset_env)
- prompt string interpretation behavior
- not import OLDPWD/PWD by default
- allow void cast
- public api
- type alias syntax (now use alias keyword)
- forbit redefinition of builtin exec command
- user-defined/builtin commands in last pipe are executed in parent shell (due to unified pipeline)
- operator precedence of throw expression
- temporary disable history save when terminated by exit or assert
- cannot change SIGCHLD handler

### Fixed

- build error when specified _FORTIFY_SOURCE=2 (now use this option by default)
- code generation of block node (reclaim local)
- mix of io buffer when fork-capture
- exit status handling (when terminated by signal)
- line marker
- invalid UTF-8 byte sequence handling
- lexer mode stack
- infinite for-loop
- type checking of command substitution
- builtin command help message

## [0.8.0] - 2017-08-28

### Added

- support single element tuple literal
- support null coalescing operator for option type
- block expression
- if expression
- expression with io redirection
- support signal handling (except for posix real-time signal)
- loop expression(for, for-in. while, do-while)
    - break statement return value
- put comma in last element of array and map literal
- reactivate LLVM fuzz target

### Changed

- default value of exit command
- disallow magic method invocation
- public api
- remove constructor of single element tuple type
- ternary expression has void expression
- if, do-while, while, for, try, block statement parsing
- bit operator syntax
    - "-and" -> "and"
    - "-or" -> "or"
    - "-xor" -> "xor"
- array/map method
    - rename "find" to "get"
    - return value of "put"
    - "default" method to map
- toplevel-printing of option value
- regex api
    - remove "search" method
    - add "=\~" / "!\~" operator
- builtin exit command is Bottom type
- PID, PPID type (Uint32 -> Int32)

### Fixed

- type checking of is expression
- home/end key in putty
- SEGV in interactive mode
- new expression
- '!=' and '!~' operator parsing when left hand-side is a type expression
- multiple return type parsing
- stack overflow of parser
- memory leak of type coercion

## [0.7.0] - 2017-05-06

### Added

- linenoise use history api
- support history command
- add SCRIPT_DIR
- add regex type and regex literal
- add option type and unwrap operator
- reactivate ternary expression
- ensure destructor call of local variable when out of scope
- support here string
- cleanup redundant code
- rewrite pipeline evaluation api for supporting unified-pipeline syntax

### Changed

- do-while condition is out of scope
- do not propagate SystemError from pipeline
- user-defined command is executed in parent shell
- tilde expansion behavior
- node dumper format
- some string api (use option type)

### Fixed

- stacktrace element
- control flow of nested try-catch
- invalid option handling of read and cd command

## [0.6.0] - 2016-12-31

### Added

- add experimental history api
- add abbreviated type notation for array, map and tuple types
- improve builtin read command
- improve command line option handling of builtin command
- add basic debug api
- cleanup and stabilize the interpreter
- improve null character handling of string api
- update google test to 1.8
- support the following builtin variable
    - RANDOM
    - SECONDS
    - HISTCMD, HISTFILE, HISTSIZE, HISTFILESIZE
    - MACHTYPE
    - UID, EUID, PID, PPID
- improve error line number
- reactivate waitSignal method

### Changed

- change naming convention of public api
- remove __ADD__ method from string
- backslash handling in double-quoted string
- drop support ternary expression
- drop support print exprression

### Fixed

- string self assignment
- builtin cd command
- completor
- undefined behavior of illegal iterator usage
- line marker of EOS token

## [0.5.0] - 2016-06-27

### Added

- replace AST interpreter with byte code interpreter
- omit parenthesis from assert, if, while, do-while, catch statement
- change throw statement to expression
- support ternary expression
- cd and pwd command support -L and -P option
- hex and octal number

### Changed

- temporary disable waitSignal method (due to some issue)
- for-in statement syntax
- follow symbolic link when complete file name

### Fixed

- break and continue statement in finally block
- correctly handle stack overflow

## [0.4.0] - 2016-02-29

### Added

- improve semantic error message (now show error line marker)
- replace editline to linenoise
- add some built-in variable (OSTYPE, YDSH_VERSION, REPLY, reply, IFS)
- support positional parameter ($1, $2, ... $9, $#)
- add special character '$$'  (for indicating parent process pid)
- add built-in test command
- add built-in read command (only support basic feature)
- specifying a separator of internal field splitting (use IFS)
- escaped string literal supports octal or hex number
- cache full path of command name (also support hash command)
- support basic input completion

### Changed

- public api
- change logical operator syntax (&, |, ^) to (-and, -or, -xor)
- ignore empty string value of String Array object when performing parameter expansion
- allow subscript operator when performing parameter expansion
- unary operator type
- built-in eval command invokes user-defined command

### Fixed

- UTF-8 handling
- infinite loop of binary expression parsing
- command name syntax
- exec_test runner
- segmentation fault when having circular reference (now raise StackOverflowError)
- suffix operator
- integer cast
- environmental variable handling

## [0.3.0] - 2015-11-04

### Added

- add some string api(count, slice, indexOf, startsWith, ...etc.)
- prompt string interpretation(PS1/PS2)
- add some built-in command (command, eval, exec, pwd)
- user-defined command
- add some float api(isNan, isFinite)
- block statement
- add debug function(checked-cast, logging)

### Changed

- import-env/export-env(default value or exception raising)
- string literal definition
- map literal syntax
- remove backquote literal
- float zero-division behavior

### Fixed

- coercion
- try-catch behavior
- unreachable code detection
- parameter expansion of Any object
- function call
- assert or exit
