# N2 native audit: symbols and unions

Base: `c02b91b`, committed unchanged. The current audit is
`test_doc_symbols_n2.cmake`, using the unchanged `doc_native.cmake` framework.
All 15 fences in `docs/lang-symbols.txt` are hash/count guarded and classified
exactly once. One complete fence executes unchanged; 14 remain listings.
Eleven independent, complete prose programs exercise available rules. No
quoted fence is split, rewritten or completed with missing types/APIs/bodies.

```sh
ctest --test-dir build -L doc-symbols --output-on-failure
```

Normal and sanitizer variants check exact stdout/stderr/status, rejection
messages and source lines, and absence of unexpected fixes or sanitizer output.
Generated sources, cases, inventory and runner logs live under
`build/tests/current-doc-symbols{,_sanitized}/`. Shared manifest guards remain
active and unchanged. Parser classifications are not native evidence.

## Every fence

| Fence | Native disposition |
|---|---|
| 1 | Context: missing `User`; no stand-in payload type. |
| 2 | Context: missing `user`; no splitting construction alternatives. |
| 3 | Context: missing `User`; not optional/API acceptance. |
| 4 | Fragment: bare union type without a declaration/body. |
| 5 | Fragment: interleaved declarations/local usage; no scope relocation. |
| 6 | Context: missing `heron`; no replacement function. |
| 7 | Context: missing `Op`; no removed declarations or invented type. |
| 8 | Later: File API, qualified symbols and N5 interpolation. |
| 9 | Later: merged symbols and external module identity. |
| 10 | Fragment: ellipsis body, external APIs and typed merged matching. |
| 11 | Context: missing `User.new`; not propagation evidence. |
| 12 | Context: missing `User.new`; not panic evidence. |
| 13 | Later: User API, qualified symbols and N5 interpolation. |
| 14 | Run: complete local `Error` declaration with entry observers. |
| 15 | Context: missing `User` and qualified `Json.Error`. |

Fence 14 is the exact file prefix, including its Json file-label comment. The
observer constructs and exhaustively matches all three local errors, inspecting
integer and owned-string payloads: stdout is `7`, `owned key`, `owned type`;
stderr is empty and status is zero. A comment does not create a Json module or
prove exported/cross-module union composition.

## Independent prose coverage

These are newly authored programs, not adapted or extracted document fragments:

- `prose-neutral-classification`: a named data union in a struct, all three
  classification branches and a discarded ordinary-data call. This does not
  claim declaration-order comparison support.
- `prose-positional-payloads`: zero ambiguity between one, two and three
  payload positions, contextual numeric literals and owned positional text.
- `prose-absent-and-unit`: distinct zero-position and unit-payload values,
  including a unit payload's observable effect and empty `:none[]` syntax.
- `prose-forward-and-recursive`: forward aliases through a struct and an
  acyclic owned tree. No cycle collection is claimed.
- `prose-contextual-nesting`: contextually widened nested unions and a nested
  `:none` that stays distinct from the surrounding optional's `:none`.
- Six rejection programs pin kind conflicts, conflicting type/shape schemas
  within a union, a plain union member, unguarded alias recursion, a `:none`
  payload and an already-typed payload that cannot widen implicitly. Their
  seven exact diagnostics have no guessed fixes.

Available kind/postfix/optional/entry rules also have independent programs in
[the errors audit](doc-errors-n2.md) and the existing N2 unit/native fixtures.
There is no invented User, Shape API, Op enum, File/Db module or JSON parser.

## Expectations and limits

**Changed existing expectations: none.** No existing fixture, runtime marker,
golden, `.expect`, quoted fence, parser classification or native N1 case
changes. New kind-conflict, recursion and typed-payload markers initially
guessed the wrong wording; they now use existing exact compiler messages.
No compiler behavior or existing expectation was changed to make them pass.

This is scoped N2 evidence, not full chapter acceptance. Declaration-order tag
comparison is still unsupported. Module-wide schema consistency across disjoint
sets is not established here; the existing checker enforces schema identity
when sets/values meet. Modules, merged/typed/refutable matching, multi-position
success extraction and cycle collection retain their existing boundaries.
The broader language wording is not silently weakened by this audit.

The slice's full validation, unchanged-source comparison and N2 closure decision
are recorded in [the closure review](next-version-n2-review.md). In this audit,
12 programs comprise six native executions and six compiler rejections.
