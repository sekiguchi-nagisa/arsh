# Supported Features of Language Server Protocol
## Supported Language Features

| **Feature**                          | **status** |
|--------------------------------------|------------|
| ``textDocument/definition``          | ✔️         |
| ``textDocument/references``          | ✔️         |
| ``textDocument/documentHighlight``   | ✔️         |
| ``textDocument/documentLink``        | ✔️         |
| ``textDocument/hover``               | ✔️         |
| ``textDocument/documentSymbol``      | ✔️         |
| ``textDocument/semanticTokens/full`` | ✔️         |
| ``textDocument/signatureHelp``       | ✔️         |
| ``textDocument/completion``          | ✔️         |
| ``textDocument/publishDiagnostics``  | ✔️         |
| ``textDocument/rename``              | ⛛          |
| ``textDocument/prepareRename``       | ✔️         |


## Supported symbols

| **Symbol**                            | ``definition`` | ``references``        | ``hover``       | ``rename`` |
|---------------------------------------|----------------|-----------------------|-----------------|------------|
| variable (local/global)               | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (for-in)                     | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (catch)                      | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (function param)             | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (user-defined command param) | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (this)                       | ✔️             | ✔️                    | ✔️              | -          |
| variable (upper variable)             | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (global import)              | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (inlined import)             | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (named import)               | ✔️             | ✔️                    | ✔️              | ✔️         |
| variable (positional parameter)       | -              | -                     | ❌ (show code)   | -          |
| variable (if-let)                     | ✔️             | ✔️                    | ✔️              | ✔️         |
| builtin variable                      | -              | -                     | ✔️              | -          |
| builtin constant                      | -              | -                     | ✔️ (show value) | -          |
| function                              | ✔️             | ✔️                    | ✔️              | ✔️         |
| function (global import)              | ✔️             | ✔️                    | ✔️              | ✔️         |
| function (inlined import)             | ✔️             | ✔️                    | ✔️              | ✔️         |
| function (named import)               | ✔️             | ✔️                    | ✔️              | ✔️         |
| user-defined command                  | ✔️             | ✔️                    | ✔️              | ✔️         |
| user-defined command (global import)  | ✔️             | ✔️                    | ✔️              | ✔️         |
| user-defined command (inlined import) | ✔️             | ✔️️                   | ✔️              | ✔️         |
| user-defined command (named import)   | ✔️             | ✔️️️                  | ✔️️             | ✔️         |
| builtin command                       | -              | ✔️                    | ✔️️ (show help) | -          |
| type (builtin)                        | -              | ✔️️️                  | ✔️️️            | -          |
| type (alias)                          | ✔️             | ✔️                    | ✔️              | ✔️         |
| type (global import)                  | ✔️             | ✔️                    | ✔️              | ✔️         |
| type (inlined import)                 | ✔️             | ✔️️                   | ✔️              | ✔️         |
| type (named import)                   | ✔️             | ✔️                    | ✔️              | ✔️         |
| type (user-defined type)              | ✔️             | ✔️                    | ✔️              | ✔️         |
| field (tuple)                         | -              | ✔️ (same module only) | ✔️              | -          |
| field (user-defined type)             | ✔️             | ✔️                    | ✔️              | ❌          |
| method (builtin)                      | -              | ✔️                    | ✔️              | -          |
| method (generic type)                 | -              | ✔️                    | ✔️              | -          |
| method (user-defined)                 | ✔️             | ✔️                    | ✔️              | ✔️         |
| source (variable)                     | ✔️             | ✔️                    | ✔️              | ✔️         |
| source (type alias)                   | ✔️             | ✔️                    | ✔️              | ✔️         |
| source (command)                      | ✔️             | ✔️                    | ✔️              | ✔️         |
| here doc start/end                    | ✔️             | ✔️                    | ✔️              | -          |