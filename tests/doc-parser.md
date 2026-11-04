# Current-document parser inventory (N1 in progress)

`test_doc_parser` reads the 13 language chapters in `docs/lang-*.txt`, not
`docs/v0/`. The rationale file `lang-post-0-notes.txt` is not a language chapter.
The manifest in `test_doc_parser.cmake` classifies all **239 code fences** and
the two unfenced chapters. It runs **95 successful parses and four intentional
syntax rejections**. The other **140 fences** are explicit listings, not passing
parser tests: 56 await N1/N4/N5 syntax, 72 need later features, and 12 are
incomplete signatures/operator/context fragments. The overview and raw base
sample have separate zero-fence listings.

```sh
ctest --test-dir build -L doc-parser --output-on-failure
```

The full index and reasons are written to
`build/tests/current-doc-parser-fixtures/inventory.txt`. Successful parses use
only an enclosing `main` for statements, no wrapper for declarations, or a
`main` around the usage after a complete function. No syntax is rewritten, no
helper library is synthesized and no function body is extracted. Intentional
syntax errors must reject with no AST. Semantic-error examples can parse: this
is **not** proof of typing, casing, outcomes, ownership, modules or library APIs.
In particular, some unimplemented payload/interpolation syntax could otherwise
be misread as an old index or a plain string; those fences are listed instead.

| Chapter | Parser checks | Listings (including unfenced chapters) |
|---|---:|---:|
| collections | 1 | 30 |
| overview | 0 | 1 |
| errors | 7 | 31 |
| raw base sample | 0 | 1 |
| functions | 15 | 12 |
| memory | 16 | 2 |
| modules | 6 | 13 |
| pipelines | 11 | 6 |
| standard library | 0 | 1 |
| symbols | 2 | 13 |
| syntax | 26 | 9 |
| traits | 1 | 9 |
| types | 14 | 14 |

Chapter hashes force classification review after document edits; counts, index
bounds, duplicate/missing classifications, nonempty reasons and the complete
chapter inventory are also guarded. `test_doc_parser_manifest` exercises 13
failure guards plus positive listing/zero-fence accounting without a compiler.
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
