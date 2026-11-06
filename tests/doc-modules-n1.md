# Current modules native audit (N1 scope)

`test_doc_modules_n1.cmake` reads `docs/lang-modules.txt`, not `docs/v0/`.
All **19 fences** are classified: three execute unchanged, two reject for the
implemented declaration rules, and **14 remain listings**. Fifteen independently
authored prose programs bring the audit to **20 cases**: seven native
executions and thirteen compiler-rejection programs. This is one-file native
coverage, not implementation of modules across files.

```sh
ctest --test-dir build -L doc-modules --output-on-failure
```

Normal/sanitized variants use the reviewed shared harness and C runner. Shared
compiler-free manifest guards run in every configuration. The historical
[module audit](doc-modules.md) stays retired; no API stand-in or adapter is
restored.

## Fence inventory

| Fence | Current native evidence or explicit limitation |
|---|---|
| 1 | `struct-fields`: original User, with an observing local literal requiring its name, age and flt field. This is not cross-file initialization/privacy acceptance. |
| 2 | `type-aliases`: unchanged Id/DateError definitions, alias conversion and a typed error-tag value observed exhaustively. |
| 3 | Context: missing User and duplicate literal alternatives; no supplied type or splitting. |
| 4 | Listed: N5 interpolation, missing DateError/daysIn and mixed declaration/usage. No stripping or local stand-ins. |
| 5 | Context: absent Date/date; no guessed receiver. Independent wrong-form programs do not complete these calls. |
| 6 | Listed: module exports/name resolution, const and trait dependencies. The User declaration is not extracted. |
| 7 | Context: missing User/version/HasUser and qualified tag resolution. |
| 8 | `public-type`: the full fence rejects only pub on Report, on original line 22, with its exact comment-preserving fix preview. Cross-file privacy and duplicate alternative definitions are not accepted by that rejection. |
| 9 | Listed: trait conformance and cross-file APIs. A scope error would not prove conformance. |
| 10, 11 | Context: ambiguous/misspelled module lookup needs the unavailable loader. Unknown names are not the documented module diagnostics. |
| 12, 13 | Listed: import selection, filesystem and standard-library module resolution. |
| 14 | Fragment: alternative conformance headers and ellipsis bodies. No completion or extraction. |
| 15 | Context: file-label comments do not make separate modules; no simulated dependency cycle. |
| 16 | `file-statement`: exact original source, including its existing main, rejects the top-level print on line 3. |
| 17 | Listed: missing File API and N2 qualified-error entry/formatting semantics. No mocked I/O or changed exit policy. |
| 18 | Context: missing User/getName API. No constructor supplied to the complete main. |
| 19 | `fallible-constructor`: complete original User constructor, observing success at ages 20/18 and invalidAge at 17. |

No fence receives a missing type, field, function body or module API. Entry
observers instantiate/call only definitions already in that same whole fence.

## Independent prose programs

- `prose-field-order`: field initializers run in written rather than declaration
  order, proved by a 21 digit trace and both stored values.
- `prose-members`: real Gauge construction, read-only/mutable self, methods and
  associated calls inside the struct, and pub metadata accepted in one file.
- `prose-readonly-self`: an inferred read-only receiver cannot mutate its field.
- `prose-call-forms`: exact wrong-way diagnostics/fixes; an ordinary owner-typed
  parameter is not self, unreachable self still makes a method, and a missing
  receiver gets no invented fix.
- `prose-literal-errors`: missing, duplicate and unknown field initializers.
- `prose-public-types`: pub is rejected on struct/alias/trait declarations with
  exact removal previews, not a claim of native trait implementation.
- `prose-module-name-syntax`: import/qualified-module/tag casing and safe fixes
  at syntactically known positions, without loading any file.
- `prose-import-boundary`, `prose-trait-boundary`,
  `prose-conformance-boundary` and `prose-qualified-tag-boundary`: one exact
  unsupported-scope diagnostic before dependent missing names/bodies. These
  guard honest limitations, not native acceptance of those features.
- `prose-plain-entry`: a plain numeric result is discarded and exits zero.
- `prose-constructor-convention`: new is not magical; a self-using new is a
  method, while a differently named function can construct the value.
- `prose-no-module-wrapper`: reserved mod does not introduce an accepted module
  wrapper, nor get a guessed relocation fix.
- `prose-entry-parameters`: main must have no parameters.

The N1-specified keyword retirements were checked by the syntax audit; mod is
not one of those retirements. Available module-name grammar now has explicit
casing coverage. Filenames are not source declarations: this audit does not
rename the existing lower-case/hyphenated fixtures, enforce a new filename rule
or claim case-sensitive module discovery. That requires the out-of-scope
filesystem loader. Lowercase dotted receivers are not guessed to be modules.
N2 error-kind exits/formatting and N3 handling policy stay unchanged.

## Integrity and artifacts

All five quoted fences retain their original bytes, comments and line frames.
A public-type diagnostic may now register one exact same-line help preview in
the shared harness. The full printed source line is compared, not just a code
substring. This checks whitespace/comment preservation without applying the fix
or modifying quoted code. A missing, wrong, extra or unexpected help fails;
other diagnostic records retain their no-fix default.

The compiler-free positive covers a semicolon/Unicode fix preview alongside
existing top/body/complete-source byte and frame checks. Twenty-seven
independent negative guards include absent diagnostic indices, duplicate
registrations and missing/wrong/extra/unexpected fixes. Raw/example checks
also protect zero-fence
completeness, source hashes and exact example inventory/counts. Existing quoted
source and expectations stay unchanged.

Generated cases, provenance, classifications, diagnostic logs and runner logs
are under `build/tests/current-doc-modules/` and
`current-doc-modules_sanitized/`. Shared guards use
`build/tests/doc-native-manifest-fixtures/`. Parser inventory counts remain
unchanged. The raw-base inventory now exists with independent example tests;
the [final N1 review](next-version-n1-review.md) closes its milestone scope.
This is not full module/API acceptance.
