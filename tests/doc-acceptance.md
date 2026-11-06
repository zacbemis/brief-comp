# Document acceptance status

**N1 is complete for its milestone scope; N2 is in progress.** The
[final review](next-version-n1-review.md) checks all three completion criteria.
First-version native document audits and all spelling bridges have been retired
as required by `docs/plans/next-version.txt`. Executable scripts and CTest
registrations are deleted; they are not maintained as a second syntax or
extended into a migration system. `docs/v0/` is untouched.

The replacement [current-document parser inventory](doc-parser.md) reads all
13 current chapters: 239 fences, 162 parser checks, 77 explicitly listed fences
and two zero-fence listings. Parser checks are not native acceptance. The
retirement left an intentional native document-coverage gap. The first scoped
syntax/functions/types/modules audits and raw-base inventory cover available
N1 rules, not the later native/API requirements.
Six complete independent examples now run in normal/sanitized CI too; program
fixtures and independent C/compiler/native-driver tests remain active.

```sh
ctest --test-dir build -L doc-parser --output-on-failure
```

## Replacement native audits

The [current functions native audit](doc-functions-n1.md) is now active for six
complete fences: four execute and two reject their documented return contracts.
Eight independent prose programs bring it to 14 cases (eight executions, six
compiler rejections). All 27 fences are classified; 21 remain explicit listings,
not native/API acceptance. No missing type/API/body is supplied to a fence.
Normal/sanitized variants and compiler-free manifest guards run with:

```sh
ctest --test-dir build -L doc-functions --output-on-failure
```

The [current types native audit](doc-types-n1.md) now checks eight unchanged
statement fences: six successful runs, the first overflow panic, and all three
explicit-conversion diagnostics. Twelve independent prose programs bring it to
20 cases: 17 native executions (five expected panics) and three rejection
programs. The other 20 fences remain explicit context/later-feature listings.
No inputs, APIs, split alternatives or replacement bodies are supplied.

```sh
ctest --test-dir build -L doc-types --output-on-failure
# All scoped audits and shared guards:
ctest --test-dir build -L doc-native --output-on-failure
```

The [current syntax native audit](doc-syntax-n1.md) now classifies all 35
fences: two execute unchanged, five reject for their documented errors, and 28
remain listings. Thirteen independent prose programs bring it to 20 cases
(eight executions and twelve rejection programs), including local casing,
keyword roles and exact old-spelling fixes. Mixed scopes and unavailable APIs
receive no context substitutions. An existing main is preserved without adding
a duplicate entry. Run this audit with `-L doc-syntax`.

The [current modules native audit](doc-modules-n1.md) checks five unchanged
fences: struct/alias/constructor executions, the pub-type diagnostic with its
exact comment-preserving fix, and a file-level-statement rejection. Fifteen
independent prose programs bring it to 20 cases (seven executions, thirteen
rejection programs). All 19 fences are classified; 14 remain explicit listings.
No module loader, privacy boundary or missing API is simulated. Available
module-name syntax has casing checks, but filesystem case/discovery requires
the out-of-scope loader. Run this audit with `-L doc-modules`.

The [raw-base inventory and examples](doc-base-n1.md) now preserves the entire
323-line unfenced sheet as one unaccepted raw listing. Nothing is extracted,
stubbed or rewritten into a passing program. The independent six complete
examples run normally and under sanitizers, with pinned source hashes and
committed byte-exact output/status markers. They are not native acceptance of
the raw sheet. Use `-L doc-base` or `-L native-examples` to run this slice.

All five N1 audit entry points exist and final completeness review closes N1.
This is bounded coverage, not full syntax/function/type/module/API acceptance.
Listed cases return with their actual dependencies, not rewritten v0 scaffolds.
Current module-name grammar and reserved-mod behavior are covered; external
filename/discovery rules stay with the bootstrap module loader.

N2's first slice rejects nonempty `:none` payload lists in types, values and
patterns, including `()`. Empty `:none[]` remains the payload-less form. These
unit/native regressions do not promote a fence or complete a N2 chapter audit.
The next slice gives fallible converters local intrinsic error kinds and rejects
collisions with explicit neutral data declarations, without reserving the names
for infallible casts or supplying APIs to a fence. Outcome classification,
propagation, exact returns, implicit endings and entry policy/formatting still
await their N2 slices.

Remaining milestone work:

- **N2:** symbols and errors sections 1–5 and 7–12.
- **N3:** error handling section 6, pipelines and memory.
- **N4/N5:** add bindings/loops and interpolation fences to their chapter audits.
- **N6:** overview, traits, collections and standard library scope inventories.

N1's `:!name` syntax/kind checks are not the N2 native audit. Entry status,
propagation, outcome classification, `:ok` equivalence and runtime formatting
still use the first-version rules. The explicit N1 error-only entry fixture
exits 0 until N2 changes and reviews that expectation. Bracketed payload lists
compile with positional data/failure payloads, preserving zero/one behavior.
Multiple `:ok` positions still stop at the single-result success boundary.
Union types and ordinary `Sym` names also preserve first-version outcome rules.
Type-context and whole-fence parser checks do not complete a native audit.
Typed symbol payload binders retain annotations for parser checks, but native
compilation still reports that typed matching is not implemented. No native/API
acceptance follows from that syntax support.

## Historical grammar checkpoints

These paragraphs record earlier migration slices. Their pending/incomplete
statements and counts are historical, superseded by the current status above
and the final N1 review; they are not additional open N1 work.

Ordinary `Self` names and concrete owner-type spelling also do not complete the
remaining member/receiver grammar or any replacement chapter audit.
Native aliases now reuse existing primitive/unit/struct/symbol types, with
direct native fixtures and a canonical-spelling C-emission comparison. This
closes the old symbol-only alias limitation, not the pending document/API
acceptance audits. Empty type bodies now classify as unsupported traits, with
explicit unit fields used for retained struct cases. This does not implement
trait members/conformance or complete any replacement chapter audit.

Qualified symbol syntax is also parser-only: native compilation reports the
module-qualified scope boundary. Four newly checked complete fences bring the
inventory to 150 checks and 89 fenced listings, not module/API, merged symbol,
interpolation or N2/N3 acceptance.

Struct-owned function declarations now replace `impl` blocks, with existing
explicit receivers and native behavior preserved. Typed trait signatures and
default bodies parse but retain the native unsupported-trait barrier. Receiver
inference, `self`/`&self`, wrong-way calls and conformance remain pending. Two
additional whole module fences brought that inventory to 152 checks and 87
fenced listings; neither they nor converted native fixtures complete a
replacement chapter audit or accept module/library APIs.

Primitive-spelled members allow the complete standard-library fence to parse,
including `Json.str` and `Json.int`, without installing those APIs. That
inventory had 153 checks and 86 fenced listings. Native fixtures test real
locally declared members, separate from builtin conversions; they do not
complete a native chapter audit or add receiver inference or N2 behavior.

Conformance headers now retain named trait references and type arguments, but
native compilation reports an explicit scope error before dependent names,
cycles, bodies or missing-main checks. Four whole module/trait fences brought
that inventory to 157 checks and 82 fenced listings. This does not resolve
traits or modules, infer receivers, dispatch defaults or complete a native
chapter audit. No bodies or missing names are invented.

The `self` keyword, expression nodes and explicit receiver metadata added five
whole-fence checks, bringing the inventory to 162 checks and 77 fenced listings.
Native struct receiver binding is now implemented: a syntactic self mention or
written `&self` creates a canonical owner parameter before return inference.
Existing permissions and ownership apply; outside-owner use and unavailable
traits/imports/conformance retain their scope errors. Parser counts stay
unchanged, and no whole-fence parser check becomes native/API acceptance.
Strict call forms now reject wrong-way calls with safe fixes, and affected
native fixtures, examples and embedded sources are converted. Pipelines prefer
actual self methods before visible module functions. This does not complete a
replacement chapter audit or add trait/module resolution. Remaining N1 rules,
source conversions for those rules and replacement audits keep N1 incomplete.

Native data/failure payload lists now compile with positional packing,
projection, casts and ownership. Four new fixtures bring the native inventory
to 156; source coverage includes arity/type errors, permissions, evaluation
order, owned values and panic formatting. Multiple `:ok` positions remain an
explicit boundary of the first-version success ABI. Parser counts do not
change, and native fixtures are not replacement whole-chapter audits. No fence
is promoted and no replacement native chapter audit is complete.

Five examples and fourteen native fixtures now spell unambiguous failure tags
with `:!`. Full-source AST checks guard those kinds without changing native
observations or claiming a document audit. Optional `:none` stays neutral, and
`outcomes.brf`'s mixed `:empty` was split into data `:empty` and failure
`:!blank`. Unambiguous failure strings in five checker/compiler C-test modules
are now converted as well. Intentional neutral data/kind characterizations are
retained. The parser/generated-source spelling review now converts four parser
fixtures and unambiguous lexer/newline/parser C-test examples; CLI-generated
casing, kind-conflict and obsolete-syntax cases stay intentional. Full-source
spelling checks cover 24 sources, not 24 native audits. All four parser fixtures
retain their native rejections. No document fence is promoted, no replacement
native audit is completed, and N1 stays in progress.

## N1 spelling bridges

The early `not`, `dbl` and return-header spelling bridges are deleted. References
to them in retired records below describe past tests, not available helpers.

## Historical first-version evidence

M6 completed the first-version feature scope before this migration. Its
[final review](first-version-review.md) and the following retired records
preserve the previous classifications, adapters, exclusions and validation.
Their embedded script names/commands are historical, not current instructions:

[Errors](doc-errors.md), [pipelines](doc-pipelines.md),
[overview](doc-overview.md), [syntax](doc-syntax.md), [types](doc-types.md),
[functions](doc-functions.md), [symbols](doc-symbols.md),
[memory](doc-memory.md), [base sample](doc-base.md),
[standard library](doc-stdlib.md), [modules](doc-modules.md),
[collections](doc-collections.md), [traits](doc-traits.md).

The old green suites and their generated cases do not establish acceptance of
the current documents. Current audit status is the explicit inventory above.
