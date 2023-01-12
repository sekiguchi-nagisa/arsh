# -h/--help option support for builtin commands
builtin commands except for `:`, `echo`, `eval`, `false`, `true` support `-h`/`--help` options

| **builtin command** | **support** |
|---------------------|-------------|
| `:`                 | -           |
| `__gets`            | ✔️          |
| `__puts`            | ✔️          |
| `_exit`             | ✔️          |
| `bg`                | ✔️          |
| `cd`                | ✔️          |
| `checkenv`          | ✔️          |
| `command`           | ✔️          |
| `complete`          | ✔️          |
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
