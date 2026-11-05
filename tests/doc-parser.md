# Current-document parser inventory (N1 in progress)

`test_doc_parser` reads the 13 language chapters in `docs/lang-*.txt`, not
`docs/v0/`. The rationale file `lang-post-0-notes.txt` is not a language chapter.
The manifest in `test_doc_parser.cmake` classifies all **239 code fences** and
the two unfenced chapters. It runs **103 successful parses and four intentional
syntax rejections**. The other **132 fences** are explicit listings, not passing
parser tests: 54 await N1/N4/N5 syntax, 64 need later features, and 14 are
incomplete signatures/operator/context fragments. The overview and raw base
sample have separate zero-fence listings.

```sh
ctest --test-dir build -L doc-parser --output-on-failure
```

The full index and reasons are written to
`build/tests/current-doc-parser-fixtures/inventory.txt`. Successful parses use
only an enclosing `main` for statements, no wrapper for declarations, or a
`main` around the usage after a complete function. The `import-body` context
wrapper keeps the initial import block at top level and encloses all remaining
usage, preserving the entire fence text; missing boundaries or non-import
prefixes are rejected. No syntax is rewritten, no helper library is synthesized
and no function body is extracted. Intentional syntax errors must reject with no
AST. Semantic-error examples can parse: this is **not** proof of typing,
complete casing rules, outcomes, ownership, modules or library APIs. In
particular, some unimplemented payload/interpolation syntax could otherwise be
mistaken for an old index or a plain string; those fences remain listed.
The `.()` index and bracketed-constructor fences check syntax only; collection
operations remain unsupported. The interleaved syntax example with an ellipsis
function body is a fragment, not a future passing test or an extracted stub.
The casing fence also mixes local bindings with a top-level struct: declaration
casing is implemented, but the whole fence remains a context fragment rather
than being extracted or accepted for the wrong error.

| Chapter | Parser checks | Listings (including unfenced chapters) |
|---|---:|---:|
| collections | 2 | 29 |
| overview | 0 | 1 |
| errors | 7 | 31 |
| raw base sample | 0 | 1 |
| functions | 15 | 12 |
| memory | 16 | 2 |
| modules | 8 | 11 |
| pipelines | 11 | 6 |
| standard library | 0 | 1 |
| symbols | 2 | 13 |
| syntax | 29 | 6 |
| traits | 1 | 9 |
| types | 16 | 12 |

Chapter hashes force classification review after document edits; counts, index
bounds, duplicate/missing classifications, nonempty reasons and the complete
chapter inventory are also guarded. `test_doc_parser_manifest` exercises 15
failure guards, the import context wrapper and positive listing/zero-fence
accounting without a compiler.
When syntax is implemented, move its applicable listings into parser checks;
when documents change, review classifications before updating their hashes.

## Native acceptance gap

N1 retired all 13 v0 native chapter scripts, their normal/sanitized CTest
registrations, support guards and spelling bridges. No active CMake test reads
`docs/v0/`. The old chapter Markdown records are historical; their commands no
longer select registered tests. Existing checked-in native fixtures and C tests
remain active. New native chapter audits return in the milestones specified in
`docs/plans/next-version.txt`; none is claimed complete by this parser inventory.
This intentional gap is not equivalent coverage of the retired generated cases.
