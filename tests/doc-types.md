# M6 acceptance slice: `docs/lang-types.txt`

`test_doc_types.cmake` reads all **24 code fences** directly from the chapter.
Fifteen are represented in **64 native/diagnostic cases**; nine require deferred
features or provisional library APIs. Fences **20 and 21 are partial**: content
equality is checked, but the `same(...)` identity calls remain deferred.
Selected first-version inline/prose rules have additional guarded cases. The
[final M6 review](first-version-review.md) records overall completion; this
chapter is not a claim that all language-design types are implemented.

```sh
ctest --test-dir build -L doc-types --output-on-failure
```

Normal and sanitized variants use the shared document harness and C runner.
They require exact stdout/status, every diagnostic message and source line,
source-located panics, no sanitizer reports, and leak-free normal debug exits.
Fence/classification/case counts, unique names, adaptation boundaries and prose
anchors are guarded. The primitive and escape tables are also exact anchors.
Generated files live under `build/tests/test_doc_types-fixtures/programs/`,
with a separate directory for the sanitized variant.

## Fence-by-fence record

| Fence | Example | Check or classification |
|---|---|---|
| 1 | Contextual literals | `literal-context`: keep all declarations, setter body, call and arithmetic. Move only the exact setter definition to file scope; run the other statements in `main`, observing count, ratio, updated age and total. |
| 2 | Default literal types | `literal-defaults`: retain both declarations; pass the results to explicitly `int`/`dub`-typed functions before observing them. |
| 3 | Constant annotations | Deferred: `const`. The analogous local binding contexts/defaults execute in fences 1/2; they do not establish constant support. |
| 4 | No implicit numeric conversions | `implicit-conversions`: retain every statement and require the two binding-contract errors and the mixed-operand error on their original lines. |
| 5 | Conversion calls/pipelines | `infallible-conversions`: retain every original conversion expression. Rename only the second duplicate `label` binding to `pipedLabel`, allowing both alternative spellings to execute and be observed. |
| 6 | Fallible conversions | `fallible-conversions`: retain both `?` calls and the print; supply typed text/wide-integer arguments. Exercise success, invalid heap text and integer overflow. Effects show that failure skips subsequent work, and handlers inspect both error shapes. |
| 7 | Overflow/underflow | Split the two alternatives at the unique blank line. `signed-overflow` and `unsigned-underflow` require the integer-overflow panic and original update's source location. |
| 8 | Checked/wrapping library arithmetic | Deferred: provisional `checkedAdd`/`wrappingMul` APIs, whose names are explicitly not final. No fabricated wrapping implementation is used to claim library acceptance. Checked language operators are covered separately. |
| 9 | Division/remainder signs | `signed-division`: wrap each complete original expression in `print(str(...))`, preserving its comment and grouping; require 3, -3 and -1. |
| 10 | Division by zero | `division-zero`: retain the original binding with total 7 and count 0. `remainder-zero` changes only the anchored `/` expression to `%`; both require the panic and operation's source location. |
| 11 | String concatenation | `string-concatenation`: retain the declarations; observe both greeting and unchanged name, then show that `+=` on another binding replaces only that binding's string. |
| 12 | Shared immutable strings | `shared-string`: retain both declarations; replacing a third binding's string leaves both original values unchanged. This demonstrates value behavior, not an identity/count measurement. |
| 13 | Escaped newline | `escaped-newline`: retain the source-line literal and print it; require the two resulting output lines. |
| 14 | Byte characters/UTF-8 text | `character-and-utf8`: retain the `chr` declaration; wrap the two complete string expressions in `print`, checking the exact three/two UTF-8 bytes plus output newlines. Observe the character by equality, not an unsupported `str(chr)` conversion. |
| 15 | Written empty return type | `written-unit`: keep the complete `pub add(...): ()` function and body. A one-slot nominal collection supplies `userVec.add`; its owned string result is discarded by the original written-unit function. Observe unit success, retained user and visible alias mutation. No generic collection or privacy claim is made. |
| 16 | Named tuple values | Deferred: named tuples and field access on them. |
| 17 | Named tuple return types | Deferred: named tuples. The integer `/` and `%` expressions are independently exercised, not counted as acceptance of the tuple-returning function. |
| 18 | Tuple ordering | Deferred: named tuples and their ordering. Struct equality is not an ordering substitute. |
| 19 | `Fn` types | Deferred: function types/function values. |
| 20 | Content/identity equality | **Partial:** `content-equality` retains the three bindings and both content comparisons, with whole-expression output wrappers. Remove only the two exact `same(...)` lines. Independently constructed equal users compare equal; changing `b` makes it unequal while `a`/`c` remain equal. Identity comparison remains unsupported. |
| 21 | Recursive-shape equality | **Partial:** `recursive-equality` retains the full `Node` declaration and equality expression, supplying typed `nodeA`/`nodeB` parameters. Require the reference-cycle equality diagnostic on the original equality line. Remove only the exact identity-call line; no identity operation or runtime cycle is synthesized. |
| 22 | Maps | Deferred: `Map`, generic collections, lookup/update, hashing and insertion-order iteration. |
| 23 | Array/vector type notation | Deferred: fixed arrays and generic vectors, not native type support. |
| 24 | Nested vector construction | Deferred: generic collections and user-written generics. Parser coverage remains separate. |

## Scaffolding and fidelity

`User` is a local nominal stand-in with heap-backed names and a deterministic
fallible associated constructor. Its age is `shrt`, matching fence 1's unchanged
setter rather than silently inserting a conversion from another age type. The
constructor rejects negative ages; it does not establish a real user library.
The fallible-conversion fragment receives actual `str`/`lng` parameters so that
numeric conversions retain the documented source domains.

`UserSlot` and `UserCollection` retain one `User` through writable parameters.
The slot method returns an allocated string to exercise discarded owned results.
This supplies only fence 15's omitted method/field context, not `Vec`, collection
iteration, modules, or public/private visibility enforcement. The original
retaining function is not rewritten.

Output adapters wrap whole expressions only. The duplicate label is a binding
rename, not a change to either conversion. Alternatives are split at guarded
boundaries; every intended error remains present, with all actual diagnostics
required. No unavailable identity call is replaced with pointer equality or an
approximate user function.

## Guarded inline and prose rules

- Primitive domains: signed minima, unsigned maxima, whole-number literals in
  floating contexts, byte characters, bools and the sole unit value. Remaining
  integer-width overflow cases supplement fence 7 so every integer type panics
  at its boundary in both native configurations. These observe language domains,
  not `sizeof` or a foreign ABI.
- Literal typing: out-of-range values for all six integer types, decimal-to-int
  annotation rejection, binary32 direct rounding versus double rounding,
  ties-to-even, slightly-above-maximum finite rounding, subnormals, signed-zero
  underflow and literal overflow rejection.
- Arithmetic: signed-minimum division panic versus remainder zero; negative
  integer exponent and invalid shift-count panics (including shifting zero);
  `0 ** 0`, negative signed left/arithmetic right shifts, unsigned right shift
  and floating negative powers.
- IEEE behavior: infinities, signed-zero division, NaN and finite floating
  overflow. A struct containing NaN compares unequal even to itself, checking
  content equality without an identity shortcut.
- Numeric domains: widening, rounded integer-to-float success, `flt` to `dub`,
  signed `dub`-to-`flt` finite-boundary rounding/overflow, preserved infinity/NaN,
  underflow to negative zero, float-to-integer zero/integrality/nonfinite checks,
  and fallible signed-to-unsigned conversion. Typed/computed inputs cannot avoid
  handling by having a small runtime value.
- Literal conversion contexts: direct, parenthesized, negated and pipeline
  literals; a wide `lng` literal; decimal-point-to-integer conversion fails
  rather than truncates. Wrong arity, bool/struct inputs, custom `str(user)`
  and non-string `print` arguments require specific current builtin diagnostics.
- Text conversions: all eight targets; full-input decimal grammar, leading
  signs/zeroes, unsigned `-0`, malformed/range errors, empty/whitespace/hex/
  separator/NUL rejection and exact original invalid payloads, including an
  allocated binary string. Floating decimal/exponent/special-value grammar,
  direct binary32 rounding, subnormals, signed-zero underflow and text overflow
  are observed. Source exponent notation remains rejected independently.
- Formatting: bools, both float widths, fractional/integral values, signed zero,
  infinities and NaN. These are selected examples of the documented initial
  formatter, not an exhaustive shortest-text oracle.
- Strings/characters: complete byte comparisons across NULs, prefixes, high UTF-8
  bytes and distinct Unicode spellings; every supported escape; unsupported
  escape diagnostics; multibyte character rejection; raw LF/CRLF unterminated
  string/character recovery; character arithmetic rejection, and the
  documented explicit alternative: `chr` converts to every numeric target
  without failure, directly and as a pipeline stage.
- Unit effects, owned tail discarding, unit fields/equality and nominal struct
  identity of types (identical fields do not make two structs assignable).

## Independent coverage and remaining limits

The arithmetic, conversion, object, matching, semantic, IR and ownership suites
remain independent. In particular, all 64 numeric domain pairs, exhaustive/
random runtime bounds, round trips and character ranks are not replaced by this
chapter's selected observations. `test_objects.c` directly checks stored string
lengths, trailing NULs, immortal literals and counts; the Brief cases do not
claim C interop or exact transient ARC counts. `test_lexer.c` separately checks
exact backslash-before-newline bytes and recovery for LF/CRLF. Native invalid
literal cases add their expectation text before converting to CRLF; they do not
claim that an inline annotation preserves a final backslash byte.

The audit exposed two gaps, both fixed. The chapter required an explicit
conversion for character arithmetic but its converters accepted only numbers
and strings; the chapter now lets numeric converters take a `chr`, which always
succeeds with its unsigned byte value. Converting a number back to `chr` is not
part of this change. Recursive-shape equality also reported the generic
`Comparison of this type is not implemented yet.`, implying future support for
a permanent rule; it now reports `Types that can contain a reference cycle have
no == or !=.`, while ordering comparisons keep the not-implemented message.
A later M6 lexer slice replaces the generic separator error for source exponent
notation with `Exponent notation is not supported in source number literals.`.
Recognized integer/fraction-plus-`e`/`E` suffixes (including a boundary-ending
unfinished marker/sign) form one source-backed error token. Identifier-like
suffixes, separated tokens, ordinary literals, member/range punctuation and
following line endings are preserved. Lexer/parser/CLI regressions check exact
spans, LF/CRLF recovery and duplicate suppression; this audit's original source
negative pins the new message. Numeric text conversions still accept exponents,
including signed uppercase forms; no source exponent feature is implemented.
Named tuple semantics, identity, custom trait conversions, provisional
arithmetic libraries and collection/FFI behavior are not established by this
audit.
