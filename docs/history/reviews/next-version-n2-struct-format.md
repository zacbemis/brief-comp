# N2 slice: struct fields use `=` in diagnostics

Base: `4d5b9bc`. Stop uncommitted for review. The user chose to align diagnostic
struct values with Brief literals: `Box { n = 1 }`, not `Box { n: 1 }`.

## Change and expectation inventory

One separator changes in `src/codegen/c.c`'s shared generated struct writer.
Entry errors and symbol panics both use it, recursively through struct fields
and symbol payloads. Field order, braces/commas, quoting, escaping, depth/budget
limits, ownership, flush order, exit status and panic locations are unchanged.
The runtime source, checker and lowering are untouched. The plan's Design
bullet gains only the requested `name = value` clause.

Every changed existing expectation:

| File | Exact change |
| --- | --- |
| `programs/main-error-object.brf` | Its one stderr marker changes `: ` to ` = ` after Report fields `code`, `count`, `marker`, `ready`, `real`, `empty` and `detail`. All values, escapes, enclosing symbol syntax and exit 1 remain. |
| `programs/main-symbol-format-n2.brf` | Its one stderr marker changes Box's `detail: ` and `marker: ` to `detail = ` and `marker = `. Nested symbols, literal text and exit 1 remain. |
| `test_cli_native.c` | The shared-budget test strengthens `error: :!bad[Node {` to `error: :!bad[Node { left = `. Its length cap, abbreviation and clean ownership checks remain. |

No golden or `.expect` file changes. All six example goldens under
`tests/examples/` are unchanged. `programs/literal-fields.brf` deliberately
prints the string `Pair { first: 1 }`; both that source and its stdout marker
remain unchanged because it is literal text, not a rendered struct.

Two new complete C-authored ordered-stream cases render a Box inside a neutral
symbol inside an error, for both entry propagation and panic. They require
`Box { n = 1, unit = () }`, preserve the quoted string `"Box { n: 2 }"`, check
stdout-before-stderr ordering, and retain the panic call's line 5. Both normal
and sanitized variants run. These are additions, not replacements for the eight
existing merged-stream cases. Fixture and CTest inventories remain 182 and 38.

## Validation

Default `cmake --build build -j 6` and its full CTest suite pass 38/38. A fresh
Clang Debug ASan/UBSan build with `-Werror` also passes 38/38. Changed C files
pass `build/format-env/bin/clang-format --dry-run --Werror`; `git diff --check`
is clean. This is not the milestone-closing four-configuration matrix.

A fresh archived-HEAD Release baseline captures 1,895 source-create records
from eight semantic/compiler C suites, yielding 1,748 unique raw inputs plus
465 retained complete CLI sources. All 2,213 AST/status/diagnostic observations
are unchanged. Of 813 prior emitted C outputs, 369 change: an anchored check
proves every changed byte is a generated struct field separator (2,838 field
occurrences in total); the other 444 outputs are byte-identical. Source strings
and all other generated C remain unchanged, without broad text replacement.

Only two of 157 retained sanitized executable comparisons differ: the exact
stderr labels inventoried above. Status, stdout and the rest of stderr are
unchanged, including all six examples and the literal-colon control. Additional
native CLI checks cover panic formatting and bounded recursive rendering.
Evidence and complete comparison deltas are ignored under
`build/validation/n2-struct-format.20261107T030455Z/`.

N2 remains open. Next: `:none` endings and the payload-less `:ok`/`:none`
ambiguity, then scoped native audits. N3 handling is not advanced. The unrelated
untracked `skills/` directory is untouched.
