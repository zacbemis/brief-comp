# M6 first-version review and completion decision

The review is anchored to `docs/lang-desc.txt`, **First version**, and the goal
and M6 definition of done in `docs/plans/first-version.txt`. The eleven scope
bullets have an existing evidence map in [doc-overview.md](doc-overview.md);
[doc-acceptance.md](doc-acceptance.md) inventories all thirteen chapter audits.
Neither map, the closed initial diagnostic inventory, nor passing tests alone
establishes M6 completion.

The first checkpoint reviewed the scope/goal, README and plan claims, the
overview, error/function/type audit records, and recursive checking/member
lookup against implementation and independent probes. It was interrupted by
the correctness finding below. The second checkpoint advanced adapter/checker
review without declaring completion. These historical checkpoints are retained
here; the final boundary review and completion decision appear at the end.

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

## Second checkpoint: adapters and remaining checker error paths

The adapter/status cross-check now includes pipelines, symbols, memory, syntax,
the raw base sample, standard-library, modules, collections and traits,
alongside the previously reviewed overview/error/function/type records. It
compared the records with their CMake extraction/adaptation scripts and shared
harness, including the raw sample's reconstruction, partial-method inventories
and localized bodies. The distinctions remain important:

- Same-file associated functions and deterministic DTO getters do not test
  module lookup/privacy, JSON parsing/serialization or filesystem access.
- Local slots test retention/return/discard behavior, not vectors or collection
  operations. The cursor loop is written scaffolding, not generic iteration
  lowering. Transplanted ordinary impl methods do not establish trait dispatch
  or conformance.
- Const-to-let adapters do not establish const permissions; recursive type
  declarations and acyclic heap chains do not establish cycle collection or
  count elimination. Identity calls removed from equality fragments stay
  excluded rather than being credited to fieldwise equality.
- Parsing, extracted runnable fragments, representative prose cases, partial
  adapters and unsupported-feature rejection remain separate evidence classes.

This pass found a stale count in `doc-pipelines.md`: the shared harness has
fourteen failure guards, not eleven. The generated inventory is unchanged:
450 acceptance cases, separate from 87 checked-in native fixtures and the
syntax-only parser inventories. No language-reference changes were needed.

The implementation cross-check followed expression checking, explicit numeric
conversion anchoring, scalar/string coverage, symbol coverage/payload binding,
and read-only permission flow. Independent exploratory probes of recursive
handlers, conversions, operators and patterns found no further silent
acceptance. They are not a proof about all possible programs.

`test_sema.c` now retains seventeen focused invalid/valid pairs in anchored
recursive components:

- Pattern literal range/type errors, forbidden string range patterns, missing
  symbol tags, payload-shape mismatches and conflicting named-set payloads.
- Nonexhaustive scalar/symbol matches and duplicate scalar/string/symbol arms.
  Coverage diagnostics are emitted outside the checker's local error counter;
  these cases independently verify that those paths still reject.
- `?`/`!` on data-only sets, invalid unary/binary operands, and numeric
  converter arity/source-domain errors. Valid counterparts keep data-only
  matches, ordinary logical operators and explicit conversion behavior.

Each pair runs in both declaration orders and with LF/CRLF, then rechecks the
same resolved program. Negatives require one exact diagnostic and byte span,
unchanged non-error recursive anchors, unchecked status and rejected lowering;
positives require matching anchors, checked status and successful lowering.
The converter source-domain negative discards its invalid conversion result
before returning the recursive value, isolating that diagnostic from an
independent return-type conflict. A converter used as a tail result is itself
an independent numeric anchor; incompatible nominal/numeric return anchors
still reject rather than being reinterpreted as a valid conversion. No compiler
or runtime behavior was changed in this checkpoint.

Validation was rerun sequentially in GCC Debug ASan/UBSan, Clang Debug
ASan/UBSan, GCC Release and unsanitized GCC Debug: 50/50, 50/50, 50/50 and
36/36 tests passed. `test_sema` now has thirteen groups; the local document
label passed 27/27. The same 333 non-compile-error native/generated sources
again emitted byte-identical C to the previous checkpoint and compiled under
strict GCC and Clang flags. Compilation/execution deadlines remain 30/10
seconds; no deadlines or feature scope were widened.

## Final checkpoint: lowering, ownership, runtime and native boundaries

The remaining review followed `src/ir/lower.c`, `src/ir/rc.c`,
`src/codegen/c.c`, `runtime/brief_runtime.c`, `src/driver/native.c` and
`src/main.c`, comparing the implemented paths with README/plan claims and the
independent stage, runtime and native-driver tests. The eleven-bullet scope map
remains in `doc-overview.md`; this pass checks the back-end boundaries behind
that evidence rather than crediting the chapter stand-ins with extra features.

| Boundary | Implementation and independent evidence |
|---|---|
| Sequencing and control flow | Lowering materializes operands/arguments before calls, captures field targets and compound-assignment old values before RHS work, branches for short-circuit logic, and evaluates range bounds once before their loop. Loop jumps carry lexical cleanup boundaries; `for` continues pass through the increment. `struct-evaluation-order.brf`, `borrow-lifetimes.brf`, `control-flow.brf`, `pipelines.brf` and match/ownership fixtures observe these rules. |
| Owned versus borrowed values | Lowering records owners and exit drops; the separate idempotent RC pass pins reference reads, retains before replacement, releases reused temporary slots and transfers return ownership. Emission refuses IR without ownership insertion. `test_compile`, `test_ownership`, `test_symbols`, `test_errors` and `test_conversions` inspect the stages independently; native/debug fixtures exercise cleanup and argument snapshots. |
| Symbols, propagation and entry | Symbol casts repack the active payload; retain/release helpers follow only its tag. Propagation narrows to failures before converting to the enclosing outcome and cleaning other owners. Entry drops ordinary/data-only results and formats/releases active fallible errors. Unit fields/parameters omit C storage without dropping source effects. Existing outcome/unit/entry fixtures and merged-output/bounded-formatting tests cover these distinctions. |
| Runtime safety and limits | Checked integer arithmetic guards operations before evaluating them; floating and text conversions validate bounds/grammar before casts/parsing. Binary strings have explicit lengths. Allocation/count checks, immortal literals and queued field destruction match the documented single-threaded ARC model. Independent arithmetic/conversion oracles and runtime live-count tests remain separate from native acceptance. No cycle collection, count elimination, serialization or general stack-exhaustion guarantee is inferred. |
| Native publication and failures | Main rejects frontend/semantic diagnostics before lowering/native compilation. The driver invokes one CC executable without a shell, stages beside the destination, publishes by rename only on success, protects source inodes, restores child SIGINT state and cleans temporary files. Relocated-compiler, path/argument, status, signal, source-protection and publication tests exercise these boundaries directly. |

The boundary pass itself found no production-code defect. It added focused
regression coverage rather than changing the implementation:

- `error-ownership.brf` now exercises a pinned symbol/string argument before a
  later argument mutates its source and either succeeds or propagates failure.
  The callee's trace occurs only on success; the returned snapshot still names
  the old string. It also checks a heap-owning loop condition that succeeds or
  propagates an owned error, with surrounding owners and a body `continue`.
  Normal/debug execution requires exact output and clean ownership exits.
- `test_cli_native.c` now checks hard-link source protection before invoking
  the recording CC, compiler success without an output file, compiler signal
  termination, failed rename onto an existing directory, a signaled program's
  `128 + signal` status and semantic rejection from `run` before CC. Existing
  outputs/marker files remain intact and temporary directories are empty.
  `fake_cc.c` supplies controlled missing-output/signal modes, not new compiler
  behavior. Existing partial-output failure and terminal Ctrl-C tests remain.

## Follow-up fixes before completion

A later whole-compiler probe exercised every scope bullet with new programs,
about seventy programs that must be rejected (including fourteen read-only
permission laundering routes), runtime arithmetic/text edges, C name collisions
and compiler stress inputs. It found three defects, now fixed, and one
undocumented limit:

- **Deep release.** Destroy callbacks released fields recursively, so dropping a
  long acyclic chain built by an ordinary loop exhausted the C stack: under
  50,000 links with `--debug` and about 1,000,000 in a normal build.
  `brt_release` now queues objects whose counts reach zero during another
  destruction and drains them in one loop, so every object is still freed
  before the outermost release returns. `test_objects.c` releases a
  1,000,000-object chain and a 4,096-leaf tree; `structs.brf` drops a
  200,000-link chain in normal and debug runs.
- **Literal cascade.** An unknown type in a binding, parameter, field or return
  set reports its own error, but an integer literal in that context then fell
  back to `int` and could add `Number literal is outside the range of its
  type.`. Literals in an already-diagnosed context still take their default
  type, so independent errors remain checked, but no longer report a range
  error against it. `test_sema.c` pins six contexts and an independent range
  error.
- **Missing compiler output.** A C compiler that exited 0 without writing its
  output was reported as `cannot write executable` by `build` and as an exec
  failure with status 127 by `run`. Both now report `C compiler did not produce
  an executable` and exit 1; `test_cli_native.c` checks both commands.
- **Syntax nesting limit.** The parser's 128-level limit keeps the compiler's
  recursive passes within its stack. It counts each operator in a chain, so one
  expression can chain at most 126 binary operators. The README now documents
  the limit; it is unchanged.

A `chr` cannot currently be converted to `str` or produced from a number. That
matches `lang-types.txt` and is a later design question, not a defect.

Fresh sequential configurations passed after these fixes: GCC Debug ASan/UBSan
**50/50**, Clang Debug ASan/UBSan **50/50**, GCC Release **50/50**, and GCC
Debug with sanitizers OFF **36/36**. The local document label passed **27/27**.
All **333** non-compile-error native/generated sources compiled under strict GCC
and Clang flags. Compared with the pre-fix compiler on the same sources, their
emitted C differs only in the embedded runtime's release queue. Compile/run
deadlines remain **30/10 seconds**. The inventory remains **450** generated
acceptance cases, **87** checked-in native fixtures, **146** document parsing
cases and **106** checked-in parser fixtures.

## Completion decision

**M0–M6 are complete for the first-version scope in `docs/lang-desc.txt`.**
The decision combines the explicit chapter/example classifications, intended
error checks and diagnostic improvements, implementation-to-scope review,
independent boundary regressions, the follow-up fixes, bounded validation
matrix and reconciled README/status claims. It is not based only on passing
tests or an empty gap inventory. No known in-scope blocker remains from this
review.

This is a scoped first-version acceptance decision, not a proof that every
possible program is correct or a production-readiness/security claim. Partial
fragments remain partial; single-file local stand-ins do not implement their
deferred surroundings. Modules/privacy, user generics, collections/lambdas,
traits, const, identity/clone, general nested/refutable payload patterns,
symbol ordering, cycle collection and the other recorded limitations remain
outside this sign-off. Future defects should be tracked as compiler bugs,
without redefining the completed scope or treating this review as infallible.
