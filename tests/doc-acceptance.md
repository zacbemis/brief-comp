# Document acceptance status

**N1 is in progress.** The first-version native document audits and all spelling
bridges have been retired as required by `docs/plans/next-version.txt`. Their
executable scripts and CTest registrations are deleted; they are not maintained
as a second syntax or extended into a migration system. `docs/v0/` is untouched.

The replacement [current-document parser inventory](doc-parser.md) reads all
13 current chapters: 239 fences, 153 parser checks, 86 explicitly listed fences
and two zero-fence listings. Parser checks are not native acceptance. The
retirement leaves an intentional native document-coverage gap; the checked-in
program fixtures and independent C/compiler/native-driver tests remain active.

```sh
ctest --test-dir build -L doc-parser --output-on-failure
```

## Replacement native audits

No replacement native chapter audit is complete yet. They return with the rules
they can actually test, rather than through rewritten v0 scaffolds:

- **N1:** syntax, types, functions, modules and the raw base sample.
- **N2:** symbols and errors sections 1–5 and 7–12.
- **N3:** error handling section 6, pipelines and memory.
- **N4/N5:** add bindings/loops and interpolation fences to their chapter audits.
- **N6:** overview, traits, collections and standard library scope inventories.

N1's `:!name` syntax/kind checks are not the N2 native audit. Entry status,
propagation, outcome classification, `:ok` equivalence and runtime formatting
still use the first-version rules. The explicit N1 error-only entry fixture
exits 0 until N2 changes and reviews that expectation. Bracketed payload lists
now parse; zero/one payload preserves native behavior, while multiple payloads
still block compilation. Union types and ordinary `Sym` names also preserve
first-version outcome rules. Type-context and whole-fence parser checks do not
complete a native audit. Typed symbol payload binders now retain annotations
for parser checks, but compilation still reports that typed matching is not
implemented. No native/API acceptance follows from that syntax support.
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

Primitive-spelled members now allow the complete standard-library fence to
parse, including `Json.str` and `Json.int`, without installing those APIs. The
current inventory has 153 checks and 86 fenced listings. Native fixtures test
real locally declared members, separate from builtin conversions; they do not
complete a native chapter audit or add receiver inference or N2 behavior.

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
