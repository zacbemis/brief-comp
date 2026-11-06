# Current syntax native audit (N1 scope)

`test_doc_syntax_n1.cmake` reads `docs/lang-syntax.txt`, not `docs/v0/`. All
**35 fences** are classified: two execute unchanged and five reject for their
documented errors. **28 remain explicit listings**. Thirteen independent prose
programs bring the audit to **20 cases**: eight native executions and twelve
compiler-rejection programs. No missing API, value or body is supplied to a
quoted fence.

```sh
ctest --test-dir build -L doc-syntax --output-on-failure
```

Normal/sanitized variants use the reviewed shared harness and C runner. The
compiler-free manifest guards run in every configuration. The historical
[syntax audit](doc-syntax.md) remains retired; no adapter is restored.

## Fence inventory

| Fence | Current native evidence or explicit limitation |
|---|---|
| 1 | `comments`: the complete comment fence followed only by an empty entry. |
| 2 | Fragment: interleaved local bindings, constants and a type header. No scope rewriting to force the intended casing errors. |
| 3, 5, 6, 8, 9 | Context: missing User/input APIs; const/collection dependencies remain unavailable. |
| 4 | Fragment: interleaved declarations/usage, missing APIs and ellipsis body. |
| 7 | Listed: N4 declared bindings, collection construction and N5 interpolation. |
| 10 | `annotation-mismatch`: exact binding-annotation error, original line 1 plus the one-line main wrapper. |
| 11–14, 17, 23 | Context: missing scalar operands. Reference operators are not made executable with invented bindings. |
| 15, 16 | Context: missing age/user/method inputs. No isActive/isBanned stand-ins. |
| 18 | `comparison-chain`: one exact syntax diagnostic before lookup. The second, well-formed expression is not claimed accepted natively by that rejection. |
| 19, 20 | Context: missing inputs, sum/double/f/g. No local helper substitutions. |
| 21 | Context: missing age and mixed comparison/match alternatives; neither expression is extracted. |
| 22 | `pipeline-stage`: exact malformed-stage syntax error before lookup, not an unknown-function rejection. |
| 24–26 | Context: missing age or duplicate label alternatives. Independent programs check branch values and missing else. |
| 27 | `while-loop`: complete loop, followed by an observer requiring count 10. |
| 28, 29 | Listed: inclusive/typed loops and collection APIs. No extraction of the already-supported first exclusive loop. |
| 30, 32, 33 | Listed: collection iteration and unavailable user/row APIs. Independent range loops check break/continue semantics. |
| 31 | `outside-loop`: the entire existing main is written unchanged, without a second main, and rejects on original line 2. |
| 34 | `literal-header`: the documented unwrapped-header error rejects before lookup. This does not accept the other Guest/array examples natively; independent real types check valid escapes. |
| 35 | Listed: generics, collections and indexing. |

Body-rejection lines have exactly one wrapper-line offset; complete-source
rejections have none. All five errors require exact primary text, location,
count, status 1, no emitted C and no unexpected fix or sanitizer report.

## Independent prose evidence

- `prose-precedence`: arithmetic/associativity, power before unary minus,
  bitwise/comparison binding, pipelines and logical precedence. The `or`/`and`
  inputs distinguish the grouping instead of producing the same value either
  way.
- `prose-not-precedence`: the exact bool-operand error proves that `not` binds
  to its operand before equality, not to the comparison result.
- `prose-continuations-control`: multiline operators, leading dot/pipeline,
  multiline arguments, value-producing if, while and break/continue.
- `prose-scope-and-ranges`: outer-name access in a shadowing initializer,
  range-bound evaluation once in left-to-right order, descending empty ranges
  and nearest-loop break. A digit trace distinguishes reversed/repeated bounds.
- `prose-update-statements`: postfix binding/field updates and every arithmetic
  compound assignment, observing the final values.
- `prose-header-escapes`: real Gate values in grouped and call-argument keyword
  headers, not definitions supplied to fence 34.
- `prose-ordinary-retired-names`: real Sym/Self declarations and ordinary
  type/impl/trait/ovr functions, with a primitive-spelled member distinct from
  its builtin converter.
- `prose-declaration-casing`: type/function/field/parameter/constant/binding/
  loop/tag roles, with safe preview fixes. Ambiguous lowercase `choice = ...`
  remains a file-level statement error; no uppercase alias intent is guessed.
- `prose-reserved-names`: primitive/self keywords cannot bind; underscore
  prefixes reject without an invented replacement.
- `prose-binding-rejections`: immutable let/loop bindings, duplicate local
  declarations and visibility only after an initializer.
- `prose-statement-only`: updates and assignments cannot be nested expressions.
- `prose-old-spellings`: old logical operators and dub reject with exact fixes.
- `prose-missing-else`: a used if value requires else.

These complete authored programs do not complete any listed fence. Local
casing and the specified keyword retirements are covered without changing
production diagnostics. The modules audit now reviews available module-name
and keyword grammar; filesystem module naming/discovery is not claimed.
N4 bindings/inclusive/typed loops and N2/N3 outcome policy are unchanged.

## Integrity and artifacts

The only shared harness addition writes complete rejection fences verbatim,
so an existing main cannot acquire a duplicate entry or shifted error line.
The compiler-free positive checks this with CRLF bytes and an unchanged line
frame, alongside existing top/body/UTF-8/semicolon checks. Twenty-seven
independent negative guards now include listed-as-complete attempts and exact
fix-preview registration/protocol failures added for the modules audit.
The raw/example
positive and guards verify zero-fence completeness, unchanged bytes, pinned
source hashes and exact example inventory/counts.

Generated cases, provenance, classifications and logs are under
`build/tests/current-doc-syntax/` and `current-doc-syntax_sanitized/`.
Shared guards use `build/tests/doc-native-manifest-fixtures/`. Parser counts,
frozen documents, existing fixtures and compiler/runtime behavior are unchanged.
Syntax, functions, types and modules have bounded native audits, not full API
coverage. The raw-base inventory now exists with independent example tests;
the [final N1 review](next-version-n1-review.md) closes its milestone scope.
