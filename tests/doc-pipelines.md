# M6 acceptance slice: `docs/lang-pipelines.txt`

`test_doc_pipelines.cmake` reads all 17 code fences directly from the document.
Ten fences are represented in **20 native/diagnostic cases**: nine contain only
first-version examples, and fence 11 is explicitly partial. Six fences require
later-version features; fence 1 is the operator's spelling, not a program.
Four of the cases also check inline stage-callee syntax examples.

Run this slice in normal and sanitized configurations:

```sh
ctest --test-dir build -L doc-pipelines --output-on-failure
```

The tests use the existing C program runner: output, diagnostic text/source line,
exit status, and sanitizer failures are checked. They share extraction, adapter,
classification, case-count and runner support with the M5 error-example audit in
`doc_examples.cmake`. `test_doc_examples_support` independently checks eleven
failure guards so a stale or incomplete audit does not silently pass.

## Fence-by-fence record

Case names refer to generated `.brf` files under
`build/tests/test_doc_pipelines-fixtures/programs/` (and the corresponding
sanitized directory).

| Fence | Example | Check or classification |
|---|---|---|
| 1 | `\|>` | Syntax reference only; assert that the fence contains exactly this token. |
| 2 | Chained text calls | `text-2`: local functions and an effectful heap-string input show input evaluation once, trim-before-lower order, and the resulting `hello`. |
| 3 | Equivalent ordinary calls | `text-3`: the exact ordinary-call expression produces the same result and trace as fence 2. |
| 4 | Filter/count with another argument | Deferred: collection API. Ordinary pipeline argument insertion/order retains native coverage in `pipelines.brf`. |
| 5 | Multi-line collection chain | Deferred: collection sorting/taking. Continuation grammar remains covered by parser examples and scalar pipeline tests. |
| 6 | Lookup over collections/modules | Deferred: `Vec` method, lambda and module-qualified call. First-version method-first lookup is covered in `test_matching` and `pipelines.brf`. |
| 7 | Pipeline propagation | `propagation-7`: success and both error paths; an exhaustive observer requires exactly the expected accumulated tags/payloads. A derived `unhandled-stage` case removes the final `?` and checks its exact diagnostic. |
| 8 | Equivalent ordinary propagation | `propagation-8`: the same success/errors and call trace as fence 7, including skipping validation after parse failure. |
| 9 | Inferred module error union | Deferred: `File`/`Json` module APIs and qualified error sets. Local accumulation is covered in fences 7–8 and the error-document audit. |
| 10 | Ending module pipeline with a match | Deferred: module APIs and JSON getters. Local stage-to-match ownership is covered by existing symbol/outcome fixtures. |
| 11 | Precedence | **Partial:** `precedence` preserves the arithmetic, comparison and boolean expressions. Observed calls verify argument grouping and both short-circuit paths. Only the standalone `0..10 \|> sum()` line is removed, with an exact adapter anchor: first-version ranges are loop bounds, not first-class values for collection APIs. The complete fence still has parser coverage. |
| 12 | Parenthesized comparison input | `comparison-input`: ages below, at and above 18 select the expected string. |
| 13 | Whole-value name binding | `whole-value-binding`: the exact transformation returns 20. |
| 14 | Numeric range/comparison match | `age-ranges`: test both int extrema and boundaries 17, 18, 99 and 100. |
| 15 | Collection lookup outcome | Deferred: collection lookup and predicate function values. Scalar symbol matching has independent native coverage. |
| 16 | Payload arm fragment | `payload-arm`: supply a one-arm success match and local `User.getName`; an owned heap name survives payload extraction and match cleanup. |
| 17 | Four coverage alternatives | Split the four alternatives at guarded blank-line boundaries. `exhaustive` and `wildcard` check useful boundaries; `missing-value`/`missing-discard` reject the uncovered interval, and `unreachable-value`/`unreachable-discard` reject the overlapping literal. Discarding a match does not weaken diagnostics. |

## Inline rules and scaffolding

`stage-invalid-0` and `stage-invalid-1` extract the documented `x |> f().g()` and
`x |> functions[0]()` spellings and require the exact stage-callee syntax error,
not an unknown-name error. `stage-valid-0` and `stage-valid-1` check both documented
alternatives, `x |> f() |> g()` and `(x |> f()).g()`, with a local struct, ordinary
function, and receiver method.

All code fragments are supplied with local variables, function bodies and entry
points. Text helpers handle only the exercised inputs; they are not a new trim or
lower standard library. Likewise, parse/validate are deterministic scalar
stand-ins, not JSON support. The coverage alternatives are separate examples,
not four statements assumed to compile as one program. Their arm text and
explanatory comments remain intact.

Existing `test_matching`, `scalar-matches.brf`, `integer-matches.brf`,
`string-matches.brf`, `string-match-ownership.brf` and their negative fixtures
cover the chapter's additional prose rules: immutable arm bindings, first-useful
selection, byte-exact strings, required catch-alls, NaN/signed zero, nonlocal jumps
and ownership. This slice is not a claim that every inline/prose example in every
document has been audited, nor that M6 is complete.
