# N3 slice: return outcomes unchanged

Base: `9559a4d`, N2 closure, committed as reviewed. This first N3 slice adds
outcome returns in existing `TAIL`/`ret` contexts. Delayed/linear bindings,
flow-based consumption/joins/exits/loops and the N3 native audits remain open.
The plan now has a sliced N3 checklist beside N2's completed checklist.

## Implementation and boundary

`src/sema/check.c` accepts a fallible call when its checked use is `TAIL`, as
well as when an immediate postfix/match handles it. Existing context threading
reaches `ret` values, value-producing if/match arms, final block expressions and
final pipeline stages. No persistent handled flag is added to returned calls,
so repeated checks cannot leak permission into a different context.

The complete result participates in ordinary written-contract checking and
inferred/partial/SCC joins. No tag test, payload projection or invented success
envelope is added. A returned outcome must fit the outer result, not merely an
`:ok` payload type: implicit wrapping excludes top-level error sets, while an
explicit `:ok[...]` construction still supplies intentional nesting. Neutral
values and ordinary plain successes retain their existing contextual wrapping.

Binding initializers, arguments, fields, payloads, nonfinal pipeline inputs and
discarded statements still require immediate handling of a fallible call.
Non-value-producing if-without-else branches and loop bodies remain discarded
contexts. Explicit unit-return functions still reject fallible discarded tails.
This does not implement delayed outcomes or change those existing contexts.
The shared flow framework and permissions, IR, RC and runtime are unchanged.

## Every changed existing expectation

All retained source strings stay intact. Changes below are acceptance/count/
message changes for the new return rule, not removed cases or rewritten inputs.
The unchanged rejection wording is abbreviated here as **handling error**:
`A call that can fail must be handled with ?, !, or a match.`

### `tests/test_errors.c`

1. The two `main` sources ending with `f()` and `ret f()` now accept and emit C,
   instead of reporting one handling error. Their original bodies are retained.
2. The tail match with unit in its true arm and `f()` in its false arm still
   rejects once, but now reports `Symbol tag has conflicting payload types.`
   instead of the handling error. The return arms really disagree on success.
3. The bad/good table's written `f() -> Result { source() }`, partial
   `f() -> :ok[int] | _ { source() }` and `f() { effect() }` now accept.
   Their explicitly handled counterparts remain tested with unchanged sources.
4. The same table's `f() -> int { ret source() }` still rejects once, but now
   reports `Return value does not match the written return type.`. Returning
   counts as handling; it does not authorize dropping the callee's errors.
5. The incompatible int/bool returns with final `source()` lose only the final
   call's handling error: two errors become one incompatible-return error.
   Likewise `f(value: P) { writable(value) }` loses only its handling error:
   two errors become the one unchanged read-only argument diagnostic.
6. `f() { both(source(), source()) }` has two handling errors, not three: both
   argument calls remain invalid; the returned outer call is handled.
7. All three original SCC sources now accept: one-anchor `a`/`b` loses one
   handling error, two-anchor `a`/`b` loses two, and the acyclic
   `a -> b -> source` chain loses one. Their error result types are not poisoned
   or suppressed; new tests independently inspect accumulated schemas.

### `tests/test_symbols.c`

8. Five original error-only main bodies now accept: `fail()`, `ret fail()`,
   `source.fail()`, `Source.create()` and `source |> fail()`. Each LF/CRLF input
   previously had one handling error. Repeat checks now require successful
   lowering and an error-only entry result. The other six body shapes retain
   their original one-error, span, no-fix and blocked-lowering assertions.
9. `f() -> :ok[:!problem] { :ok[:!problem] }` followed by `main() { f() }`
   now accepts; singleton normalization exposes the returned error unchanged.
10. The original recursive `f`/`g` match-forwarding source followed by
    `main() { f(1) }` now accepts instead of reporting one handling error. All
    three inferred results remain error-only on repeated checks.
11. `main() { missing()\nfail() }` now has one error instead of two: the same
    unknown-function diagnostic remains, while returned `fail()` is allowed.

### Converter, fixture and CLI expectations

12. Four `test_conversions.c` sources now accept instead of reporting one
    handling error: main tails `shrt(x)` after `let x = 1`, `flt(d)` after
    `let d = 0.1`, `int(2.5)` and `int("1")`. The `test_structs.c` copy of
    `main() { int("1") }` likewise accepts. Domains, literal range errors,
    malformed converters and nested recursive-call handling are unchanged.
13. `programs/errors/handled-calls.brf:9` removes only the obsolete `ret f()`
    handling marker: ten exact diagnostics become nine. The source line stays
    in place, with a comment explaining the new rule.
14. `programs/errors/call-kind-errors-n2.brf:23` likewise removes only the
    `ret fail()` handling marker: twelve diagnostics become eleven. All other
    messages and physical error lines remain unchanged in both fixtures.
15. The original CRLF `error-only-call.brf` in `test_cli_ast.cmake` now requires
    successful emit-c instead of emit/build/run rejection. Its original bytes,
    successful AST and source hash stay checked. A separate discarded-call
    source adds a following `()` and retains the exact emit/build/run rejection
    on line 4 before a nonexistent CC, with no executable or source mutation.
    New entry fixtures exercise actual returned-call build/run behavior.

No other existing expectation changes. No old runtime marker, panic location,
golden or `.expect` changes. No current language document, parser classification
or scoped native audit is edited, and no missing API/type/body is supplied.

## New regressions

`test_return_outcomes.c` adds seven groups. Nine forwarding shapes across
written/inferred/partial contracts and LF/CRLF produce 54 complete programs,
checked repeatedly for immutable source and stable canonical result identity.
Their forwarding IR contains calls/returns, without symbol construction, tag
inspection or payload projection. RC insertion is idempotent and C emits.

Other groups cover error-only positional schemas, unit/non-`:ok`/optional/owned
struct results, recursive declaration orders, error accumulation, unreachable
returns, mixed plain/returned successes, written widening and read-only alias
permissions. LF/CRLF negative controls pin exact messages/start spans, absence
of guessed fixes/notes, repeat checks and blocked lowering. They include dropped
errors, wrong partial payloads, accidental nested successes and non-return
contexts. An explicit nested-success control stays valid.

Four new complete fixtures bring the native inventory to **189**:

- `returned-outcomes-n3.brf`: owned success/error paths through tail, early,
  inferred, partial, branch, match, pipeline and recursive forwarding; exactly
  twelve source calls; optional/unit/non-`:ok`/positional errors; owned locals,
  struct results and a borrowed alias that survives forwarding cleanup.
- `main-returned-outcome-n3.brf`: a returned owned struct error reaches main;
  stdout is `before`, exact stderr uses `name = value`, and status is 1.
- `main-returned-success-n3.brf`: returned owned success reaches main, releases
  normally and exits 0 with stdout `before` and empty stderr.
- `errors/returned-outcomes-n3.brf`: eleven exact rejection sites, without
  fixes, for incompatible/nested return contracts and remaining non-return uses.

Initial failures were the inventoried old expectations. New test setup also
needed the real `arena_destroy` name, a parenthesized call instead of an illegal
bare block, CRLF conversion of a multiline diagnostic-search string and a
missing C string delimiter. Only new test code changed for those corrections;
no grammar rule was broadened.

## Validation and retained-source comparison

Commands run include the default `build/` rebuild, focused checks and full
`ctest --test-dir build --output-on-failure --parallel 4`. A fresh Clang Debug
ASan/UBSan build with `-DCMAKE_C_FLAGS=-Werror` also runs its complete suite.
Both pass **44/44**. The milestone-closing four-configuration matrix is not run.
Changed C passes pinned clang-format 23.1.3; diff/prose and protected-plan
Goal/Scope/Principles/Design/Tests/Milestones checks pass.

A fresh archived-HEAD baseline captures 2,026 source-create records from nine
semantic/compiler suites (1,817 unique), plus existing complete CLI sources.
Across 2,423 retained sources, every AST is identical. Only **32 emit-c
observations** differ: 24 newly accept, six lose only returned-call diagnostics,
one now reports the return-contract mismatch and one the payload conflict.
Every change is inventoried above. All **945 previously successful C outputs**
stay byte-identical. All **249 prior sanitized executable observations** keep
exact stdout/stderr/status, including six examples and N1/N2 audit cases.

Three new native sources compile and run with strict GCC/Clang warnings as
errors, normal and sanitized: **12/12**. All eleven negative fixture sites stop
emit/build/run before a nonexistent CC, publish no executable/fix and preserve
source bytes. The original CRLF CLI tail-call source independently changes from
one rejection to C emission with an identical AST. Evidence, scripts, baseline
sources and logs remain under
`build/validation/n3-returned-outcomes.20261107T043659Z/`.
