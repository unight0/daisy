Daisy
================================================================================

**Daisy** is a minimalistic FORTH-like language kernel. It is developed as an
exploration of capabilities of stack-based languages.

Examples are located in 'examples/' folder.
Standard library in development, located in 'include/' folder.

## How to build
Type `make` to build the language kernel. Then run `kern`. Type in `words` to see
words that exist in the system. Use `load <filename>` or `load "<filename>"` to
include files, e.g. `load basis.fs`.
