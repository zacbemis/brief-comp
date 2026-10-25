# M6 first-version review: checkpoint, not completion

The review is anchored to `docs/lang-desc.txt`, **First version**, and the goal
and M6 definition of done in `docs/plans/first-version.txt`. The eleven scope
bullets have an existing evidence map in [doc-overview.md](doc-overview.md);
[doc-acceptance.md](doc-acceptance.md) inventories all thirteen chapter audits.
Neither map, the closed initial diagnostic inventory, nor passing tests alone
establishes M6 completion.

This checkpoint reviewed the scope/goal, README and plan claims, the overview,
error/function/type audit records, and recursive checking/member lookup against
implementation and independent probes. It was interrupted by the correctness
finding below. It is **not** a new sign-off on all chapter adapters or every
implementation path. The rest of the final review remains open.

## Finding: speculative error results could erase their own diagnostics

This program was resolved successfully and then incorrectly marked checked with
zero diagnostics, even though both functions' inferred outcomes became error
types:

```brief
P {}
f(p: P, b: bool) {
  if b { ret p }
  g(p, b)
}
g(p: P, b: bool) { f(p, b).missing }
main() {}
```

Changing the field to `.missing()` reproduced the same silent acceptance.
The checker suppressed diagnostics during inference probes but propagated the
failed lookup's error type through the component. That erased `f`'s nominal
receiver anchor; the final pass then treated the receiver as already erroneous
and suppressed the original lookup error. This is an in-scope inference/checking
bug, not a deferred feature or a reason to weaken the scope.

A second probe found the same silent acceptance for a contextual range error:

```brief
f(p: shrt, b: bool) { if b { ret p }
  g(p, b) }
g(p: shrt, b: bool) { if b { ret f(p, b) }
  40000 }
main() {}
```

After the `shrt` anchor was poisoned, 40000 defaulted to `int` and its original
range error disappeared. A member-only patch would therefore be insufficient.

The follow-up in `src/sema/check.c` tracks speculative diagnostics and intrinsic
inference conflicts locally to each expression/checking pass. An error result
from a subtree with a diagnosed, non-intrinsic probe failure defers as unknown,
so it cannot destroy the context required for final checking. Intrinsic joins
retain their existing poisoning/provenance and diagnostic behavior; inherited
frontend/callee errors without a local failure still propagate. These counters
never suppress final diagnostics and are not a global error-count shortcut.
Missing-method guidance now uses the same diagnostic entry point during probes.

The final pass reports the original diagnostic and gives invalid expressions
error recovery types. It does not invent a receiver type, use fallback for
dotted calls, change permissions, or make invalid code lower. A component
without an independent anchor still needs a written return type.

Evidence:

- `test_structs.c`: ten lookup contexts, both declaration orders, LF/CRLF,
  exact spans, stable anchors, repeat checking and rejected lowering. Self
  recursion, top-level-function advice, independent contract errors, genuinely
  unanchored recursion and frontend-error receivers have separate controls.
- `test_sema.c`: five contextual numeric domains, both declaration orders and
  LF/CRLF, full rechecks and valid boundary counterparts. Existing intrinsic
  inference conflict and independent-error tests retain their prior diagnostics.
- `tests/programs/errors/recursive.brf`: missing field, missing method, absent
  pipeline function, a locally shadowed pipeline target and contextual literal
  overflow each report the specific error; the unanchored-recursion case
  remains.
- `test_cli_ast.cmake`: CRLF locations/highlights and rejection from `emit-c`,
  `run` and `build`, before consulting an invalid `CC` or publishing an output.
- `tests/programs/structs.brf`: valid recursive method/pipeline calls and a
  symbol-indirected field projection run with owned strings and ordinary
  cleanup. The constructed Link values form an acyclic chain, not a collected
  runtime reference cycle. These cases do not establish a collection API or
  identity operation. `numeric-conversions.brf` observes the corrected recursive
  `shrt` literal case returning 32000.

## Status accuracy corrections

The plan now distinguishes current implementation from design references:

- First-version coverage uses numeric intervals, finite symbol tags and binary
  string records with ordered lowering, not a general nested-pattern decision
  tree. Nonexhaustiveness diagnostics do not yet give a missing-case witness.
- Global symbol tag IDs are stable, but different payload unions are repacked;
  zero-cost widening is not claimed.
- Parser interning does not imply that every semantic lookup is a pointer
  comparison. Top-level `const` remains unsupported.
- Numeric converters include byte `chr` sources; sanitizer program runs are
  conditional on the supported configuration.
- The document parser currently runs 146 cases, independently of 106 checked-in
  parser fixtures, 450 generated acceptance cases and 87 native program
  fixtures. Historical milestone limitations are explicitly identified as
  historical.

Language-reference rules and the eleven first-version bullets are unchanged.
General nested/refutable payload patterns, ordering, modules/privacy, generics,
collections/lambdas/traits, named tuples/maps/indexing, function values, const,
identity/clone, cycle collection and speculative library/resource APIs retain
their recorded deferred or unresolved boundaries.

## Validation for this checkpoint

Sequential GCC Debug ASan/UBSan, Clang Debug ASan/UBSan, GCC Release and
unsanitized GCC Debug configurations passed their full CTest suites (50, 50, 50
and 36 tests). The combined local document label passed 27/27 tests. All 333
non-compile-error native/generated sources emitted byte-identical C with the
pre-change compiler on the same sources and compiled with both GCC and Clang
using `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
Compilation/execution deadlines remain 30/10 seconds. This validates the slice,
not the unreviewed remainder of M6.

## Remaining review

Continue the implementation-to-scope and chapter-adapter review, checking
intended errors as well as valid behavior. Reconcile any further
current-versus-historical claims in README, the plan and audit records. In
particular, this checkpoint is not a proof that every possible error-producing
inference path is fail-closed. Run the bounded compiler/runtime matrix, all
document labels and strict emitted-C checks after any further changes. Only then
make a reasoned M6 completion decision; this checkpoint leaves M6 **in
progress**.
