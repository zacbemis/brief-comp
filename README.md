# Brief

Brief is an experimental statically typed language. The first compiler is
written in C and emits readable C, then invokes an existing C compiler to
produce a native program.

**Current state: milestones 0–3 are implemented**, including native compilation,
numbers, bindings, control flow, and function return inference. Compilation
supports the subset below; the parser
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
cc -std=c11 hello.c -lm -o hello
./build/brief run --debug tests/programs/hello.brf
```

Compilation supports:

- Every integer and floating-point type, contextual decimal literals, `bool`,
  `chr`, permanent strings, and `()`; no implicit numeric conversions.
- Arithmetic, numeric/character comparisons, bool/unit equality, bitwise
  operations, and short-circuit logic. Integer
  overflow, integer division/remainder by zero, invalid shifts, and negative
  integer exponents panic in every build, with the Brief file and line.
- Lexical `let`/`var` bindings, optional annotations, assignments, compound
  assignments, and standalone `x++`/`x--` statements (no update expressions).
- Value-producing `if`/`else`, `while`, half-open ascending range `for` loops,
  and innermost-loop `break`/`continue`.
- Typed parameters, forward calls, recursion, written or inferred primitive
  return types, `ret`, and implicit tail returns. Unanchored recursive return
  types need annotations; call-graph analysis uses explicit work stacks, not
  the old 128-function depth limit. Runtime recursion still uses the host C
  stack and may exhaust it rather than panic; tail-call elimination is not
  guaranteed.

`main()` is required and cannot have parameters. Its plain result is discarded;
success exits 0. An explicit `: ()` discards a function's tail value, but not an
explicit non-unit `ret`. `print` remains a reserved top-level builtin taking exactly one
`str`; it writes bytes plus a newline, including embedded zero bytes. Numeric
results in the program tests are observed through comparisons and branches.

`const`, numeric conversions, string operations, heap strings, structs, symbols,
pipelines, mutable
borrow parameters, and other later-milestone features produce explicit
unsupported-feature diagnostics. `dump-ast` remains syntax-only.

`CC` names a **single executable** (default `cc`), not a shell command with
flags. Paths containing spaces work; use an executable wrapper for extra flags
or compiler launchers such as `ccache gcc`.
Native compilation uses `-std=c11 -O2` and links `-lm`; `--debug` instead selects `-O0 -g`, ASan,
and UBSan. `build` defaults to `a.out` and atomically replaces the requested
output only after successful C compilation. `run` uses `TMPDIR` (default `/tmp`),
removes its temporary files (including after Ctrl-C), and propagates the program's
exit status. While waiting, the driver ignores SIGINT but the child retains its
original disposition, so terminal Ctrl-C stops the child and cleanup still runs. Use `--`
before an input filename starting with `-` for `build` or `run`.

The generated C uses readable `brf_main`/`brf_greet` function names, a separate
`brt_` runtime namespace, and escaped string literals with explicit byte lengths.
It includes its runtime and needs no project headers or libraries; link with
system `libm` (`-lm`) for floating-point power. Generated C checks that the host
provides binary32 `float` and binary64 `double`.
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
  with canonical primitive types, lexical scopes, flow checks, and iterative
  SCC-based recursive return inference.
- `include/ir/`, `src/ir/`: typed temporaries, sequenced calls, explicit control
  flow/returns, loop targets, and value-producing branches.
- `include/codegen/`, `src/codegen/`, `runtime/`: self-contained C emission,
  source mapping, literal strings/printing, and checked numeric operations.
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

The overview links the remaining topic documents. Next is milestone 4:
heap strings, conversions, structs, methods, mutable borrows, and reference counts.
