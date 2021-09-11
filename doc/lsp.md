# Supported Feature of Language Server Protocol

## ``textDocument/definition``, ``textDocument/references``, ``textDocument/hover``

| **Feature**  | ``definition`` | ``references`` | ``hover`` |
|----------------|---------|---------|-----|
| variable (local/global) | ✔️ | ✔️ | ✔️ |
| variable (for-in) | ✔️ | ✔️ | ✔️ |
| variable (catch) | ✔️ | ✔️ | ✔️ |
| variable (function parameter) | ✔️ | ✔️ | ✔️ |
| variable (global import) | ✔️ | ✔️ | ✔️ |
| variable (named import) | ❌ | ❌ | ❌ |
| builtin variable | - | - | ❌ |
| builtin constant | - | - | ❌ (show value) |
| function  | ✔️ | ✔️ |✔️ |
| function (global import) | ✔️ | ✔️ | ✔️ |
| function (named import) | ❌ | ❌ | ❌ |
| user-defined command | ✔️ | ✔️ |✔️ |
| user-defined command (global import)  | ✔️ | ✔️ | ✔️ |
| builtin command  | - | - | ❌ (show help) |
| type (alias) | ✔️ | ✔️ |✔️ |
| type (global import)| ✔️ | ✔️ | ✔️ |
| type (named import) | ❌ | ❌ | ❌ |
| field (tuple)      | - | - | ❌ |
| method (builtin) | - | - | ❌ |
| source path | ❌ | ❌ | ❌ (show path) |
| source path (glob) | ❌ | ❌ | ❌ (show path list) |
| source (variable) | ✔️ | ✔️ | ✔️ |
| source (type alias) | ❌ | ❌ | ❌ |
| source (command) | ❌ | ❌ | ❌ |