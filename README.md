# Brief

Brief is an experimental statically typed language. Its compiler is written in
C11, emits readable C and invokes an existing C compiler to build a native
program.

The compiler implements the language in [`docs/lang-*.txt`](docs/lang-desc.txt)
for the first version's scope: numbers, strings, structs with methods, symbols
and unions, outcomes with `?` and `!`, pattern matching, pipelines, loops,
`let ... else` and automatic reference counting. The language is moving to a
next design (sketched in [`docs/next/`](docs/next/)); the compiler's structure
is being prepared for it by the [architecture plan](docs/plans/architecture.txt).

## Build and test

Requires a POSIX host, a C11 compiler (GCC or Clang), CMake 3.16 or newer and
Ninja.

```sh
./build.sh   # Debug build in build/, with AddressSanitizer and UBSan
./test.sh    # ctest
```

CI runs GCC and Clang Debug builds with sanitizers, a GCC Release build and a
GCC Debug build without sanitizers, all with warnings as errors, and checks
formatting with clang-format 23.1.3:

```sh
git ls-files -z '*.c' '*.h' | xargs -0 clang-format --dry-run --Werror
```

## Use

```sh
./build/brief dump-tokens file.brf
./build/brief dump-ast file.brf
./build/brief emit-c file.brf > file.c
./build/brief build [--debug] file.brf [-o executable]
./build/brief run [--debug] file.brf
```

`--debug` builds with sanitizers and leak checks. Compile errors exit with
status 1, usage errors with status 2.

## Layout

| Path | Contents |
|---|---|
| `src/common/` | Arena, source files, diagnostics with fixes, interning, name maps |
| `src/lexer/` | Lexer and newline filter |
| `src/parser/` | Recursive-descent parser, one file per grammar area |
| `src/ast/` | Syntax tree printer and child traversal |
| `src/sema/resolve/` | Name resolution into the checked tree |
| `src/sema/check/` | Type checking and return inference |
| `src/sema/flow/` | Flow framework; definite assignment, outcomes, permissions |
| `src/ir/lower/`, `src/ir/rc.c` | Lowering to a linear IR; ownership insertion |
| `src/codegen/c/` | C emission |
| `src/driver/` | Staged `Compilation` API and the native build driver |
| `runtime/` | The C runtime embedded in every program |
| `tests/` | Unit suites, program fixtures (`tests/programs`), parser fixtures (`tests/parse`) and document audits |
| `examples/` | Complete programs, run in CI |

Each pass keeps its private declarations in `src/<pass>/internal.h`.

## Documents

- [`docs/lang-desc.txt`](docs/lang-desc.txt) and the other `docs/lang-*.txt`:
  the current language. [`docs/lang-post-0-notes.txt`](docs/lang-post-0-notes.txt)
  records the decisions behind it.
- [`docs/plans/architecture.txt`](docs/plans/architecture.txt): the active plan.
- [`docs/next/`](docs/next/): sketches of the next language; not compiled.
- [`tests/doc-acceptance.md`](tests/doc-acceptance.md): which document examples
  the compiler accepts.
- [`docs/history/`](docs/history/): earlier versions, plans, reviews and the
  previous detailed status of this README. Not maintained.
