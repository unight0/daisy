Daisy
================================================================================
![Logo](./daisy.svg)

**Daisy** is a minimalistic FORTH-like language kernel. It is developed as an
exploration of capabilities of stack-based languages.

Examples are located in 'examples/' folder.
Standard library in development, located in 'include/' folder.

## How to use
Run `kern`. Type in `words` to see
words that exist in the system. Use `load <filename>` or `load "<filename>"` to
include files, e.g. `load basis.fs`. Type in `doc <word>` to view
documentation about the word. Type in `doc-mem` to view important
information about FORTH memory model.
In future, there will be an option to compile the language without
documentation to decrease the final file size.

## How to build
Type `make` to build the language kernel.

## How to install
Modify the PREFIX variable within the Makefile to set your installation prefix.
Run `make install` to build and install into the specified prefix. Default
prefix is the local directory.
