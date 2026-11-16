# N3 slice: loop back edges, nesting and recovery

Base: `128492d`, committed unchanged. This slice completes the loop/recovery
checklist item. N3 remains open for scoped native audits and milestone closure.

## Implementation and semantics

The existing shared walker supplies whole-function fixed points, loop headers,
normal back edges, continues and breaks. No walker, public hook, permissions,
typing, lowering, RC or runtime source changes are needed for this slice.
Reporting-only poison and per-binding/join deduplication remain unchanged.

The outcome checker's lexical loop stack now distinguishes a loop's body from
its condition or range bounds. Its jump target is the nearest enclosing loop
whose body is being evaluated, matching resolution and the shared walker.
A break/continue in an inner condition or bound exits the enclosing body, not
the loop whose condition/bounds have not finished. Departing local obligations
are checked and cleared before the walker snapshots that edge. Inconsistent
jump edges are attributed to that target's join, not the inner lexical loop.

Range-entry provenance is now recorded after both bounds finish. They run
once, outside the repeated header; an outcome produced by either bound must
get a loop-entry witness rather than retaining its assignment/branch witness.
While conditions remain inside the repeated header. A condition may handle a
fresh value on every iteration if each back edge produces a new pending value.

Repeated consumption of an outer binding joins pending entry with handled
back-edge state and rejects once. A loop with no back edge can handle an outer
binding once; a break-only `while true` does not invent a zero-iteration path.
For loops retain their possible zero-iteration path even with literal bounds.
Locals created inside a repeated body acquire a fresh obligation per iteration;
consistent rearming of an outer var remains legal. No destructive runtime move
or permission relaxation is introduced.

## Every changed existing expectation

**None.** No committed C-test input, native marker, golden, `.expect`, runtime
status, stdout/stderr or language chapter changes. All existing sources and
assertions remain intact. Retained-source and native comparisons report zero
changes against the fresh archived base.

Independent newly added complete-source probes expose these pre-existing gaps:

1. Lower-bound `break`, upper-bound `continue`, condition `break` and condition
   `continue` inside an enclosing loop body previously accepted even when they
   discarded that body's pending local. Each now rejects at its actual jump
   with `Outcome binding is not handled before leaving its scope.` and one
   declaration note. LF/CRLF tests also retain positive copies that handle the
   local before the same jump. No guessed fix is offered.
2. When two such bound-evaluated breaks disagree about an outer binding, the
   single mixed-state primary moves from the inner `for` to the enclosing
   `while` join. Its pending-break path and declaration notes stay the same.
3. If a range bound rearms a handled outer slot, a subsequent consuming loop
   reports its mixed state with the pending-entry witness at the lower bound,
   not the pending branch ending left over from evaluating the bound.

These are new regression programs, not changed or replaced old expectations.
They use ordinary implemented functions and types, not completed chapter
fragments, extracted document code or stand-in APIs.

## Coverage

`test_outcome_loops.c` adds eight groups with LF/CRLF and three checks of each
resolved program. Valid cases preserve canonical return and local type identity,
lower, insert RC idempotently and emit C. Invalid cases pin primary counts,
messages/starts, note order/messages/starts, no fixes, immutable source bytes
and blocked lowering. Coverage includes:

- Normal/continue back edges, outer consumption in bodies and conditions,
  condition regeneration, both pending and handled header invariants, bound
  handling once and second use after a consumed bound.
- Pending values created by bounds, and jumps in inner lower/upper bounds or
  conditions, with handled controls and the actual outer join location.
- Nested for/while loops, loop-local generations, shadowing, match-arm jumps,
  short-circuit joins, propagation exits and returned bindings. The first
  inconsistent inner join remains the only primary despite later reads/exits.
- An invalid read before or inside a loop, unrelated argument type errors,
  independent local scope failures and two independently invalid outer
  bindings. Recovery suppresses cascades for one binding, not other diagnostics.
- Dead handlers create no live obligations, but dead immutable writes still
  receive the ordinary immutability error. Read-only and explicit mutable
  parameter aliases preserve their existing permissions through nested handling.
- Twenty-four nested loops converge with one primary at the innermost join;
  repeated checks reset headers and diagnostics. Nested rearming also accepts.

Three new native fixtures bring the inventory to **198**:

- `outcome-loops-n3.brf`: owned struct/string aliases survive condition-based
  regeneration, nested local handling and bound evaluation. Both continue and
  break execute from nested bounds/conditions after the old local is handled.
  Twelve source evaluations and their output remain exact, including the old
  alias printed after all replacements.
- `main-loop-outcome-n3.brf`: an owned error is returned unchanged from nested
  loops, with exact stdout, formatted stderr, status 1 and normal cleanup.
- `errors/outcome-loops-n3.brf`: fifteen exact source-located primaries, nine
  pending-path notes and fourteen declaration notes, including an independent
  argument type error. Emit/build/run reject before invoking CC.

No scoped chapter fence is promoted. Errors section 6 and the pipelines/memory
native audits are the next N3 work, followed by the final closure matrix.

## Validation

Default GCC Debug ASan/UBSan and fresh Clang Debug ASan/UBSan with `-Werror`
suites pass **47/47** each, including the focused outcome tests. The full
four-configuration matrix remains reserved for N3 closure. Changed C sources
pass pinned clang-format 23.1.3; diff/prose widths and protected plan sections
pass, including the unchanged one-line top status.

A fresh archived `128492d` build captures **2,328** source-create records from
12 semantic/compiler suites (**2,119** unique). Across **2,735 retained
sources / 5,470 AST/emit observations**, there are zero deltas: all ASTs and
**1,117 previously successful C outputs** are identical. All **256 prior
sanitized executable observations**, including examples and scoped native
audits, retain exact stdout/stderr/status.

The two new executable fixtures pass **8/8** strict GCC/Clang normal/sanitized
runs plus two CLI debug runs. The negative fixture pins fifteen primaries and
23 notes, with no fixes or executable publication; build/run use a nonexistent
CC to prove rejection happens first. Source bytes are unchanged.

Initial failures were test-only: one new fixture used a comma rather than a
newline between struct fields; two new source-location assertions selected a
parenthesized expression's interior or a prior substring. They were corrected
without changing compiler expectations or weakening existing tests.

Logs, archived baseline, retained captures, independent probes and comparison/
validation scripts remain in
`build/validation/n3-loop-recovery.20261107T063137Z/`.
