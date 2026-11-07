# N3 slice: pending and handled outcome bindings

Base: `d1e5603`, the returned-outcomes slice, committed unchanged. This slice
admits written let/var outcome bindings and checks straight-line consumption,
second handling and forbidden transfers with the shared flow framework. N3
remains in progress; richer path notes and broad loop/exit coverage come next.

## Implementation

The checker admits a whole initializer result in a distinct `BOUND` context.
It does not admit argument/payload calls or assignment as a new binding.
Written local declarations, hidden pipeline owners and variable handler reads
have separate metadata. A local is tracked only when its valid initializer and
annotation establish an actual top-level error kind. Neutral-only data,
optionals, singleton-success types and nested errors in ordinary data remain
freely reusable. The tracked obligations are written let/var declarations, not
new obligations imposed on parameters or pattern binders.

A stored result keeps its canonical type. Ordinary reads get a recovery type
inside the checker so they cannot cause spurious argument/field/inference
errors; the flow pass diagnoses the actual illegal read. Handler reads retain
the real type. Invalid annotations still report their original type error.

`src/sema/outcomes.c` runs after types stabilize, using `sema_flow_run` rather
than a second branch/loop engine. Per-variable pending/handled bits join by
union. A match, postfix or return consumes a pending binding once. A second
handling, passing/copying/storing, discarded read, or reassignment rejects with
a declaration note and no guessed fix. Reassignment is not a reset, including
for var after handling. An invalid reporting state suppresses follow-up errors
for that same obligation on the path. Probe passes do not retain poison in
loop headers, which would otherwise suppress the final diagnostic.

Hidden match owners transport the input without making a second obligation;
catch-all/wildcard matches are handlers too. Explicit matching can produce a
new value that is bound as a fresh obligation. Shadowed locals have distinct
IDs. No source spelling, migration adapter, API stand-in or parser promotion
is introduced. Permissions, shared flow machinery, IR, RC and runtime sources
are unchanged. Existing cleanup retains/releases ordinary owners as before;
linear consumption is a checking fact, not a new destructive runtime move.

## Safety guards and remaining scope

Admission cannot be safe if pending results can silently disappear on another
path. Basic guards therefore check normal lexical exits, ret, propagation's
error edge and scopes left by break/continue. Mixed pending/handled states
reject at a later use or exit. The existing loop fixed point supplies back-edge
states; fresh body locals reset at their lexical exit. Dead syntax is still
type/name checked, but dead bindings do not create live flow obligations.
Panics retain the existing non-unwinding termination behavior.

This does not close the next two checklist slices. First-join diagnostics and
notes identifying the unhandled path are not implemented here; current flow
notes identify the declaration. Comprehensive branch, jump, nested loop,
short-circuit and recovery matrices remain for those slices. The few control
cases here are admission-safety regressions, not full N3 control-flow coverage.
The errors/pipelines/memory native audits and milestone-closing matrix remain.

## Every changed existing expectation

The old call diagnostic is abbreviated below as **call error**:
`A call that can fail must be handled with ?, !, or a match.`
The new **read error** is:
`Outcome binding must be handled with ?, !, a match, or return.`
The new **exit error** is:
`Outcome binding is not handled before leaving its scope.`
Both flow errors carry exactly one declaration note and no fix.

### Checker C tests

1. `test_errors.c`: `main() { let x = f() }` still rejects once, but the call
   error at `f()` becomes the exit error at the closing brace.
2. Its recovery table's `let value = source(); take(value, true)`, object
   binding followed by `value.value`, and `flt("1")` binding followed by
   `takeFloat(value)` replace the initializer's call error with the read error
   on `value`. The strings still use their original newlines, not semicolons.
3. The table's annotated `let value: int = source()` now reports the annotation
   mismatch instead of a call error. It does not get a spurious exit error.
   Its unreachable `ret 0; let value = source(); take(value, true)` source still
   rejects once: the call error becomes `Function argument has the wrong type.`
   on `value`. There is no live obligation, but dead syntax remains typed.
4. Three original table sources now accept: bound `value?`, `value!`, and the
   exhaustive `value` match. Their explicitly matched initializer counterparts
   remain tested. Separate dead-binding controls retain normal type and
   immutability checks without creating live consumption obligations.
5. The retained metadata characterization now requires the initializer and
   variable to retain the canonical symbol set, not `SEMA_ERROR`. Its diagnostic
   moves from `source()` to the `value` argument and gains a declaration note.
   The illegal read still blocks checking/lowering and does not hide later code.
6. The two unrelated-error recovery sources each keep two diagnostics. Binding
   then `take(value, "wrong")` keeps the argument error and gains a read error
   instead of the call error. Binding then `let wrong: int = false` keeps its
   annotation error and gains an exit error. Type errors now precede flow errors
   because flow runs after checking. All other table inputs retain expectations.
7. `test_symbols.c`: the unused `let result = fail()` body, LF and CRLF, still
   rejects once; the error moves from the initializer to the closing brace and
   gains the exit message/note. The other returned and immediate-use cases stay.
8. Its nested-error extraction schema `:created[:!inner[str]] | :!outer` now
   rejects the four original `let value = f()?` / `f()!` inputs in LF/CRLF.
   Extraction exposes an error set, so the new local must be handled. Each has
   one exit error rather than succeeding. The original inputs remain negative
   checks; separately completed positive copies append an inner-error match,
   preserving the old kind/schema/IR/projection/RC/emission assertions.
9. `test_return_outcomes.c`: the original unused `let value = source()` followed
   by `()` in LF/CRLF replaces one call error with an exit error at the final
   brace, with one declaration note. Its other non-return rejection cases stay.

### Existing native fixture contracts

Counts remain nine errors in `errors/handled-calls.brf` and eleven each in
`errors/call-kind-errors-n2.brf` and `errors/returned-outcomes-n3.brf`:

- `call-kind-errors-n2`: the initializer call error on line 13 becomes an exit
  error on `ret fail()` at line 23, naming the pending `result` declaration.
  Returning a different call does not discharge that earlier binding.
- `returned-outcomes-n3`: the old initializer call error on line 15 becomes an
  exit error on the closing brace at line 20, naming `pending`.
- `handled-calls`: initializer call errors on old lines 6, 15, 17 and 24 become,
  respectively, the exit error at ret line 9 and read errors at lines 16, 18
  and 26. The last is the first projection, not the later receiver call.
- `handled-calls`' old wrong-argument error is retained. Its call is split over
  lines 18–19 to give the flow read and wrong argument distinct marker lines;
  the runner supports one error marker per physical line. The wrong-argument
  location moves from 18 to 19, and the unchanged recursive conversion-input
  error moves from 30 to 31. The binding/projection below the split moves by one
  line too. No Brief tokens change except comments and this call's whitespace.

No other existing expectation changes. No prior stdout/stderr/status/panic
marker, golden or `.expect` changes. Language chapters, quoted fences, parser
classifications, shared audit helpers and native N1/N2 cases are untouched.

## New tests and validation

Six groups in `test_outcome_bindings.c` cover all three declaration forms with
six handlers, LF/CRLF, repeated checks and exact canonical result identity.
Negative transfer/second-use/assignment/scope controls check exact messages,
spans, one declaration note, no fix, immutable sources and blocked lowering.
Other controls cover shadowing, basic loop/exit guards, nested extracted errors,
ordinary optional/data reuse, recursive declaration order, hidden wildcard
matches, owned values and read-only aliases. Successful programs lower, retain
stable result identity and pass idempotent RC insertion and C emission.

Three new fixtures bring the inventory to **192**: `outcome-bindings-n3.brf`
checks owned/borrowed delayed handling, six source evaluations, optional and
nested-error behavior, unit effects and intrinsic conversion. The owned entry
in `main-bound-outcome-n3.brf` prints `before`, the exact error symbol/struct on
stderr and exits 1. `errors/outcome-bindings-n3.brf` pins eleven independent
flow rejection sites with declaration notes and no fixes.

Default `build/` and fresh Clang Debug ASan/UBSan with `-Werror` pass **45/45**
each. This is not the milestone-closing full matrix. Pinned clang-format
23.1.3, diff/prose widths and protected plan sections pass. Initial failures
were the inventoried old expectations. A new test's intentional adjacent
strings needed parentheses for Clang's warning check; the native two-error
call needed the line split described above. The loop admission test ensures
that probe-header poisoning cannot suppress the final diagnostic.

A fresh archived-HEAD baseline captures 2,128 source-create records from ten
semantic/compiler suites (1,919 unique). All AST observations across **2,529
retained sources** are identical. Exactly **22 emit-c observations** differ:
three newly accept, four original nested-extraction inputs now reject, and 15
retain rejection with the inventoried diagnostic changes. The **1,032 remaining
previously successful C outputs** are byte-identical. All **252 prior sanitized
executable observations**, including six examples and the scoped audits, retain
exact stdout/stderr/status.

The two new executable sources pass strict GCC/Clang, normal and sanitized:
**8/8** native runs, plus two CLI debug runs. All eleven negative sites and
notes stop emit/build/run before a nonexistent CC, with no executable/fix or
source mutation. Evidence, source snapshots, comparison scripts and logs are in
`build/validation/n3-outcome-bindings.20261107T050849Z/`.
