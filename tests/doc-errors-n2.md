# N2 native audit: errors and outcomes

Base: `c02b91b`, committed unchanged. `test_doc_errors_n2.cmake` uses the
unchanged `doc_native.cmake` framework. The full 38-fence chapter is hash/count
guarded and classified exactly once, but native acceptance is scoped to
sections 1–5 and 7–12. Section 6's six fences remain explicit N3 listings,
including its complete `print` fence; they are not accepted by this audit.

One whole fence executes unchanged, 37 remain listings, and 21 independent
complete prose programs exercise available rules. These are not completed or
rewritten File/Json/User examples. No fence is split or supplied missing
names/APIs/bodies, and no rejection for a missing API is called a documented
outcome error. Parser-only checks remain separate.

```sh
ctest --test-dir build -L doc-errors --output-on-failure
```

Normal and sanitizer variants use exact runner stdout/stderr/status, panic
locations, diagnostics/source lines and no-unexpected-fix contracts. Sources,
cases, inventory and logs live in
`build/tests/current-doc-errors{,_sanitized}/`. Shared integrity guards are
unchanged. Fence 5's complete `Health` alias is the unchanged file prefix; entry
observers construct and match all three data variants. Output is `ok`,
`degraded`, `down`, with empty stderr and status zero.

## Every fence, by section

| Section | Fences | Disposition |
|---|---|---|
| 1 | 1, 4, 6, 7 | Fragment: signatures/interleaved usage; missing bodies. |
| 1 | 2, 3 | Context: missing `parse`/`text`. |
| 1 | 5 | Run: complete neutral `Health` alias. |
| 1 | 8 | Later: Vec, lambdas and missing User APIs. |
| 2 | 9 | Fragment: standalone signatures, no implementations. |
| 2 | 10–14 | Context: missing read/parse/Json dependencies. |
| 3 | 15 | Fragment: mixed files, signatures and duplicate aliases. |
| 3 | 16, 17 | Context: missing types, external APIs/qualified errors. |
| 4 | 18, 19 | Context: external modules/APIs; 19 needs interpolation. |
| 4 | 20, 21 | Fragment: alternative signatures with missing types. |
| 5 | 22, 23 | Fragment: signatures/mixed files; no replacement File. |
| 6 | 24–29 | Outside N2: N3, including File-dependent/print fences. |
| 7 | 30 | Context: read/parse/Json plus N5 brace escaping. |
| 8 | 31 | Fragment: missing body and external Json/User dependencies. |
| 8 | 32 | Context: missing User type/constructor; not an error proof. |
| 9 | 33 | Fragment: function/local usage; no relocation/extraction. |
| 9 | 34 | Later: select, collections and lambdas. |
| 10 | 35 | Later: collection iteration, missing users and User type. |
| 10 | 36 | Later: N4 let-else and unavailable find. |
| 11 | 37 | Context: Json/User modules, APIs and qualified patterns. |
| Summary | 38 | Context: external APIs/modules and qualified patterns. |

## Available rules exercised independently

- Section 1: scalar and unit postfix extraction; non-`:ok` success; multiple
  surviving tags with equal payload types stay distinct; `:none` survives both
  postfixes. Error-only propagation/panic has no success edge. Separate panic
  programs pin error text, nonzero status and the original source location.
  Plain, neutral, optional and singleton-`:ok` operands reject postfixes.
- Section 2: all three error routes and success through written, inferred and
  partial results, including pipeline propagation; omitted written errors
  reject. `_` outside function returns remains invalid.
- Sections 3–5: local named error groups and specific error payloads, without
  pretending to implement module qualification or standard-library APIs.
- Section 7: a handled error disappears, while the catch-all propagates only
  the remaining error. All success/handled/unhandled branches execute. The
  authored program uses ordinary text, not an implementation of N5 escapes.
- Section 8: a narrower result widens to a larger local union; the reverse
  propagation fails its written contract. No tag-ID conversion is invented.
- Section 9: plain arithmetic and singleton-`:ok` aliases in parameters,
  fields and returns; plain results cannot match `:ok`. Optional union returns
  still wrap plain successes only at a result boundary.
- Section 10: written, inferred and partial valueless endings select absence;
  payload-less success and ordinary unit endings retain their behavior.
  Payload-less `:ok`/`:none` ambiguity rejects; `:ok[()] | :none` stays valid.
- Section 11: named-union pattern coverage runs success and both error members;
  omitting a separate error arm rejects as nonexhaustive.
- Section 12: error entry results print escaped owned struct/string/character
  and nested-symbol payloads with `name = value`, then exit 1. A neutral entry
  value containing an owned nested error is released and exits 0. Prior stdout
  and exact split stderr are checked; existing CLI tests check merged flush
  order, bounded formatting and owner cleanup more extensively.

No returned-call acceptance, delayed handling or linear-binding behavior is
claimed. Every fallible call in an accepted prose program is handled immediately
with a postfix or match. A stored error literal is not used as evidence of N3.

## Expectations and validation

**Changed existing expectations: none.** Existing Brief fixtures, runtime
markers, goldens, `.expect` files, language chapters and N1 audits are
untouched. New postfix markers initially guessed old message wording; the new
stderr marker needed doubled backslashes for the runner's escape decoder.
Only these new expectations were corrected; no compiler behavior changed.

The 22 cases comprise 16 native executions (two expected panics) and six
rejection programs with nine exact diagnostic sites, all without guessed fixes.
See [the closure review](next-version-n2-review.md) for the full matrix,
retained-source comparison, additional strict-C checks and scoped limitations.
