# N2 slice: `:none` endings and the ambiguity diagnostic

Base: `78d21b0`. Stop uncommitted for review. The scoped symbols/errors native
audits remain before N2 closure; this is not a milestone-closing matrix run.

## Rules and implementation

A function whose result contains `:none` selects it for valueless returns and
unit-valued tails. This includes empty bodies, bindings/updates, loops,
branch/match endings and calls returning unit. It applies to written, inferred
and partial results, including an empty return before a later `ret :none`.
An explicit `:ok[()]` in an optional union still selects its unit-payload
success. An ordinary binding or argument does not gain a unit-to-`:none` cast.

The checker records valueless paths separately while probing inferred results.
A later return or another recursive component member can reveal `:none`;
provisional unit and payload-less-success edges must not poison that anchor or
leave an ambiguous phantom `:ok`. Concrete payload anchors remain fixed, and
inconsistent non-optional returns still reject on the final check. Existing
structured reachability, error-only terminal paths and ordinary unit-discard
contracts are retained. Empty paths do not invent a value after a terminal loop
or return. A per-expression ending flag lets lowering evaluate all effects,
construct `:none` and reuse the normal return/cast/cleanup paths.

A union containing payload-less `:ok` and `:none` is invalid. Canonical set and
union construction refuse it before interning; resolution reports the full
type's span, including composed/nested types. Partial inference reports at its
return annotation. The exact message is:

```text
A union cannot contain both payload-less ':ok' and ':none'.
```

There is no guessed fix: removing a tag or adding a payload are different design
choices. `:ok[()] | :none` remains valid; zero payload positions and a unit
payload are distinct. `?`/`!` remove only error tags; `:none` stays neutral.
N3 returned-call acceptance and delayed/linear handling are unchanged. Code
generation, runtime and permission/flow sources are untouched.

## Every changed existing expectation

Only the 64 raw canonical inputs in `test_symbols.c` whose independent mask has
both bits 0 and 1 set change: those are payload-less `:ok` plus `:none`, across
eight tag-kind assignments and eight subsets of the other three tags. They now
require NULL rather than a canonical symbol set. The remaining 176 symbolic
rows and eight unit-normalized rows retain their existing checks.

No other existing assertion, source fixture, stdout/stderr/panic/status marker,
example golden or `.expect` file changes. Parser classifications, quoted
fences, current language documents and old audit retirement are unchanged.
New independent programs do not complete or substitute any document fence.

## Added regressions

`test_none_endings.c` adds six groups: canonical rejection/immutability and
valid unit-payload controls; twelve ending shapes in LF/CRLF; later/inferred/
partial return anchors; recursive declaration-order controls; twelve ambiguity
diagnostic sources; and no arbitrary absence conversion in value contexts.
Positive inputs check repeated semantic passes, unchanged source bytes and C
emission with RC insertion. Negative checks use fresh LF/CRLF inputs, an exact
message and start location, no fix/notes, and no lowering. Resolved programs
also get semantic rechecks. Recursive controls cover ordinary success anchors
and a provisional payload-less success replaced by absence as errors propagate.
An unreachable return and an infinite loop cannot add an ending value.

Three new native fixtures bring the inventory to 185:

- `none-endings-n2.brf`: owned success/absence paths, loop and branch endings,
  inferred empty returns, partial wide literals, explicit unit success versus
  absence, continued execution after `?` sees none, and a non-returning function
  that must compile without an invented return.
- `main-none-ending-n2.brf`: owned local cleanup, output and exit 0 when entry
  ends without a value and its written type is `:none`.
- `errors/none-endings-n2.brf`: eleven exact ambiguity diagnostics, including
  aliases, reversed/empty-bracket spellings, fields, parameters, locals, nested
  unions and inferred/partial results. No fix or executable is published.

## Validation and differences

Default build and full CTest pass 39/39. Fresh Clang Debug ASan/UBSan with
`-Werror` also passes 39/39. Changed C/headers pass pinned clang-format's
`--dry-run --Werror`; `git diff --check` and prose-width checks pass. The plan's
Design bullets and top status remain byte-for-byte unchanged.

A fresh archived-HEAD Release baseline captures 1,895 source-create records
from eight semantic/compiler suites: 1,748 unique raw inputs plus 465 complete
CLI sources. All 2,213 retained AST/status/diagnostic observations and all 157
retained sanitized executable observations are unchanged, including the six
examples and their goldens. Of 813 prior emitted C outputs, 811 are identical.
Only bank and its whole-source audit copy differ: an anchored identifier-only
comparison proves IDs 20/21/22 become 19/20/21, with every other byte unchanged.
There is no normalization of runtime observations or fixture expectations.

Four complete new/retained sources pass strict GCC/Clang normal/sanitized C
compilation and exact execution, 16/16. CLI emit/build/run reject all eleven
negative sites before a nonexistent C compiler and leave no executable.
Initial runs caught the intended old mask assertions, a provisional terminal
loop incorrectly marked as an empty path (fixed), and a new negative control's
incorrect guessed location (corrected to the existing body-span diagnostic).

Full deltas, source captures, builds, C outputs and logs are ignored under
`build/validation/n2-none-endings.20261107T032252Z/`. The unrelated `skills/`
directory is untouched. Stop here for review; native audits are the next work.
