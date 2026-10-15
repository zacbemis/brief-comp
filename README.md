# Brief

Brief is an experimental statically typed language. The first compiler is
written in C and emits readable C, then invokes an existing C compiler to
produce a native program.

**Current state: milestones 0–2 are implemented**, including the end-to-end
walking skeleton. Compilation supports only the narrow subset below; the parser
accepts much more syntax. Most examples in the design docs still describe future
features, not programs that can currently be compiled.

## Build and test

Requires a POSIX host, a C11 compiler (GCC or Clang), CMake 3.16 or newer,
and Ninja for the helper scripts. The test tools are also written in C;
CTest has no additional interpreter dependency.

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

## Inspect source

```sh
./build/brief dump-tokens hello.brf
./build/brief dump-ast hello.brf
```

The command loads the whole file and prints each token's line, byte column,
kind, and escaped source spelling. Lexical errors are collected and printed
on standard error with their source line and an underline; invalid input exits
with status 1. Invalid command arguments exit with status 2.

`dump-ast` filters continuation newlines and parses first-version syntax into
an arena-owned tree, printed as a stable S-expression. Syntax errors are
collected with source locations; malformed items are skipped so later errors
can still be reported, even when the file also contains lexer errors. Assignment
is parsed only as a standalone, non-chaining statement, and pipeline calls must
have a name or dotted-path callee. Bare block expressions and standalone block
statements are rejected; blocks remain valid in documented body and match
positions. Parsing does not resolve names, check types,
enforce error handling, or check match exhaustiveness.

## Compile and run

```sh
./build/brief run tests/programs/hello.brf
./build/brief build tests/programs/hello.brf -o hello
./hello
./build/brief emit-c tests/programs/hello.brf > hello.c
cc -std=c11 hello.c -o hello
./build/brief run --debug tests/programs/hello.brf
```

Milestone 2 compiles zero-argument, unit-returning functions, including forward
calls, string literals, `()`, `ret`, and `print(str)` in function bodies. `print`
writes the string's bytes followed by a newline, including embedded zero bytes.
An explicit `: ()` discards the final value; inferred non-unit returns are not
implemented. `main()` is required. Duplicate functions, unknown calls, and
incorrect argument counts/types are diagnosed before invoking C compilation.
The name `print` is reserved for the builtin in this subset.

Numbers, bindings, parameters, recursion, structs, symbols, pipelines, and other
later-milestone features produce explicit unsupported-feature diagnostics.
The call-graph traversal limits recursive descent to 128 function levels.
`dump-ast` remains syntax-only and does not impose these compilation limits.

`CC` names a **single executable** (default `cc`), not a shell command with
flags. Paths containing spaces work; use an executable wrapper for extra flags
or compiler launchers such as `ccache gcc`.
Native compilation uses `-std=c11 -O2`; `--debug` instead selects `-O0 -g`, ASan,
and UBSan. `build` defaults to `a.out` and atomically replaces the requested
output only after successful C compilation. `run` uses `TMPDIR` (default `/tmp`),
removes its temporary files (including after Ctrl-C), and propagates the program's
exit status. While waiting, the driver ignores SIGINT but the child retains its
original disposition, so terminal Ctrl-C stops the child and cleanup still runs. Use `--`
before an input filename starting with `-` for `build` or `run`.

The generated C uses readable `brf_main`/`brf_greet` function names, a separate
`brt_` runtime namespace, and escaped string literals with explicit byte lengths.
It includes its runtime and needs no project headers or libraries.
Only permanent string literals exist so far; heap strings and reference counting
arrive in milestone 4.

## Compiler components

- `include/common/`, `src/common/`: arena allocation, source loading and line
  indexing, byte-offset spans, diagnostics with notes, and string interning.
- `include/lexer/`, `src/lexer/`: streaming and whole-file lexing, validated
  escapes, single-line strings, and single-byte character literals. Unterminated
  literals report errors without consuming the next line; unclosed block
  comments are also errors.
- `include/ast/`, `src/ast/`: syntax-tree nodes with spans and a tree printer.
- `include/parser/`, `src/parser/`: recursive-descent declarations, statements,
  and patterns, Pratt expressions, generic-call disambiguation, and recovery.
- `src/lexer/newline.c`: continuation and nested-delimiter newline filtering.
- `include/sema/`, `src/sema/`: separate name resolution and type checking,
  with canonical unit/string types and resolved call targets.
- `include/ir/`, `src/ir/`: statement-based lowering with string temporaries,
  sequenced calls, and explicit returns.
- `include/codegen/`, `src/codegen/`, `runtime/`: self-contained C emission,
  source mapping, and an embedded literal-string/print runtime.
- `include/driver/`, `src/driver/`: shell-free native compiler invocation.
- `tests/`: common, lexer, newline-filter, parser, and compilation unit tests;
  native driver tests; program tests in normal and sanitizer configurations;
  and 145 doc-example parsing cases, with wrappers for fragments and expected
  failures for syntax-error examples. Later-version syntax and incomplete
  API notation are excluded explicitly in the doc-test manifest.
- `tests/parse/`: 106 parser example files that must parse or be rejected
  on a stated line, run by `test_parse_examples` (CTest label
  `parse-examples`).

Source text, token arrays, interned names, and diagnostics live in the
compilation arena. Tokens retain source slices even on errors; error messages
are separate. Raw newlines, including those in block comments, are preserved
until the newline filter runs. Source columns count bytes; diagnostic excerpts
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

The overview links the remaining topic documents. Next is milestone 3:
numbers, bindings, control flow, and general function return checking/inference.
