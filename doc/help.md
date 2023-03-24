# -h/--help option support for builtin commands
builtin commands except for `:`, `call`, `echo`, `eval`, `false`, `test`, `true` support `-h`/`--help` options

| **builtin command** | **support** |
|---------------------|-------------|
| `:`                 | -           |
| `__gets`            | ✔️          |
| `__puts`            | ✔️          |
| `_exit`             | ✔️          |
| `bg`                | ✔️          |
| `call`              | -           |
| `cd`                | ✔️          |
| `checkenv`          | ✔️          |
| `command`           | ✔️          |
| `complete`          | ✔️          |
| `dirs`              | ✔️          |
| `disown`            | ✔️          |
| `echo`              | -           |
| `eval`              | -           |
| `exec`              | ✔️          |
| `exit`              | ✔️          |
| `false`             | -           |
| `fg`                | ✔️          |
| `hash`              | ✔️          |
| `help`              | ✔️          |
| `jobs`              | ✔️          |
| `kill`              | ✔️          |
| `popd`              | ✔️          |
| `pushd`             | ✔️          |
| `pwd`               | ✔️          |
| `read`              | ✔️          |
| `setenv`            | ✔️          |
| `shctl`             | ✔️          |
| `test`              | -           |
| `true`              | -           |
| `ulimit`            | ✔️          |
| `umask`             | ✔️          |
| `unsetenv`          | ✔️          |
| `wait`              | ✔️          |
