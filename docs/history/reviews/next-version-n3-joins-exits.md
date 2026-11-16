# N3 slice: path joins, scope exits and handled-var rearming

Base: `f6db744`, committed unchanged. This slice completes the path-join/scope
exit checklist item and corrects the earlier restriction on handled var writes.
N3 remains in progress: broader loop/recovery coverage and native audits remain.

## Semantics and implementation

A mutable outcome binding can receive a new outcome **after** its previous
value has been handled. It becomes pending again only after a successfully
completed, type-correct RHS. The slot's declared/inferred type does not grow.
The RHS may itself handle the old value before the actual write. A write that
still sees pending/mixed state, a let write, compound assignment or update
remains forbidden. An incompatible RHS reports its assignment type error and
does not invent a replacement obligation. RHS return/propagation edges check
the old state, not a new value that has not been produced yet.

This corrects the first binding slice's blanket reassignment restriction; the
word “until” in `lang-errors.txt` section 6 permits this reuse. Every produced
outcome needs handling once, not every mutable slot just once for its lifetime.
Ordinary non-handler transfers and second handling without a rearm still reject.
No language document or protected plan Design bullet is changed.

Flow facts now retain a pending-path span/kind and the first inconsistent join,
in addition to pending/handled bits. The existing walker still supplies all
branches, joins and fixed points; `flow.c` and its public hooks are unchanged.
Path hooks label explicit branch/arm endings, an implicit false path, skipped
short-circuit paths, loop entry, normal back edges and jump edges. Provenance
does not drive fixed-point iteration. Reporting-only poison never enters probe
headers as a persistent fact.

A mixed fact reports at its first join, including when recovered from a loop
header before visiting the body. Each bad binding/join pair emits one primary,
then an unhandled-path note and the existing declaration note. Later reads and
exits cannot repeat that primary. Explicit branches point to the unhandled
branch/arm ending; implicit/short-circuit paths name the relevant true/false
condition. Loop notes distinguish pending entry and pending back edges.

Break/continue checks now run before the walker snapshots the jump. Departing
locals are checked and cleared there, rather than leaking their facts into an
outer scope/header. An outer pending binding is not incorrectly discharged by
a break. Break/continue edge joins are attributed to the enclosing loop, not
the last jump that happened to arrive. Return, propagation and normal block
exits continue to check their own live obligations. Returning arms do not join
fallthrough facts. All errors remain without guessed fixes.

Permissions, IR, RC and runtime sources are unchanged. The ordinary ownership
store path retains the new result before releasing the old slot. Extracted
aliases retain their owners across replacement; a new borrowed result retains
its read-only permission, while a fresh owned replacement does not inherit the
old result's permission. This is not a new destructive runtime move.

## Every changed existing expectation

1. `test_outcome_bindings.c` retains the original LF/CRLF source
   `var result = source(); result!; result = source()` (with its original
   newlines). It still rejects once, but the forbidden-write diagnostic at the
   assignment becomes `Outcome binding is not handled before leaving its
   scope.` at the closing brace. The write is legal; the replacement is not
   handled. Its one declaration note stays. A separate positive copy adds the
   final handler, and the requested `f(1)`/`f(2)` complete sequence now accepts.
2. Its original LF/CRLF `if true { result! }` source still rejects once with
   `Outcome binding is handled on some paths but not others.`. The primary
   moves from the enclosing scope's closing brace to the if expression. A new
   first note, `The outcome is still pending when this condition is false.`,
   points to `true`; the original declaration note follows it. Both note
   messages/positions, single-primary count and blocked lowering are checked.
3. Its original LF/CRLF `while true { result! }` source still rejects once with
   the same mixed-state message. The primary moves from the `result` read to
   the while join. The first note is now `The outcome is still pending on entry
   to this loop.` at `true`, followed by the original declaration note.
4. `programs/errors/outcome-bindings-n3.brf`'s `assignAfter` keeps its exact
   Brief code and physical lines. The assignment's line-32 forbidden-write
   marker moves to the closing brace on line 33, with the pending-exit message.
   Its literal error value is now a permitted fresh outcome that needs handling.
   The fixture still requires eleven primaries. Its other ten expectations stay.

Those are all changed committed expectations. No old stdout/stderr/status,
panic location, golden or `.expect` changes. All old passing C-test inputs stay
covered; no source correction replaces an old negative input. The old binding
notes are historical; this slice explicitly supersedes their “not a reset”
limitation. Language chapters, parser classifications and scoped audits stay.

## Requested regressions and new coverage

Independent complete-source probes compare archived HEAD with this compiler:

- `var r = f(1); r!; r = f(2); r!`: one forbidden-write error becomes successful
  C emission. The full result type is retained on both assignments.
- A variable-condition `while flag { r! }` with an outer pending binding:
  HEAD reports twice, at the read and function scope end. This compiler reports
  once at the while join, with pending-entry and declaration notes.
- `if flag { r! }` with the same outer binding: the one primary moves from the
  function scope end to the if join, with its missing-false-path note.

`test_outcome_paths.c` adds seven groups with LF/CRLF and repeated checks:

- Mutable slot rearming after postfixes/matches, written types, complete
  rearming on both branches, consistent pending/handled loop headers and
  intentional early-return RHS paths; pending/let/type-error counterexamples.
- If/else, match and short-circuit joins with exact primary starts and exact
  unhandled-path note spans, note order and declaration locations. A mixed var
  cannot be rescued by a later write or handler.
- One loop primary even with later reads/ret or normal scope exit; entry,
  fallthrough/continue back edges, nested joins and multiple break edges.
- Normal nested-block exits, ret, error propagation and break/continue, with
  local facts cleared before snapshots and outer obligations preserved.
- RHS exit checking and read-only alias controls after slot replacement.

Successful checks retain canonical result identity across rechecks, lower,
insert RC idempotently and emit C. Invalid inputs require exact diagnostics,
no fixes, immutable source bytes and blocked lowering. The prior binding
helper now pins the two new mixed-state notes instead of weakening their checks.

Three new fixtures bring the native inventory to **195**:

- `outcome-rearm-n3.brf`: owned struct/string aliases survive replacement;
  actual success/error values can be rebound and handled, returned replacements
  stay exact, both kinds of consistent loop header work, and both branch paths
  can produce a new pending value. RHS return and completion release the old
  slot/alias correctly; thirteen source evaluations are observed once.
- `main-rearmed-outcome-n3.brf`: handling owned success and then rearming with
  owned error gives exact stdout/stderr, entry status 1 and unchanged cleanup.
- `errors/outcome-paths-n3.brf`: fourteen exact rejection sites, six of them
  mixed joins, with nineteen path/declaration notes and no fixes.

The next checklist item still owns comprehensive loop/nesting/recovery
hardening. The loop cases here specifically cover join placement/deduplication,
value regeneration and scope departure. No N3 chapter fence is promoted and
no missing API/type/body or extracted fragment is supplied.

## Validation

Default `build/` and fresh Clang Debug ASan/UBSan with `-Werror` pass **46/46**
each. Focused outcome tests pass too. The full four-configuration matrix is
reserved for milestone closure. Changed C/headers pass pinned clang-format
23.1.3; diff/prose widths and protected plan sections pass. Initial failures
were the listed old expectations. The new long array strings are explicitly
parenthesized for Clang's concatenation warning; no check is relaxed.

A fresh archived-HEAD baseline captures 2,239 source-create records from eleven
semantic/compiler suites (2,030 unique). Across **2,643 retained sources**, all
ASTs and **1,088 successful C outputs** are identical. Only **seven emit-c
rejection observations** change: the six LF/CRLF inputs in items 1–3 and the old
native fixture in item 4. All **254 prior sanitized executable observations**
retain exact stdout/stderr/status, including examples and scoped native audits.

Two new executable sources pass strict GCC/Clang normal/sanitized checks:
**8/8**, plus two CLI debug runs. All fourteen negative sites and nineteen notes
stop emit/build/run before a nonexistent CC, publish no executable or guessed
fix and preserve source bytes. Requested probe results, matrix-subset logs,
source captures, comparison and validation scripts remain under
`build/validation/n3-joins-exits.20261107T054804Z/`.
