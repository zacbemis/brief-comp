# N3 scoped native audit: pipelines and pattern matching

Base: `27d7eb1`, committed unchanged. `test_doc_pipelines_n3.cmake` uses the
unchanged `doc_native.cmake` discipline. All **17 fences** are hash/count
guarded and classified once: one whole body executes, sixteen remain listings.
Twelve independent complete prose programs exercise available rules, not
repaired chapter fragments. Parser acceptance is not counted as native proof.

```sh
ctest --test-dir build -L doc-pipelines --output-on-failure
```

Normal/sanitized variants use exact output/status, panic location and diagnostic
message/source-line checks, with no unexpected fixes. Sources, cases,
inventories and logs are in `build/tests/current-doc-pipelines{,_sanitized}/`.

## Every fence

| Fences | Disposition |
|---|---|
| 1 | Fragment: the standalone operator is not a program. |
| 2, 3 | Context: missing text, trim and lower. |
| 4–6 | Later: collection/lambda APIs and unavailable users/module context. |
| 7, 8 | Context: missing json, parse and validate. |
| 9, 10 | Context: File/Json/User APIs and inputs are unavailable. |
| 11 | Fragment: mixed precedence examples, unbound inputs and a range value. |
| 12 | Context: age is absent; no invented input binding. |
| 13 | Run: complete 10-to-double match body, unchanged within main. |
| 14 | Later: age/digit absent and inclusive ranges belong to N4. |
| 15 | Later: collection find/predicate and users are absent. |
| 16 | Fragment: isolated arms, not a complete match. |
| 17 | Context: age absent; no splitting positive/negative alternatives. |

Fence 13 discards its scalar result and reaches an appended observing print.
A separate prose program observes the same transformation's value, 20. The raw
fence is never rewritten to capture its result. No fence gets missing types,
functions, inputs, renamed alternatives or relocated scope.

## Independent available-rule coverage

- The piped expression runs once before explicit arguments, followed by each
  call stage in order. The value occupies the first argument slot. Independent
  functions need not belong to the piped type.
- Actual self methods win over module-scope functions. Local qualified
  associated calls and writable self methods use ordinary lookup/permissions;
  this is not evidence of external module or trait lookup.
- Written, inferred and partial returned pipelines accumulate earlier `?`
  errors and return the final stage unchanged. Success, first-stage failure
  and final-stage failure execute; a stored pipeline outcome is matched later.
- Stage postfixes apply before the next call; immediate match stages handle
  complete owned results. Expected stage panic text/status/location is exact.
  An intermediate outcome is not automatically unwrapped or silently passed;
  written return contracts still reject missing errors.
- Arithmetic/pipeline/comparison precedence, parenthesized comparison matches,
  and `and`/`or` short-circuit effect counts run natively. Range-valued call
  arguments are not implemented and that line of fence 11 stays a listing.
- Value-binding and positional-symbol patterns, range/comparison coverage,
  complete decoded heap strings, embedded zero bytes, prefixes and UTF-8.
  Distinct tags with equal payload types still need distinct coverage.
- Arm blocks return their last expression; assignment arms yield unit. Break
  and continue arms target the loop and have no match fallthrough.
- Missing numeric/string/bool/symbol coverage, covered and empty-range arms,
  decoded-equal strings and signed-zero duplicate arms reject. Floating
  matches exercise negative/zero/positive/NaN; lack of a NaN catch-all rejects.
  Character ranges observe the exclusive endpoint.

There are **13 runner cases**: ten executions (one expected panic) and three
rejection programs with eleven exact sites. Inclusive matching/loop additions,
merged/typed/refutable payload patterns, collections, lambdas and real module
lookup remain outside this scoped acceptance.

## Expectations and validation

**Changed existing expectations: none.** No language chapter, old test input,
fixture marker, golden or parser classification changes. Initial new markers
guessed the unreachable-arm wording and put a return-contract error on the
expression rather than the function opening. A new decoded-string test used
an unavailable hex escape; two valid apostrophe spellings now test decoded
identity instead. No compiler behavior or old assertion was changed.

See [the closure review](../docs/history/reviews/next-version-n3-review.md) for the full matrix,
retained-source comparison and strict GCC/Clang normal/sanitized native checks.
