# N2 slice: exact results and singleton-`:ok` normalization

Base: `610b54c`. Stop uncommitted for review. N2 is still in progress: optional
endings, the payload-less `:ok`/`:none` ambiguity and the scoped native audits
remain. N3 returned-call acceptance and delayed/linear handling are unchanged.

## Implementation

Canonical symbol construction normalizes a deduplicated singleton `:ok[T]` to
`T`, and `:ok` to unit, without allocating a symbol set. This applies to source
annotations, aliases, expressions, constructors and selected subsets. Multiple
`:ok` payloads keep the existing unsupported boundary; no tuple is invented.

Resolution gathers an entire written union before normalization, so a direct
`:ok[T] | Error` does not normalize its first entry prematurely. Existing
conflict diagnostics keep their original entry spans. Partial annotations retain
their written success schema separately from the normalized type, preserving
both contextual literals and the zero-payload versus unit-payload distinction.

User functions' `return_type` and `outcome_type` now hold the same canonical
result. Calls do not acquire envelopes for matches, bindings or arguments.
`SemaExpr.outcome`, adaptation mode 2 and `sema_result` are removed. An explicit
`:ok[value]` still constructs its contextual union; a plain return is packed
only when its result union contains `:ok`. Inferred mixed returns and SCC growth
combine a plain success with its other tags without interning a singleton
wrapper first. Partial unit success annotations are not mistaken for explicit
unit-discard functions. Unit returns may discard ordinary values, not outcomes.

Lowering forwards a normalized `:ok` payload directly, widening nested unions
when needed. Contextual packing likewise widens its payload before constructing
the outer union. Match arms retain their actual selected tag/schema list apart
from its normalized language type. Thus an explicit match whose catch-all has
only `:ok[T]` left can test the original tag and bind a plain `T`, including
unit, owned strings, structs and nested unions. This is guarded match
projection, not an implicit extraction from an ordinary call.

Only the semantic header, resolver/checker/symbol code and lowering change
production code. Code generation, runtime, permissions and the shared flow
framework are untouched. The legacy `sema_failures` characterization helper
remains; it is not used to classify calls or postfixes.

## Every changed existing expectation

1. Four formerly passing `test_errors.c` sources now reject their obsolete
   `:ok` patterns: the positive double/only compound control, the local
   `x: :ok[int]` control, the `:ok[()]` function control and the
   `:ok[Data]` function control. Scalar/unit inputs report
   `Symbol patterns require a symbol input.`; the Data input reports
   `Symbol pattern tag is not in the input set.`. Positive controls now match
   the plain value or the Data tags directly. Their old definitions, payload
   construction, arithmetic and ordinary call/pipeline coverage remain.
2. The already-invalid compound control gains its double-call pattern error:
   three errors instead of two. Its two no-error postfix errors stay unchanged.
   The separate empty-function call matched with `:ok[x]` still rejects once,
   but reports the non-symbol-input diagnostic instead of the old wrong-payload
   diagnostic. These are the only retained rejection diagnostic differences.
3. The double function's semantic outcome metadata is now its plain int result,
   not a singleton symbol set. The ten partial-contract mismatch cases retain
   their original source, error counts and contract checks, but compare their
   stored result with the normalized written type and require the separately
   retained written `:ok` schema. They no longer expect a symbol type containing
   the success slot. No contract failure is weakened or silenced.
4. The 30 infallible source/target domains in `test_conversions.c`, and all
   eight chr-to-number domains, now compare the converter result directly with
   its target type, not a one-variant envelope. The independent infallibility
   table, fallible two-tag shapes, intrinsic error tags, plain ABI, repeated
   semantic checks, RC insertion and C-emission assertions are unchanged.
5. `test_symbols.c` checks singleton normalization directly instead of asking
   `sema_result` to extract it. The removed helper's multi-payload-error
   assertion becomes a check that all three payload positions remain in the
   unsupported canonical schema. Existing source and IR barriers still reject
   multiple `:ok` positions before C publication.
6. The eight kind-mask rows containing only payload-less `:ok` now have unit
   type, no sole-success variant and no symbolic kind partition. Other mask
   rows retain their classification, schemas, canonical identity and subset
   checks; selecting a sole `:ok` partition now returns its normalized type.
   `sema_success(Error)` is Error itself, so its error-kind assertion changes
   from false to true. Normalization must not hide an error in a fake wrapper.
7. `programs/outcomes.brf` replaces exactly three wrapper patterns: done's
   `:ok[_]` becomes `_`, double's `:ok[value]` becomes the ordinary binding
   `value`, and data's outer `:ok[inner]` match becomes a direct match on
   `:found`/`:empty`. Every original stdout marker, stderr/status expectation
   and all fallible read/append/recursive handling remain unchanged.

No other existing expectation changes. All six examples and their pinned
hashes/goldens stay unchanged. Parser fixtures, current language documents,
quoted fences, native audit classifications and v0 readers are untouched.
No document fragment is extracted or supplied with stand-in types/APIs.

## New tests

`test_exact_returns.c` adds six focused groups and repeated semantic checks:

- Canonical singleton construction over unit, primitive, owned, struct, neutral
  union and error types; duplicate inputs, immutable caller arrays, stable
  interner/tag counts and an unchanged conflicting-schema rejection.
- Alias chains, fields, parameters, local annotations, explicit/nested `:ok`
  values and direct calls all share canonical identity. A plain-only program
  emits no symbol construction, projection or tag-test IR and interns no sets.
- Ordinary primitive/unit/struct/Data calls, receiver calls, pipeline stages and
  infallible converters cannot be matched as `:ok`.
- A binding/argument annotation does not implicitly wrap a plain call; explicit
  construction remains valid. Written contracts, partial inference, wide
  literals and normalized catch-alls keep their exact types and ownership.
- Normalized `:ok[Box]` parameters and narrowed catch-alls preserve read-only
  access, with writable `&` counterparts.
- A normalized unit return cannot discard an error, including through a branch;
  a partial payload-less success can still infer errors, and rejects an integer
  success. This is return-contract enforcement, not N3 binding flow.

Negative controls check LF/CRLF, one exact primary and start location, no
fixes/notes, source immutability, repeat checks and refusal to lower. Positive
controls emit C with ordinary RC insertion. Initial failures were the
inventoried old expectations and two genuine lowering omissions: forwarding a
normalized nested-union payload and packing a narrower union payload without
widening it. Both are covered by the new unit/native cases; no expectation was
relaxed for them.

`programs/exact-returns-n2.brf` observes aliases, nested normalization, owned
strings/structs, mutation, both Data alternatives, contextual direct-call and
branch returns, normalized string/struct/unit/union catch-alls, error-valued
normalization, partial wide recursion and explicit unit-discard cleanup. The
separate `programs/errors/exact-returns-n2.brf` pins fifteen exact rejections
with no fixes. Inventory is now 182 native fixtures and 38 CTest registrations.

## Validation and baseline differences

Commands run:

```sh
cmake --build build -j 6
ctest --test-dir build --output-on-failure --parallel 4
# Fresh Clang Debug ASan/UBSan with -Werror, then its complete ctest.
build/format-env/bin/clang-format --dry-run --Werror <changed C and headers>
git diff --check
```

Default GCC Debug ASan/UBSan and fresh Clang Debug ASan/UBSan pass 38/38 each.
The milestone-closing four-configuration matrix is intentionally not run.

A fresh Release baseline is built from archived HEAD. Source-create capture
retains all 1,847 records from the seven existing semantic/compiler C suites,
yielding 1,701 unique raw inputs, alongside 463 complete CLI sources.
Comparisons use saved original bytes, including the pre-correction outcomes
fixture.

All 2,164 retained AST observations are identical. Exactly five formerly
accepted inputs now reject: the four C-test programs in item 1 and the old
outcomes fixture. Exactly two retained rejection observations change as in
item 2. No previous rejection becomes accepted. The other 801 differences are
successful emitted C: canonical set IDs/layouts, entry metadata and redundant
wrapper packing/extraction change with normalization. There are no new runtime
formatting or exit-policy changes hidden in that C comparison.

Of 156 retained sanitized executable comparisons, only the old outcomes source
now fails compilation for its three obsolete patterns. Its corrected whole
source retains baseline status and both byte streams exactly; the other 155
observations, including all six examples, are unchanged. No golden is generated.
Four complete sources (new positive fixture, outcomes, main-symbol-success and
symbol-ownership) also pass strict GCC/Clang normal/sanitized C compilation and
exact execution, 16/16. CLI emit/build/run reject all fifteen negative sites
before a nonexistent C compiler and publish no C/executable or guessed fix.

The complete ignored source captures, builds, deltas, C outputs, scripts and
logs remain under `build/validation/n2-exact-returns.20261107T010527Z/`.
Formatter, diff and prose checks pass; the plan's design sections and top
status are unchanged. N2 remains in progress; leave this slice uncommitted.
The unrelated `skills/` directory is untouched.
