# N2 closure review

**N2 is complete for its milestone scope; N3 has not started.** Reviewed base:
`c02b91b`, the optional-endings slice, committed unchanged. This closure adds
only scoped native audits, registrations and status/review documentation; it
makes no compiler or runtime change.

## Completion criteria

- Kind-based outcomes, intrinsic converter errors and mandatory call handling
  have unit/native/CLI coverage. Neutral-only calls remain ordinary data.
- Error-only and mixed postfix tests cover unconditional propagation/panic,
  scalar/unit extraction and tagged neutral remainders. `:none` never triggers
  early error propagation; operands without errors reject without guessed fixes.
- Exact-return and canonical singleton-`:ok` tests cover all type positions,
  owned values, nested union widening, contextual wrapping and rejection of
  invented call envelopes. Calls return their canonical type.
- Optional-ending tests cover written/inferred/partial results, recursive
  anchors, effects and cleanup. Ambiguous payload-less `:ok`/`:none` unions
  reject; a unit payload remains different from no payload.
- Entry/formatter tests check the active outer kind, status, escaped positional
  and nested payloads, `name = value`, output ordering, bounds and cleanup.
- Both required audit entry points now exist using `tests/doc_native.cmake`,
  with the same unchanged whole-fence/prose/listing discipline as N1.

[Symbols](doc-symbols-n2.md) classifies all 15 fences: one unchanged local alias
runs, 14 remain listings, and eleven independently authored programs exercise
available prose rules. [Errors](doc-errors-n2.md) classifies all 38 fences: one
unchanged alias runs, 37 remain listings, and 21 independent programs exercise
sections 1–5 and 7–12. Section 6's six fences are explicit N3 listings, not
native acceptance. All 53 fences are classified once, without supplying missing
types/APIs/bodies, splitting fences or counting parser checks as native proof.

## Existing expectations

**Changed existing expectations in this closure slice: none.** All old fixture
sources, markers, goldens, `.expect` files, N1 generated programs and parser
classifications are retained. The only registration change for N1 is a default
milestone parameter; its test names, source paths and execution options remain.
Shared native helpers and compiler-free manifest guards are unchanged.

The reviewed N2 changes are inventoried in the plan's individual increments:
reserved `:none`, converter kinds, entry status, symbol spelling, error-only
postfixes, no-error postfixes, exact results, struct spelling and ending
ambiguity. The call-kind and mixed-postfix slices changed no prior expectation.
Detailed inventories for the final four semantic/formatting slices remain in
`next-version-n2-{no-error,exact-returns,struct-format,none-endings}.md`.
This closure makes no additional exception to those reviewed contracts.

## Validation

Default `build/` rebuild and full CTest pass **43/43**. Fresh configurations use
`-DCMAKE_C_FLAGS=-Werror` and `ctest --output-on-failure --parallel 4`:

| Configuration | Result |
|---|---:|
| GCC Debug ASan/UBSan | 43/43 |
| Clang Debug ASan/UBSan | 43/43 |
| GCC Release | 43/43 |
| GCC Debug, sanitizers OFF | 35/35 |

A fresh archived-HEAD baseline captures 2,026 source-create records from nine
semantic/compiler suites (1,817 unique), plus retained complete CLI inputs.
All **4,710 AST/C/status/diagnostic observations across 2,355 sources** match
exactly. All **205 sanitized executable observations** retain exact split
stdout/stderr/status. There is no emitted-C delta in this slice.

Independent checks verify both executed fence prefixes byte-for-byte and all
53 classifications. The 12 negative prose programs have 16 exact diagnostic
sites and stop emit/build/run before a nonexistent CC, with no executable or
invented fixes. All 22 new executable sources pass strict GCC/Clang, normal and
ASan/UBSan: **88 native runs**, including expected errors/panics. The normal and
sanitizer audit sources are identical. Pinned clang-format 23.1.3 checks all
tracked C/headers; diff, prose widths and protected-plan-section checks pass.

Evidence, including matrix logs, source captures, comparison/byte-check scripts
and strict-runner logs, is retained under
`build/validation/n2-audits.20261107T041359Z/`.

## Scope boundary and next work

This is N2 closure, not full chapter/API acceptance or completion of the plan.
N3 returned-call acceptance and delayed/linear bindings remain unchanged. N4
bindings/loops/let-else, N5 interpolation and N6 inventories are still pending.
Modules, merged/typed/refutable matching, collections, lambdas, generic APIs,
multi-position success extraction and cycle collection retain their boundaries.
Declaration-order symbol comparison and schema consistency across disjoint sets
are not established by the scoped symbols audit. Listed examples stay
unaccepted until their real dependencies exist, not rewritten to make them pass.
