# Changelog

## [Unreleased]

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
