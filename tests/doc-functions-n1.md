# Current functions native audit (N1 in progress)

`test_doc_functions_n1.cmake` reads `docs/lang-functions.txt`, not `docs/v0/`.
It classifies all **27 fences**: four run unchanged with entry observers, two
reject for their documented return contracts, and **21 remain listings**.
Eight separately authored prose programs bring the audit to **14 cases**:
eight native executions and six compiler rejections. Prose programs do not
complete a fence or count as acceptance of an unavailable API.

```sh
ctest --test-dir build -L doc-functions --output-on-failure
```

Normal and sanitized variants use the existing binary-safe C program runner.
The sanitized variant is registered under the same compiler/sanitizer support
condition as native program tests. A compiler-free manifest test runs in every
configuration. Existing v0 records in [doc-functions.md](doc-functions.md)
remain historical; none of their adapters, extracted bodies or stand-ins is
restored.

## Fence inventory

| Fence | Current native evidence or explicit limitation |
|---|---|
| 1 | `written-return`: complete `add`, typed `int` observers and positive/zero/negative arguments. |
| 2 | `explicit-return`: complete `validate`, early/tail returns below, at and above the boundary. |
| 3–7 | Listed: function values, `Fn`, lambdas and captures. Rejecting unsupported syntax does not prove the documented lambda-inference or capture errors. |
| 8 | Listed: collection iteration and unavailable user APIs. |
| 9 | Fragment: three alternative incomplete headers; no body is supplied. |
| 10 | `inferred-number`: complete `double`, typed `int` observers and negative/zero/positive inputs. |
| 11 | `inferred-data`: complete `describe`, both neutral tags, exhaustive observers and an ignored ordinary-data call. |
| 12–14 | Context: unavailable File/Json/User APIs and qualified sets. The intended success-contract error in 14 is not tested by an unknown-name rejection. |
| 15 | `wrong-written-return`: exact body-contract diagnostic on original fence line 1 (the function body opening), not at a caller. |
| 16 | `undeclared-return`: exact written-return diagnostic on original fence line 3. |
| 17 | Listed: trait requirements/defaults and unavailable `User`. |
| 18–21 | Context: missing User/UserCollection fields and methods; independent unit/return prose cases are not native acceptance of these fences. |
| 22 | Listed: alternative definitions, missing File API and N2/N3 outcome/handling policy. No splitting or stubbing. |
| 23 | Context: missing `users.len`; unused ordinary values are checked separately. |
| 24 | Listed: missing `file.close` and N2/N3 handling; unknown-name rejection is not the intended outcome error. |
| 25 | Context: missing user/getName/save definitions. |
| 26 | Context: Report/User/module dependencies; `summary` is not rebound to a local mock. |
| 27 | Context: mixed method/module alternatives; no individual call is extracted. |

## Prose regressions, not fence substitutions

- `prose-inference`: public inferred returns, literal context across early/tail
  returns, annotated recursion, an owned tail discarded by written unit, and
  unit inferred from a printing call.
- `prose-partial-return`: public partial return inference, a written success
  slot and an inferred owned error payload, with exhaustive observation.
- `prose-receivers`: a real local Counter, associated construction, mutable and
  read-only self methods, assignment-inferred unit, direct/module-function
  calls and unqualified pipelines.
- `prose-immutable-parameter`: neither ordinary nor `&` parameter bindings may
  be replaced.
- `prose-readonly-parameter`: ordinary struct parameters do not permit field
  mutation.
- `prose-unanchored-recursion`: one exact recursive-inference diagnostic.
- `prose-evaluation-order`: left-to-right arguments and operands, short-circuit
  `and`/`or`, target-before-RHS and compound assignment's old-value snapshot.
- `prose-wrong-way-calls`: exact method/associated-call errors and safe fixes.

These are complete authored programs using their own real types and functions,
not APIs supplied to a quoted fence. They preserve first-version outcome
behavior. Kind-based outcomes, entry status/formatting, optional endings and
N3's handling policy are not claimed by this N1 audit.

## Integrity and artifacts

`doc_native.cmake` reuses the current manifest's hash-checked whole-fence reader
and classification-completeness check. Each quoted fence is the exact prefix
of its generated source, including trivia; only a `main` observer and test
expectations are appended. Rejected fences receive only an empty entry point:
no inline error markers alter the quoted lines. Diagnostics require status 1,
no emitted C, one exact primary message, its original line and no unexpected
fix. The C runner checks output, stderr, status, error/fix counts and sanitizer
failures for the remaining cases.

The shared reader preserves mixed LF/CRLF bytes instead of CMake's default
newline normalization. The manifest positive test checks semicolons, braces,
comments, UTF-8 and CRLF byte-for-byte, plus prose/quoted-source separation.
The shared guard now also covers body wrappers and multiple diagnostics for
[types](doc-types-n1.md), without changing function-fence generation. Thirteen
negative tests guard stale hashes/counts, duplicate/missing classifications,
unexercised fences, duplicate cases, listed-as-native attempts, invalid indices,
changed case counts and malformed body diagnostic frames. The syntax audit's
complete-source writer is checked for byte preservation and no duplicate main.

Generated sources, classifications, case provenance and logs are under
`build/tests/current-doc-functions/`, with a separate
`current-doc-functions_sanitized/` directory. Manifest guard reports are under
`build/tests/doc-native-manifest-fixtures/`. Parser inventory counts remain
unchanged. This is a bounded first replacement native audit, not complete
function/API acceptance or completion of N1's other chapter audits.
