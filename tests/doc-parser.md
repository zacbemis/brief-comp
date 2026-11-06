# Current-document parser inventory (N1 in progress)

`test_doc_parser` reads the 13 language chapters in `docs/lang-*.txt`, not
`docs/v0/`. The rationale file `lang-post-0-notes.txt` is not a language chapter.
The manifest in `test_doc_parser.cmake` classifies all **239 code fences** and
the two unfenced chapters. It runs **147 successful parses and five intentional
syntax rejections**. The other **87 fences** are explicit listings, not passing
parser tests: 17 await N1/N4/N5 syntax, 50 need later features, and 20 are
incomplete signatures/operator/context fragments. The overview and raw base
sample have separate zero-fence listings.

```sh
ctest --test-dir build -L doc-parser --output-on-failure
```

The full index and reasons are written to
`build/tests/current-doc-parser-fixtures/inventory.txt`. Successful parses use
only an enclosing `main` for statements, no wrapper for declarations, or a
`main` around the usage after complete declarations. A `type` context prefixes
`DocType =` and preserves the entire type fragment, so unions cannot pass merely
as bitwise expressions or assignments in a wrong body context. The `import-body`
context wrapper keeps the initial import block at top level and encloses all
remaining usage, preserving the entire fence text; missing boundaries or
non-import prefixes are rejected. No syntax is rewritten, no helper library is
synthesized and no function body is extracted. Intentional syntax errors must
reject with no AST and exactly one error, on a line the document marks `Compiler
error`, so the rest of the fence is shown to parse. Semantic-error examples can
parse: this is **not** proof of typing, complete casing rules, outcomes,
ownership, modules or library APIs. Bracketed payload lists now build symbol
nodes, not old index recovery. Unimplemented interpolation could still be
mistaken for a plain string; those fences remain listed. The `.()` index,
bracketed constructors and typed collection literals check syntax only;
collection operations remain unsupported. In the header-escape fence, only the
unwrapped struct header rejects; its grouped, call-argument and wrapped array
forms parse. The interleaved syntax example with an ellipsis function body is a
fragment, not a future passing test or an extracted stub. The casing fence also
mixes local bindings with a top-level struct: declaration casing is implemented,
but the whole fence remains a context fragment rather than being extracted or
accepted for the wrong error.

| Chapter | Parser checks | Listings (including unfenced chapters) |
|---|---:|---:|
| collections | 3 | 28 |
| overview | 0 | 1 |
| errors | 22 | 16 |
| raw base sample | 0 | 1 |
| functions | 20 | 7 |
| memory | 17 | 1 |
| modules | 15 | 4 |
| pipelines | 13 | 4 |
| standard library | 0 | 1 |
| symbols | 11 | 4 |
| syntax | 30 | 5 |
| traits | 1 | 9 |
| types | 20 | 8 |

Chapter hashes force classification review after document edits; counts, index
bounds, duplicate/missing classifications, nonempty reasons and the complete
chapter inventory are also guarded. `test_doc_parser_manifest` exercises 16
failure guards, import/type context preservation and positive listing/zero-fence
accounting without a compiler.
When syntax is implemented, move its applicable listings into parser checks;
when documents change, review classifications before updating their hashes.

The union switch adds 21 whole-fence checks: four type fragments, complete
aliases/functions/fields and one mixed declaration/usage example. File labels
and inferred-type comments are preserved, not interpreted as module loading or
qualified-tag grammar. Remaining listings now state their actual blockers:
member/receiver grammar, N4/N5, lambdas, qualified symbols or incomplete/mixed
contexts. Standalone signatures, ellipsis bodies, pattern arms and unsupported
mixed contexts are not extracted or stubbed into tests. Native multi-payload
support, typed/refutable matching and N2/N3 semantics remain separate gaps.

Typed payload binding syntax now parses, but the sole current-document example
is still inside symbols fence 10's signature/ellipsis/interpolation fragment. It
is not extracted into a passing test. Dedicated parser fixtures test the syntax;
native rejection fixtures keep type-directed matching explicitly unsupported.

Retiring `Self` as a keyword did not unblock another complete document fence.
Member/receiver syntax is still pending, and the ordinary-name fixtures did not
complete a native chapter audit.

Native type aliases now support the existing primitive/unit/struct types as well
as symbol sets. Their syntax already parsed, so no fence is promoted and counts
remain unchanged. Native fixtures and a canonical-spelling code-generation
comparison test this capability; they do not constitute a replacement chapter
audit.

Empty type declarations now print as traits rather than fieldless structs;
compilation reports the explicit field/unsupported-trait boundary. That syntax
already parsed, so that increment retained 146 checks and 93 listings. Member
and trait-body grammar is still pending; that recognition did not complete a
native chapter audit.

Qualified symbols add four complete checks: errors fence 17 uses a type context;
fences 24, 37 and 38 use a body context. Module paths, kind prefixes and whole
payload/match sources are preserved; module loading and N2/N3 handling are not
asserted. Errors fence 19 and symbols fences 8 and 13 still need interpolation,
so they move to N5 listings rather than passing as plain strings. Errors fences
15 and 23 contain standalone signatures/interleaved multi-file usage and remain
whole-source fragments. Symbols fence 9 still needs merged-symbol declarations.
That increment had 150 checks and 89 fenced listings, plus two unfenced
listings.

Struct member placement adds two whole-fence checks: modules fence 19's
constructor declarations and fence 7's namespace/tag usage. They assert syntax,
not modules, privacy, receiver inference or N2 contracts. Typed trait signatures
and default bodies are retained without synthesizing bodies, but explicit
`self`/`&self` and conformance headers remain pending. The standard-library
listing now identifies its remaining keyword-named qualified JSON members;
`Json.str` and `Json.int` do not parse yet. Counts are 152 checks and 87 fenced
listings, plus two unfenced listings. No native chapter audit is completed.

## Native acceptance gap

N1 retired all 13 v0 native chapter scripts, their normal/sanitized CTest
registrations, support guards and spelling bridges. No active CMake test reads
`docs/v0/`. The old chapter Markdown records are historical; their commands no
longer select registered tests. Existing checked-in native fixtures and C tests
remain active. New native chapter audits return in the milestones specified in
`docs/plans/next-version.txt`; none is claimed complete by this parser inventory.
This intentional gap is not equivalent coverage of the retired generated cases.
