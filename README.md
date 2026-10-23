# Brief

Brief is an experimental statically typed language. The first compiler is
written in C and emits readable C, then invokes an existing C compiler to
produce a native program.

**Current state: milestones 0–5 are implemented**, including native compilation,
numbers, control flow, heap strings, structs, methods, and reference counting.
**M5 is complete for the first-version scope:** pipelines, scalar/symbol matching, named symbol sets,
implicit success returns, error-set inference, `?`/`!` handling, numeric/string
conversions and string literal patterns now compile.
Compilation supports the subset below; the parser
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
  `chr`, strings, and `()`; no implicit numeric conversions.
- Arithmetic, numeric/character comparisons, bool/unit equality, bitwise
  operations, and short-circuit logic. Integer
  overflow, integer division/remainder by zero, invalid shifts, and negative
  integer exponents panic in every build, with the Brief file and line.
- Lexical `let`/`var` bindings, optional annotations, assignments, compound
  assignments, and standalone `x++`/`x--` statements (no update expressions).
- Value-producing `if`/`else`, `while`, half-open ascending range `for` loops,
  and innermost-loop `break`/`continue`.
- Pipeline calls with method-first lookup and left-to-right evaluation; contextual
  numeric literals receive the same parameter type as in ordinary calls.
- Exhaustive match blocks over numeric, bool and character values, with ordered
  literal/range/comparison arms. `_` and whole-value binding arms work for all
  current types; bindings are immutable and scoped to their arm. Floating matches
  require a catch-all for NaN, and `-0` and `0` match the same values.
  String literal patterns compare complete decoded bytes, including embedded NULs;
  string matches require a wildcard or binding catch-all. Duplicate literals and
  arms after a catch-all are rejected.
  Match results can be values, and arms support `ret`, `break`, and `continue`.
- Data symbols such as `:missing` and `:found(user)`, canonical `Sym<...>` types,
  forward named-set aliases and composition, and subset assignment/arguments/returns.
  Symbol matches support tags, payload bindings/wildcards and named groups;
  catch-all bindings receive only the remaining variants. Data-symbol return sets
  grow across recursive function groups. Finite symbol shapes have payload-aware
  equality; payload-free symbol ordering is not implemented yet.
- Implicit `:ok` success returns, explicit outcomes, `?` propagation and `!` panics.
  Error sets accumulate through recursive calls; partial return annotations such as
  `Sym<:ok(User), _>` fix the success type and infer additional errors. Success-only
  functions retain plain-value C ABIs. Calls with both success and error variants
  must be handled directly with `?`, `!`, or a match, even when discarded or returned.
- Typed parameters, forward calls, recursion, written or inferred
  return types (including structs and symbols), `ret`, and implicit tail returns. Unanchored recursive return
  types need annotations; call-graph analysis uses explicit work stacks, not
  the old 128-function depth limit. Runtime recursion still uses the host C
  stack and may exhaust it rather than panic; tail-call elimination is not
  guaranteed.
- Numeric-target converters accept numbers or strings. Integer targets require
  exact integral values in range; floating targets round, with every integer-to-float
  conversion succeeding. `dub` to `flt` fails only on finite overflow. Numeric
  literals take the converter's target type, so `shrt(1)` succeeds directly and
  `shrt(70000)` is a compile error. Other numeric failures return `:overflow`.
  Parsing text returns `:invalid(original)` on bad syntax or range, with binary-safe
  full-input validation. Floating decimal text rounds directly to its destination
  width. Conversions work with pipelines, matching,
  `?` and `!`; see `docs/lang-types.txt` for the exact rules.
- Immutable heap strings, `+`/`+=`, bytewise comparisons, and `str(number)` /
  `str(bool)` conversions. See `docs/lang-types.txt` for formatting choices.
- Nominal structs, complete named field initialization, field reads/writes and
  numeric field updates, `impl`, associated/receiver calls, and `Self`.
  Ordinary parameters and their direct local/field aliases are read-only; `&`
  struct or symbol parameters permit mutation of reachable objects. Permissions
  propagate through symbol payloads, rebinding and
  control flow. A struct call result is read-only when its type can reach a
  read-only argument's objects, and a literal is read-only when built from a
  read-only struct. A read-only struct cannot be stored in a field, so a
  function that keeps an argument takes it as `&`.
  References share objects rather than copying them (this is not `const` or purity).
- Fieldwise equality for nonrecursive structs. Reference counting owns locals,
  temporaries, fields and returns; parameters borrow. Every scope exit and early
  return releases owners, and loop temporary slots release previous values.

`main()` is required and cannot have parameters. Its successful result is discarded;
success exits 0, including data-only symbol results without `:ok`. Only a fallible
result type (both `:ok` and other variants) produces an entry error: an active
non-`:ok` variant prints `error: :tag(payload)` to stderr, releases its payload and
exits 1. Prior stdout is flushed before error/panic output. String/character payloads are quoted and
escaped; structs and nested symbols have bounded diagnostic representations. An explicit `: ()` discards a function's tail value, but not an
explicit non-unit `ret`. `print` remains a reserved top-level builtin taking exactly one
`str`; it writes bytes plus a newline, including embedded zero bytes. `str` is
also reserved as a top-level builtin and accepts exactly one number or `bool`.

Partial `Sym<..., _>` annotations are return-only and must declare `:ok`; they are
not general open symbol-set types. Symbol payload patterns currently must be a
binding or `_`; refutable/nested patterns and symbol ordering remain deferred
features outside M5's acceptance scope. `const`,
character/object/string-to-string conversions, indexing,
function values, cloning, identity comparison, and other later features also
remain unsupported. Recursive struct/symbol shapes do not have equality.
`dump-ast` remains syntax-only.

`CC` names a **single executable** (default `cc`), not a shell command with
flags. Paths containing spaces work; use an executable wrapper for extra flags
or compiler launchers such as `ccache gcc`.
Native compilation uses `-std=c11 -O2` and links `-lm`; `--debug` instead selects `-O0 -g`, ASan,
and UBSan, with a debug report for any heap objects still alive at normal exit. `build` defaults to `a.out` and atomically replaces the requested
output only after successful C compilation. `run` uses `TMPDIR` (default `/tmp`),
removes its temporary files (including after Ctrl-C), and propagates the program's
exit status. While waiting, the driver ignores SIGINT but the child retains its
original disposition, so terminal Ctrl-C stops the child and cleanup still runs. Use `--`
before an input filename starting with `-` for `build` or `run`.

The generated C uses readable `brf_main`/`brf_greet` function names, a distinct
`brm_` namespace for length-qualified methods, `brs_` for struct types, `bry_` for
value-represented symbol sets, and `brt_`
for runtime helpers and generated locals. Strings have explicit byte lengths.
It includes its runtime and needs no project headers or libraries; link with
system `libm` (`-lm`) for floating-point power. Generated C checks that the host
provides binary32 `float` and binary64 `double`.
String literals remain permanent; computed strings and structs use single-threaded
reference counting. The initial ownership pass conservatively pins all reference
reads, including argument snapshots, before later evaluation can invalidate them.
Count elimination and cycle collection are not implemented. Generated equality
and release callbacks also use the host C stack; very deep object shapes can
exhaust it rather than panic. Panics terminate the process without unwinding owners.

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
  with canonical primitive/nominal types, lexical scopes, flow checks, read-only
  alias analysis, scalar pattern normalization/coverage/usefulness checks, and
  iterative SCC-based recursive return inference.
- `include/ir/`, `src/ir/`: typed temporaries, sequenced calls, explicit control
  flow/returns, loop targets, value-producing branches, field operations, and a
  separately testable reference-count insertion pass.
- `include/codegen/`, `src/codegen/`, `runtime/`: self-contained C emission,
  source mapping, heap strings/structs, automatic ownership, printing/conversions,
  and checked numeric operations.
- `include/driver/`, `src/driver/`: shell-free native compiler invocation.
- `tests/`: common, lexer, newline-filter, parser, and compilation unit tests;
  native driver tests; program tests in normal and sanitizer configurations;
  and 146 doc-example parsing cases, with wrappers for fragments and expected
  failures for syntax-error examples. Later-version syntax and incomplete
  API notation are excluded explicitly in the doc-test manifest. Native document
  acceptance adds 49 error-handling, 20 pipeline/matching, 33 function, 32 symbol,
  48 memory/permission and 65 syntax cases, with explicit partial/deferred
  classifications and tested audit guards; run them with
  `ctest --test-dir build -L doc-acceptance --output-on-failure`.
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

The overview links the remaining topic documents. Milestone 5 is complete for the
first-version scope: pipelines, scalar/symbol matching, named sets, success/error
handling, numeric/string conversions and string literal patterns are implemented.
The [error-example acceptance audit](tests/doc-errors.md) checks the in-scope
`lang-errors.txt` examples, including their compile errors. **Milestone 6 is in
progress:** executable pipeline/matching, function, symbol, memory and syntax doc
examples now use shared acceptance guards. Function-contract mismatches no longer
cause a misleading partial-set payload-collision diagnostic. The
[document acceptance tracker](tests/doc-acceptance.md) records partial coverage
and the remaining audits/diagnostic finish. Symbol ordering and refutable/nested
payload patterns remain deferred, not M5 blockers.
