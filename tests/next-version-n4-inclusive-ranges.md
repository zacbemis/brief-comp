# N4 slice: inclusive range patterns and loops

Base: `822e102`, the reviewed let-else slice in this checkout. Inclusive
`..=` syntax now reaches checked patterns and integer loops. N4 remains open:
typed loop variables, scoped audit updates and closure review remain.

## Syntax and checked ranges

The lexer recognizes adjacent `..=` as one token, before considering `..`.
It continues a line like the existing range operator. Expression precedence,
non-chaining rules, syntax-depth accounting and pattern roles are retained.
The AST records inclusivity on range expressions and patterns. Dumps use
`(..= ...)` and `(range-inclusive ...)` only for the new forms; old dumps stay.
There is no migration spelling, guessed fix or first-class range value.

Loop bounds retain the existing same-integer-type rule and contextual literal
checking. Either typed bound can anchor literals; two literal bounds still
default to int. The immutable iteration name exists only in the body. Typed
iteration headers are not implemented by this slice.

Pattern normalization takes the upper endpoint's inclusive high rank, without
subtracting one. Integer and chr maxima therefore need no successor. Floating
endpoints include both representations of zero; NaN still needs a catch-all.
Equal inclusive endpoints are nonempty, reversed endpoints are unreachable,
and duplicate/overlapping arms use the existing coverage machinery. Interval
merging, subtraction, coverage and match lowering are not replaced.

## Overflow-safe iteration and jumps

The inclusive lowering path does the following:

1. Evaluate lower and upper bounds once, left to right, before installing the
   loop's jump region; store the starting index.
2. Check index <= upper once. An inverted range skips the body.
3. Run the body. Both fallthrough and continue reach the same exhaustion step.
4. If index == upper, leave without forming another value.
5. Otherwise increment by one and jump directly back to the body.

No path computes upper + 1 or increments a maximum final value. The exhaustion
comparison uses the bound's actual integer type. Half-open loops keep their
previous IR/C path, including their less-than header test.

Jumps in inner bounds and conditions still target the active enclosing body.
An outer inclusive continue reaches that outer loop's exhaustion check, not an
uninstalled inner region. Break and return use existing lexical cleanup.
Shared flow, definite assignment, outcomes, permissions, RC, C emission and the
runtime are unchanged. Flow retains its conservative loop fixed point; this
slice does not infer guaranteed stores from constant nonempty bounds.

## Named-bound safety guard

Inspection found that range coverage could read an ordinary variable endpoint
as if it were a literal: its unset integer metadata ranked as zero. For example,
`lower..2` ignored lower, while `-1..upper` treated upper as zero. Two
independent archived-base probes demonstrate those emitted native results.

Both half-open and inclusive range patterns now reject known variable bounds:

> Non-literal range pattern bounds are not implemented yet.

The diagnostic points at the first such bound, without a fix. This is an
explicit implementation boundary, not invented constant evaluation or runtime
pattern coverage. Unknown names retain their existing lookup diagnostics.
Literal range bounds keep their behavior; runtime loop bounds remain supported.
Comparison-pattern implementation and missing constant APIs are not expanded.

## Every changed existing expectation

**None.** No old C-test input/assertion, fixture byte/marker, AST expectation,
diagnostic expectation, golden, `.expect` file, parser/audit classification or
generated chapter program changes. All retained AST/emit observations and all
previous successful emitted C bytes match the fresh archived baseline. No old
negative is deleted, replaced or weakened.

There are two explicitly demonstrated newly rejected old shapes, not changes
to retained expectations: half-open patterns with a known variable lower or
upper endpoint. Their former zero-based normalization is replaced by the
unsupported-bound diagnostic above. New inclusive forms otherwise extend old
parse rejections: four independent probes now emit C, and four now receive
precise semantic errors. All ten probes are retained with the validation logs.

## New coverage

Eight unit groups in `test_inclusive_ranges.c` cover:

- Longest tokens, continued lines, precedence, AST flags/dumps and unchanged
  token/source bytes in LF/CRLF input.
- Full domains and duplicate maxima for all six integer types, repeated
  checking and canonical return identity.
- Inclusive chr endpoints, the full byte domain, signed-zero equivalence,
  NaN fallback, singleton and reversed patterns.
- IR continue targets and exhaustion-before-increment for all integer maxima,
  anchored from either bound, plus the unchanged half-open operator path.
- Dynamic bounds, same-type rules, immutable/scope rules and blocked range
  values; invalid programs cannot reach lowering.
- Mixed chained operators, wrong pattern types and exact named-bound rejection
  sites, including both old half-open endpoint shapes; no fixes are guessed.
- Conservative assignment/back-edge facts, deferred reference slots and nested
  return/continue cleanup, with idempotent RC insertion and successful emission.
- Four hundred inclusive/half-open chr intervals against all 256 input values:
  102,400 independent membership observations plus usefulness/error counts.

Three native fixtures bring the inventory to 220:

- `inclusive-ranges-n4.brf`: every integer minimum/maximum, singleton/final
  continue, equal/empty ranges, unconditional break, once-only ordered bounds,
  mutation after bound capture, nested loops/while and jumps in inner bounds
  and conditions. Owned deferred slots, shared owners and let-else handling
  exercise continue/break cleanup. Scalar patterns cover integer endpoints,
  chr, floating endpoints, signed zero and NaN.
- `main-inclusive-ranges-n4.brf`: return an owned error payload from the ulng
  maximum after initializing a deferred owner. Stdout, stderr and status 1 are
  exact, including cleanup after transferring a field's string.
- `errors/inclusive-ranges-n4.brf`: thirteen exact sites, no notes/fixes, for
  bound types, immutable writes, value ranges, empty/duplicate/wrong patterns,
  named bounds and ordinary-parameter mutation.

Initial new fixture separators were corrected to the existing newline syntax;
no parser rule was relaxed. Strict native validation also caught an unused C
head label when an inclusive body always returned. Lowering now makes entry an
explicit jump to that label, retaining the once-only check and terminal bodies
without warning suppressions or changes to C emission.

## Validation

Evidence: `build/validation/n4-inclusive-ranges.20261109T192628Z/`.

- Default GCC Debug ASan/UBSan rebuild and CTest: 56/56.
- Fresh Clang Debug ASan/UBSan with -Werror: 56/56.
- Strict GCC/Clang normal/sanitized native checks: eight successful executions
  across the two executable fixtures; every run also checks the negative file.
- Two CLI debug runs match exact stdout, stderr and status.
- Thirteen negative sites and six rejection probes block emit/build/run before
  a nonexistent CC, without fixes, source mutation or executable publication.
- Fresh archived HEAD capture: 2,990 records / 2,728 unique from seventeen unit
  suites. All 3,406 retained sources / 6,812 AST/emit observations, 1,482 prior
  successful C outputs and 298 prior sanitized runtime observations match.
- Pinned clang-format 23.1.3, diff/prose, protected-plan sections, one-line
  status and untouched old expectations pass. The unrelated skills directory
  stays untouched.

Five N4 checklist items are complete and three remain. The full
four-configuration -Werror matrix is reserved for milestone closure, not run
for this slice.
