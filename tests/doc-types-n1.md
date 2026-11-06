# Current types native audit (N1 in progress)

`test_doc_types_n1.cmake` reads `docs/lang-types.txt`, not `docs/v0/`. All **28
fences** are classified: six execute successfully, one reaches its first
integer-overflow panic, one rejects all three documented conversion errors,
and **20 remain listings**. Twelve independent prose programs bring the audit
to **20 cases**: 17 native executions (five expected panics) and three
compiler-rejection programs. Listings and prose are not substituted fence/API
acceptance.

```sh
ctest --test-dir build -L doc-types --output-on-failure
```

Normal and sanitized variants use the existing binary-safe C program runner.
The sanitized variant has the same support gate as other native program tests.
Shared compiler-free manifest guards run in every configuration. The historical
[types audit](doc-types.md) stays retired; no old adapters or API mocks return.

## Fence inventory

| Fence | Current native evidence or explicit limitation |
|---|---|
| 1 | Context: mixed definitions/usage need User/user. No type or receiver is invented. |
| 2 | `default-literals`: unchanged declarations, typed int/dbl observers, 10 and 0.5. |
| 3 | Listed: const and duplicate alternatives, not native literal-inference acceptance. |
| 4 | `no-implicit-conversions`: all three exact diagnostics, on original lines 2, 4 and 7 (generated lines 3, 5 and 8). |
| 5 | Context: missing numeric inputs and duplicate label alternatives. No inputs or splitting. |
| 6 | Context: missing text/big/count. No stand-ins or N2 policy acceptance. |
| 7 | `first-overflow`: unchanged whole fence panics on shrt addition, message/location/status checked. The later uint decrement is unreachable, not extracted; it has an independent prose regression. |
| 8 | Context: unspecified checkedAdd/wrappingMul library APIs and missing inputs. |
| 9 | `division-statements`: unchanged bare expressions compile/run as discarded statements. Their values are asserted by a separate authored program, not by rewriting the fence. |
| 10 | Context: missing total/count and zero condition. |
| 11 | `concatenation`: original name and derived greeting print Bob/Hello Bob. |
| 12 | `sharing`: both original bindings print Bob. Owned sharing is tested separately. |
| 13 | Listed: N5 interpolation/brace escape and missing user context. Not accepted as ordinary strings. |
| 14 | `escaped-newline`: unchanged literal prints first/second on separate lines. |
| 15 | `character-and-utf8`: char annotation, equality and numeric conversion observed. UTF-8 string statements compile unchanged; no length API or str(chr) is invented. |
| 16 | Context: missing UserCollection/User API; no unit-return stand-in. |
| 17–19 | Listed: named tuples are on hold. |
| 20 | Listed: Fn types and unavailable User. |
| 21 | Context: missing User.new and planned same API. No substitute identity helper. |
| 22 | Context: mixed Node declaration and undeclared nodeA/nodeB/same usage. The cycle-equality rule is checked independently, not by extracting the declaration. |
| 23–28 | Listed: collections, indexing or iteration. Unsupported syntax/APIs do not prove the documented unfilled-array/index errors; fence 28 also needs N5. |

## Independent prose programs

- `prose-context-and-conversions`: contextual lng/shrt/flt literals, signed
  minimum, direct/pipeline literal conversion, typed integer widening and
  integer-to-float rounding without a fallible outcome.
- `prose-conversion-failures`: successful text parsing, owned invalid text with
  an embedded zero, typed narrowing overflow and fractional-to-int failure.
- `prose-floating`: signed zero, infinity, NaN inequality, float text parsing
  and handled dbl-to-flt conversion.
- `prose-integer-arithmetic`: truncation/remainder signs, signed-minimum
  remainder, power (including zero-to-zero) and same-width signed right shift.
- `prose-owned-strings`: owned alias preservation after concatenation,
  embedded-zero/prefix/UTF-8 byte ordering and character comparison.
- `prose-struct-equality`: generated content equality on real local Records,
  with separate allocations, shared aliases and subsequent field mutation.
- `prose-cyclic-equality`: types capable of a reference cycle have no equality,
  even when the compared values themselves are finite.
- `prose-literal-rejections`: three exact out-of-range diagnostics.
- `prose-uint-underflow`, `prose-minimum-division`, `prose-division-zero` and
  `prose-remainder-zero`: panic messages, generated source lines and status 1.

No program supplies an API to a quoted fence. Content equality is not a claim
that the planned `same` function exists. Cross-width shift counts remain N4;
interpolation and its conversions remain N5. N2 outcome/entry/formatting and N3
handling rules are not advanced. No production compiler/runtime code changes.

## Integrity and artifacts

The shared native framework now encloses statement fences in `main`, keeping
all quoted bytes and trivia intact. Observers follow the complete body; they
do not extract expressions or replace declarations. Rejected bodies keep
expectations outside the source. Diagnostic records store each exact message
and original line separately, so semicolons cannot split a message. The only
line adjustment is the wrapper's one-line offset. Counts, ordered messages,
locations, status 1, empty C output, sanitizer reports and unexpected fixes are
checked. Existing whole-function fence generation is unchanged.

The compiler-free manifest positive now checks both top-level and body
wrappers byte-for-byte, including mixed LF/CRLF, Unicode, braces, semicolons,
comments and multiple-diagnostic frame offsets. Twelve negative guards include
malformed message/line pairs, invalid fence lines and listed-as-body attempts.
The real three-error fence also exercises a semicolon-bearing diagnostic.

Sources, case provenance, classifications and logs are generated under
`build/tests/current-doc-types/` and `current-doc-types_sanitized/`. Shared
manifest reports stay under `build/tests/doc-native-manifest-fixtures/`.
Parser counts remain unchanged. Functions and types now have scoped native
audits; syntax, modules, the raw base sample and remaining casing/keyword review
are still pending. N1 remains in progress.
