# Program tests

`../run_programs.c` compiles each `.brf` with `brief build`, runs successful
binaries, and checks the expectations below. CMake builds the runner alongside
the compiler. CTest runs normal and `--debug` (ASan/UBSan) variants; no interpreter
or third-party packages are required.

- `// expect: text`: one stdout line. Expectations are collected in source
  order, so put them together at the top when execution order differs from
  source order. `\0`, `\n`, `\t`, `\r`, and `\\` represent bytes; other text
  is UTF-8. An empty expectation represents a blank output line.
- `// stderr: text`: one exact stderr line, with the same byte escapes as
  `expect`. Use `// exit: 1` for an error-valued entry point. Cannot be combined
  with compile-error or panic expectations.
- `// error: message`: compilation must fail with this exact diagnostic on
  this source line. Every diagnostic must be listed; ordering does not matter.
  No executable may be published. Do not combine these with runtime expectations.
- `// panic: message`: execution must fail and stderr must contain the message.
  Arithmetic panic fixtures under `panics/` exercise this in both normal and
  sanitized builds.
- `// exit: N`: exact program status, default 0 (or any nonzero status for a
  panic unless specified). Only one exit expectation is allowed.

Successful compilation must be silent; stdout must match exactly, and stderr
must be empty unless exact stderr or a panic is expected. Sanitizer reports always fail a test,
including reports after an expected compilation error. Compilations and programs
have 30-second and 10-second timeouts respectively. The shared POSIX process
helper captures binary stdout/stderr without pipe deadlocks, and kills the
process group on timeout. Each captured stream is limited to 16 MiB. All
executables live in disposable test directories.

Run manually, using a single C compiler executable:

```sh
./build/tests/run_programs --brief "$PWD/build/brief" --cc /usr/bin/cc \
  --programs tests/programs
```

`print` accepts only `str`; `str(number)` and `str(bool)` now make values
observable directly. Older numeric tests also use comparisons and control flow. `numbers.brf`,
`control-flow.brf`, and `returns.brf` cover contextual literals, all numeric
widths, evaluation order, loops, and recursive return inference.
`float-literals.brf` covers single-rounding and finite/underflow boundary cases;
negative fixtures reject updates in value contexts and preserve independent
inference diagnostics. The runtime's
checked helpers also have exhaustive small-width and boundary/panic unit tests.

M4 fixtures exercise heap strings and binary-safe comparisons; all numeric/bool
string conversions; nominal structs, methods and `Self`; shared aliases and field
replacement; target/initializer evaluation order; field arguments invalidated by
later arguments or mutable callees; owned returns and discarded entry results;
scope/loop/early-return cleanup; fieldwise equality including NaN; generated-name
collisions; and a panic with live heap objects. Negative fixtures check fields,
conversion boundaries, and mutation permissions. Read-only alias fixtures cover
local/field copies, reassignment, branch joins, loop backedges/exits, and fresh
rebinding without changing the caller's original object. Call results whose type
can reach a read-only argument, and literals built from one, stay read-only;
results that cannot hold it are writable. `stored-references.brf` and its error
fixture check that a read-only struct is never stored in a field. Every
executable fixture runs in
normal and sanitizer configurations, and normal debug exit reports remaining owners.

The first M5 slice adds `pipelines.brf`, `scalar-matches.brf`,
`integer-matches.brf`, `match-ownership.brf`, and two negative match fixtures.
They check method-first lookup, numeric argument contexts, input/callee/argument
order, interval overlap and boundaries, all shrt/ushrt values, signed zero,
NaN/infinities, match results and nonlocal jumps, and read-only propagation.
The next slice adds `symbols.brf`, `symbol-groups.brf`, `symbol-ownership.brf`
and two negative symbol fixtures (68 total fixtures). They cover data-symbol
payloads, aliases/composition, subset transfers, recursive set inference, group
matches and narrowed catch-alls, payload-aware equality including NaN, contextual
numeric payloads, active-payload ARC, argument snapshots, loop/jump cleanup and
read-only propagation through symbols. The outcome slice adds nine fixtures
(77 total): pure-success ABI erasure, explicit/implicit successes, recursive
error inference, partial annotations, stage handlers, early-return ownership,
propagation from initializers/conditions, argument pinning, exact entry error
formatting/status/cleanup, ordinary data-only symbol entry results (discarded with
exit 0), symbol-valued successful entry results, symbol panics
and their original source location, and exact handled-call/propagation errors.
The conversion slice adds six fixtures (83 total). `conversion-matrix.brf`
executes every numeric source/target pair and all eight text targets in one
program. The others cover exact integer narrowing, rounded floating conversions,
numeric/text range errors, contextual literal arguments (including pipeline
inputs), direct-precision floating parsing, original binary invalid payloads,
propagation/panic/entry handling, argument snapshots and loop/return cleanup.
String literal patterns, refutable payload patterns and symbol ordering remain
unsupported; M5 is not complete.

These are compilation/execution tests for the current subset, not the much
broader syntax-only examples in `tests/parse/` and the doc parser manifest.
