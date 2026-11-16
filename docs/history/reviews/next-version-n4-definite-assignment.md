# N4 slice: typed deferred bindings and definite assignment

Base: `ed9d146`, committed unchanged. This completes the next N4 checklist item:
local typed let/var declarations, basic definite assignment and safe ownership/
outcome integration. The separate path/loop/nesting/recovery hardening item
remains open, as do let-else, inclusive ranges, typed loops and chapter audits.

## Syntax, types and shared flow

Typed `let name: Type` / `var name: Type` may omit `= value` at a boundary.
The annotation is required without an initializer; untyped declarations, const,
missing RHS expressions and malformed same-line continuations keep their normal
syntax errors. No guessed value or type fix is offered. Ordinary casing recovery
still supplies its unambiguous lowercase fix. The AST has a null value, not a
synthetic zero/unit expression; stable dumps print the existing `none` form.
Resolution records a deferred local and retains the canonical annotation type.
Names become visible only after resolving any written initializer.

The annotation supplies assignment/literal context and normal read types, not
a runtime default. Whole-value reads require definite assignment on every live
path. Field stores do not initialize a struct binding; their object read must
already be assigned. Compound/update operations also read their old value.
An unread declaration may leave scope without assignment, including on an early
return: the Design requires assignment before reads, not an assigned exit state.

`src/sema/assignment.c` uses unchanged `sema_flow_run` hooks. Assigned and
unassigned must-facts intersect at joins. A declaration resets its generation
to unassigned; persistent headers only lose bits. A plain let write requires
definite unassignment **after** evaluating its RHS. A var can be assigned again.
Only a completed, type-correct RHS establishes assignment. Returning paths do
not restrict continuing paths. Loop headers include back edges and continues;
possible zero iterations cannot establish assignment. Loop-local declarations
start fresh each iteration. No second fixed-point implementation is introduced.

The new diagnostics are:

- `Binding must be assigned before it is read.`
- `A declared let binding can only be assigned once on every path.`
- Note: `The binding is declared here.`

They have source sites and declaration notes, not speculative fixes. Basic
recovery deduplicates each read/write category for a binding. Reporting-only
invalid-use metadata suppresses related outcome follow-ups without poisoning
persistent facts. Ordinary type errors precede flow checks and still apply in
dead syntax. Richer path witnesses, nesting and recovery are not completed
by this admission slice; the next checklist item owns their broader coverage.

## Ownership, outcomes and permissions

Lowering registers a reference slot for lexical cleanup at its declaration,
without generating a store or load. Existing generated C initializes physical
slots to zero; null pointers and tag-zero symbols are safe empty drops. Thus
normal exits, return, break and continue can clean an unassigned slot. Existing
RC stores retain the first assigned value before releasing the empty slot, and
retain replacements before releasing the old value. Scope drops clear reused
slots. This is internal ownership safety, not a readable language-level zero.
IR shape, RC insertion, generated-C machinery and runtime sources are unchanged.

A deferred outcome declaration creates no pending obligation until an actual
successful store. First assignment may initialize a deferred let; a later let
write is rejected by definite assignment. A deferred var can be rearmed after
handling, but cannot overwrite a pending value. Assigned values still need one
postfix, match or return handler; unassigned handlers get the assignment primary
rather than misleading second-handling/scope-exit cascades. New invalid uses are
marked during assignment reporting; outcome probes never persist reporting
poison. All existing initialized-binding outcome rules remain unchanged.

Permissions reset a new empty declaration to no inherited local access bit;
first assignment then propagates the RHS's ordinary read-only/writable access.
Read-only parameter aliases and projected outcome payloads stay read-only.
A fresh var replacement can regain writable access. No retain, annotation or
owned return upgrades borrowing permission. The shared walker/public hooks and
permission policy are unchanged; only new empty declarations need a reset.

## Every changed existing expectation

**None.** No old C-test source/assertion, Brief fixture byte/marker, golden,
`.expect`, AST, diagnostic, runtime result or parser/audit changes.
All retained AST/emit/status/diagnostic observations match the archived base,
and every previously successful C output is byte-identical. The prior slice's
three intentional shift-output changes are already part of this base; this
slice adds none. No old negative input is replaced by a corrected positive.

Eight new independent complete probes compare base/current: straight-line,
both-branch, unused-reference and stored-outcome programs newly accept. Read,
second-write, unassigned-outcome and pending-outcome examples now give the exact
semantic primary/note instead of their former missing-`=` parse errors. These
are new regression programs, not changed committed expectations. No document
fragment, extracted code, stand-in API or migration bridge is used.

## New coverage

`test_definite_assignment.c` adds eight groups, including LF/CRLF and three
checks per semantic input. Valid programs retain canonical return types and
source bytes, lower, insert RC idempotently and emit C. Invalid programs pin
primary count/message/start, declaration note/message/start when applicable,
no guessed fix and blocked lowering. Frontend errors resolve freshly on repeat;
semantic cases recheck the resolved tree. Coverage includes:

- Null-initializer AST/span/dump metadata, required annotation/initializer
  boundaries, unknown annotations and ordinary casing fixes.
- Scalar/reference/unit/neutral/optional types, first let writes, var updates,
  self-reads, compound reads and whole-object field access.
- If/match joins, missing paths, returning arms and a skipped short-circuit
  path. RHS effects are evaluated before checking a let's definite unassignment.
- Basic loop back-edge/continue write guards, zero-iteration read guards,
  break-only initialization, initialized vars and fresh loop-local lets.
- Registered cleanup for never-assigned slots, no synthetic store, first owned
  stores, aliases surviving replacement and RHS returns before a store.
- Deferred outcome first assignments, no never-produced obligation, rearming,
  second let writes, unassigned handlers and pending replacement/scope errors.
- Read-only/mutable parameter aliases and result projections, fresh rebinding,
  lexical shadowing, duplicates, dead reads and retained ordinary type checks.

Three fixtures bring the inventory to **211**:

- `declared-assignment-n4.brf`: exact scalar/owned output, branch/match first
  assignment, loop-local references/outcomes, continue/break cleanup, a reused
  reference slot skipped on one iteration, old aliases after replacement and
  early return before first assignment.
- `main-declared-outcome-n4.brf`: first assignment and unchanged return of an
  owned error with a struct/string payload, exact stdout/stderr and status 1.
- `errors/declared-assignment-n4.brf`: seventeen source-located primaries and
  fourteen declaration notes, covering assignment, outcome and permission rules.

Existing initialized-let immutability, native/parser audit cases and all chapter
hashes/classifications remain intact. Broader N4 audit promotion is pending.

## Validation

Default GCC Debug ASan/UBSan and fresh `-Werror` Clang Debug ASan/UBSan suites
pass **53/53** each. No full milestone-closing matrix is run. Changed C/headers
pass pinned clang-format 23.1.3; diff/prose and protected plan sections pass.
The top status stays one line; Goal, Scope, Principles, Design, Tests and
Milestones remain byte-identical.

The fresh archived-base capture retains **2,629 source-create records** from
fourteen semantic/compiler suites (**2,416** unique). All **6,170 AST/emit
observations across 3,085 sources**, including **1,350 successful C outputs**,
match exactly. All **292 prior sanitized executable observations** retain split
stdout/stderr/status, including examples and prior generated chapter programs.

The two new executable fixtures pass **8/8** strict GCC/Clang normal/sanitized
runs plus two CLI debug runs. The negative fixture rejects emit/build/run before
a nonexistent CC: seventeen exact sites, fourteen notes, no fixes, executable
publication or source mutation.

Initial new-test failures were source-position expectations for newline tokens:
in CRLF the token starts at LF, not the preceding CR. The assertions were
corrected without changing compiler behavior or old checks. A combined baseline
capture/comparison timed out under parallel suite load; the retained capture
was complete and the standalone comparison rerun passed.

Evidence and scripts are in
`build/validation/n4-definite-assignment.20261108T040602Z/`.
N4 remains in progress; this slice stops uncommitted for review.
