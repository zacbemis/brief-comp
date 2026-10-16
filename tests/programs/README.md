# Program tests

`../run_programs.c` compiles each `.brf` with `brief build`, runs successful
binaries, and checks the expectations below. CMake builds the runner alongside
the compiler. CTest runs normal and `--debug` (ASan/UBSan) variants; no interpreter
or third-party packages are required.

- `// expect: text`: one stdout line. Expectations are collected in source
  order, so put them together at the top when execution order differs from
  source order. `\0`, `\n`, `\t`, `\r`, and `\\` represent bytes; other text
  is UTF-8. An empty expectation represents a blank output line.
- `// error: message`: compilation must fail with this exact diagnostic on
  this source line. Every diagnostic must be listed; ordering does not matter.
  No executable may be published. Do not combine these with runtime expectations.
- `// panic: message`: execution must fail and stderr must contain the message.
  Arithmetic panic fixtures under `panics/` exercise this in both normal and
  sanitized builds.
- `// exit: N`: exact program status, default 0 (or any nonzero status for a
  panic unless specified). Only one exit expectation is allowed.

Successful compilation must be silent; stdout must match exactly, and stderr
must be empty unless a panic is expected. Sanitizer reports always fail a test,
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

These are compilation/execution tests for the current subset, not the much
broader syntax-only examples in `tests/parse/` and the doc parser manifest.
