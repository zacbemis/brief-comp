# Brief

Brief is an experimental statically typed language. The first compiler is
written in C and will emit readable C, then invoke an existing C compiler to
produce a native program.

**Current state: milestone 0 (groundwork) is implemented.** There is a lexer
and a token-dump command, but no parser, type checker, or code generation yet.
Language examples in the design docs describe intended behavior, not features
that can currently be compiled.

## Build and test

Requires a C11 compiler, CMake 3.16 or newer, and Ninja for the helper scripts.

```sh
./build.sh
./test.sh
```

The scripts build in `build/` using the Debug configuration. GCC and Clang
builds enable `-Wall -Wextra -Wpedantic`; Debug builds also enable AddressSanitizer
and UndefinedBehaviorSanitizer by default. To disable sanitizers explicitly:

```sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBRIEF_ENABLE_SANITIZERS=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

For an optimized build without sanitizers:

```sh
cmake -G Ninja -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

## Inspect tokens

```sh
./build/brief dump-tokens hello.brf
```

The command loads the whole file and prints each token's line, byte column,
kind, and escaped source spelling. Lexical errors are collected and printed
on standard error with their source line and an underline; invalid input exits
with status 1. Invalid command arguments exit with status 2.

`run`, `build`, `emit-c`, and `dump-ast` are planned, not implemented commands.

## Compiler groundwork

- `include/common/`, `src/common/`: arena allocation, source loading and line
  indexing, byte-offset spans, diagnostics with notes, and string interning.
- `include/lexer/`, `src/lexer/`: streaming and whole-file lexing, validated
  escapes, single-line strings, and single-byte character literals. Unterminated
  literals report errors without consuming the next line; unclosed block
  comments are also errors.
- `include/ast/`: initial syntax-tree kind enums only.
- `tests/`: common-infrastructure and lexer unit tests, plus CLI integration
  tests for token dumps, multiple diagnostics, and file-loading errors.

Source text, token arrays, interned names, and diagnostics live in the
compilation arena. Tokens retain source slices even on errors; error messages
are separate. Raw newlines, including those in block comments, are preserved
for the future newline filter. Source columns count bytes; diagnostic excerpts
expand tabs to four-column stops and count UTF-8 code points rather than bytes.
Wide and combining characters do not yet have special display-width handling.

## Design and implementation plan

- [Language overview and first-version scope](docs/lang-desc.txt)
- [Syntax](docs/lang-syntax.txt)
- [Types](docs/lang-types.txt)
- [Functions](docs/lang-functions.txt)
- [Symbols](docs/lang-symbols.txt)
- [Error handling](docs/lang-errors.txt)
- [First compiler plan](docs/plans/first-version.txt)

The overview links the remaining topic documents. Next is milestone 1:
newline filtering, syntax-tree nodes, parsing, recovery, and `dump-ast`.
