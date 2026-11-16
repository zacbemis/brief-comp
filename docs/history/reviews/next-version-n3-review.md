# N3 closure review

**N3 is complete for its milestone scope; N4 has not started.** Reviewed base:
`27d7eb1`, the loop/recovery slice, committed unchanged. This closure adds only
scoped native audits, registrations and status/review documentation. Compiler,
shared flow/permissions, IR/RC, runtime and language chapters are unchanged.

## Completion criteria

- Complete outcomes return unchanged in tail/ret, branch/match and final
  pipeline positions, with written/inferred/partial and recursive coverage.
  Written results still constrain returned errors; no nested success is guessed.
- Written let/var outcome bindings must be handled once by match, postfix or
  return. Non-handler transfers, second handling and pending scope exits reject.
  A handled var can receive a new value, with a new obligation after its RHS.
- Shared-flow joins report one primary with a pending-path/declaration witness.
  Return, propagation, break and continue check the scopes actually departed.
- Fixed points cover normal/continue back edges, outer consumption, regenerated
  values, nesting, bounds/conditions, independent recovery and deep loops.
  Ownership snapshots and read-only permissions are not upgraded by handling.
- Required native audits now exist, with unchanged `doc_native.cmake` helpers.

The [section-6 extension](../../../tests/doc-errors-n3.md) retains the N2 audit name and all
22 prior programs: two whole fences execute, 36 remain listings, and 33 prose
programs exercise available rules. [Pipelines](../../../tests/doc-pipelines-n3.md) executes
one whole fence and twelve prose programs; sixteen fences remain listings.
[Memory](../../../tests/doc-memory-n3.md) executes four whole fences and ten prose programs;
fourteen fences remain listings. All **73 fences** are classified exactly once,
with seven unchanged whole fences and 66 honest listings. No missing API/type/
body, extracted fragment or split alternative is supplied.

## Existing expectations

**Changed existing expectations in this closure: none.** Old C-test sources,
Brief fixtures, markers, goldens, `.expect` files, parser classifications and
generated audit programs are unchanged. Section 6's six native classifications
move from later/N3 to five context listings and the complete print execution;
this is coverage promotion, not changed old runtime acceptance.

Reviewed semantic inventories remain in `next-version-n3-returned-outcomes.md`,
`next-version-n3-outcome-bindings.md`, `next-version-n3-joins-exits.md` and
`next-version-n3-loop-recovery.md`. They cover returned-call admission, delayed
handling diagnostics, rearming, first-join placement and nested jumps. This
closure changes none of those contracts or their tests.

## Validation

Default `build/` rebuild and CTest pass **51/51**. Fresh builds use
`-DCMAKE_C_FLAGS=-Werror` and `ctest --output-on-failure --parallel 4`:

| Configuration | Result |
|---|---:|
| GCC Debug ASan/UBSan | 51/51 |
| Clang Debug ASan/UBSan | 51/51 |
| GCC Release | 51/51 |
| GCC Debug, sanitizers OFF | 41/41 |

Fresh archived-HEAD captures retain 2,414 source-create records from thirteen
semantic/compiler suites (2,205 unique). All **5,648 AST/C/status/diagnostic
observations across 2,824 sources** and **1,155 successful C outputs** match
exactly. All **258 prior sanitized executable observations** retain exact split
stdout/stderr/status. Old audit inputs come from the archived base, not newly
added chapter programs. There is no emitted-C delta in this slice.

Independent checks preserve all seven executed fence prefixes/bodies and all
22 prior errors programs byte-for-byte. The 21 negative audit programs have
64 exact diagnostic sites (15 new programs/55 new sites), and stop emit/build/
run before a nonexistent CC, with no executable or guessed fix. Normal and
sanitizer audit sources are identical. All 41 audit executable sources pass
strict GCC/Clang normal/ASan/UBSan: **164 native runs**, including expected
errors/panics; 25 sources and 100 of those runs are new. Pinned clang-format
23.1.3 checks all tracked C/headers; diff/prose widths and protected plan
sections pass. Initial failures/corrections affected new tests only and are
recorded in the chapter notes; no compiler check or old assertion was relaxed.

Evidence is retained under `build/validation/n3-audits.20261108T032119Z/`.

## Scope boundary

This is N3 closure, not full chapter/API or next-version acceptance. Listed
File/Json/User/collection/resource dependencies remain absent, not simulated.
N4 bindings/loops/let-else, N5 interpolation and N6 inventories remain. Modules,
traits, merged/typed/refutable matching, collections, lambdas, cloning, const,
cycle collection and implicit resource cleanup retain their recorded limits.
