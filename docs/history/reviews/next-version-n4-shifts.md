# N4 first slice: independent integer shift counts

Base: `ad96a16`, committed unchanged. N4 starts with the bounded operator
change from its Milestones/Design sections. The checklist is beside N3's list.
Bindings/definite assignment, let-else, inclusive ranges, typed loop variables,
audit extensions and the milestone closure matrix remain separate slices.

## Semantics and implementation

Both operands of `<<`/`>>` must be integers, but their widths and signedness may
differ. `chr`, `bool`, floating types and reference/symbol types are not integer
counts or shifted values. Other numeric/bitwise/comparison operators retain
same-type checking; this exception does not add numeric coercions generally.

The left operand and its result context determine the shift's result type.
The count cannot anchor, widen or otherwise supply that type, including through
`hint()` during recursive inference. The count is checked without the left
operand's expected type. Typed counts keep their declared type; an unconstrained
whole literal defaults to `int`. Thus `f(c: ulng) { 1 << c }` returns `int`;
`f(c: int) -> shrt { 1 << c }` returns `shrt`. An unsigned left operand does not
turn a negative literal count into an unsigned-literal error; it is an integer
count that panics when evaluated. A large literal count still needs a suitable
explicit context, such as `ulng(1099511627776)`, to fit its own type.

`src/sema/check.c` handles shifts before same-type binary unification, diagnoses
invalid operands at their own spans and preserves left-to-right flow. Existing
IR already retains both operand temporary types independently; its shape and
lowering need no change. `src/codegen/c.c` receives the enclosing IR function to
inspect the actual count type. Mixed-type shifts emit negative/width checks
using that temporary before passing it to the runtime helper's signed/unsigned
64-bit count parameter. Nothing is truncated first; `ulng` maximum and `lng`
minimum cannot wrap into apparently valid counts. The operands are evaluated
once, left before right, and source location is set before either panic route.

Same-type shifts retain their existing helper checks and emitted spelling.
Checked left shift is still multiplication by a power of two, including for
negative values; overflow still panics. Signed right shift remains arithmetic.
Negative counts and counts at least the left width panic even for zero values.
Runtime helpers, parser/AST, IR/RC, shared flow, permissions, diagnostic
support and language chapters are unchanged. Invalid operands get no guessed
fix.

## Every changed existing expectation

**No committed assertion or runtime expectation changes.** All existing C-test
sources, Brief fixture bytes/markers, goldens, `.expect` files, diagnostics,
parser classifications and generated audit programs remain intact. Retained
ASTs, accept/reject results, diagnostic text and runtime observations all match.

Three retained emitted-C outputs intentionally change, and only in these ways:

1. `tests/programs/numbers.brf`: the count `63` in `l >> 63` on line 30, counts
   `15`/`63` in `us >> 15`/`ul >> 63` on line 31, and `63` in the annotated
   `1 << 63` on line 33 now have `int` temporaries rather than the left type's
   `lng`/`ushrt`/`ulng`/`ulng`. Their original result types are unchanged. Each
   mixed-type operation adds its own count guard before the existing helper.
2. `tests/programs/panics/shift-count.brf:3`: count `64` changes from `ulng` to
   `int`, with the mixed guard. Its invalid-count panic text/location/status
   remains exact.
3. `tests/programs/panics/shift-overflow.brf:3`: count `31` changes from `uint`
   to `int`, with the mixed guard. This count is valid; the same checked helper
   still produces the integer-overflow panic at the same source location.

An independent normalization proof removes just those six temporary-type
changes and six guards and obtains the exact old C bytes. All other previously
successful C outputs are byte-identical. These are not weakened golden tests.

New independent probes establish all 72 typed pair/direction combinations:
60 previously rejected mixed-type combinations accept; twelve same-type cases
retain exact emitted C. New tests also establish count-independent literal and
recursive inference. They are not substituted versions of old negative tests.

## New coverage

`test_shift_counts.c` adds six groups:

- All six integer widths/signedness choices in both operand positions and both
  directions, LF/CRLF and repeated checking. Semantic result/operand types and
  IR result/count types are pinned independently. RC remains idempotent and
  C emission places the guard only on mixed-type operations.
- Contextual left literals, independent count defaults, nested shifts, aliases,
  explicit result contracts and rejection of count-only recursive anchoring.
- Negative literal counts on unsigned values, counts exceeding a narrow left
  type's range, and a large count literal outside its own default `int` range.
- Noninteger operands at their own spans, no guessed fix and blocked lowering.
- Same-type requirements preserved on addition, bitwise-and, power/comparison.
- Unknown-name recovery, forbidden unhandled call/binding reads and recursively
  inferred results in both function orders, without count-driven type cascades.

Successful checks retain source bytes across rechecks. Invalid inputs pin one
primary, message and source start, with no fixes or IR. Resolution-error cases
resolve freshly when repeated; semantic cases recheck the resolved tree.

Ten fixtures bring the native inventory to **208**:

- `shift-counts-n4.brf`: all 72 pair/direction checks, literal/context examples,
  minimum/high-bit edges, arithmetic right shifts and single left-to-right
  effect evaluation (`LR`). Unexpected values print failures, so exact stdout
  checks every comparison as well as the final success line.
- `errors/shift-counts-n4.brf`: fourteen exact rejection sites for invalid left
  values/counts and unchanged other-operator restrictions; no fixes or notes.
- Eight `panics/shift-*-n4.brf` fixtures: seven invalid-count panics and one
  valid-count left-shift overflow, with exact text/status/source locations.
  Counts include a signed negative with unsigned value, 2^40 in signed/unsigned
  types, unsigned 64-bit maximum, signed 64-bit minimum and width boundaries.

No document fence is promoted in this first slice. The current types audit's
sources/counts remain unchanged; its broader N4 extension remains on the list.
No fragment extraction, API stand-in, migration bridge or new runtime is added.

## Validation

Default GCC Debug ASan/UBSan and fresh Clang Debug ASan/UBSan with `-Werror`
pass **52/52** each. This is not the four-configuration milestone-close matrix.
Pinned clang-format 23.1.3 checks changed C; diff/prose and protected-section
checks pass. The top status stays one line. Goal, Scope, Principles, Design,
Tests and Milestones remain byte-identical.

A fresh archived-HEAD baseline captures **2,414** source-create records from
thirteen semantic/compiler suites (**2,205** unique). Across **2,864 retained
sources / 5,728 AST/emit observations**, all ASTs and accept/reject/diagnostic
results match; only the three C outputs inventoried above change. **1,177** of
1,180 previously successful C outputs remain byte-identical. All **283 prior
sanitized executable observations** retain exact stdout/stderr/status, including
all previously generated chapter cases and examples.

The nine new executable sources pass **36** strict GCC/Clang normal/sanitized
native runs, plus the positive fixture's CLI debug run. The fourteen-site
negative source rejects emit/build/run before a nonexistent CC, with no
executable publication, fixes or source mutation.

Initial new-test failures selected `f()` rather than a unit literal's span and
expected cleared resolution errors to reappear from semantic rechecking alone.
The new helper now selects unique source sites and resolves frontend errors
freshly; semantic-error rechecks remain in place. No old check was changed.

Evidence, archived sources, pair probes, exact C-delta proof and logs remain in
`build/validation/n4-shifts.20261108T034630Z/`. N4 is in progress, stopped after
this first slice for review.
