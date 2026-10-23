# M6 acceptance slice: `docs/lang-stdlib.txt`

The chapter has **one code fence**, explicitly **partial** in this audit, and
**no builtin examples**. `test_doc_stdlib.cmake` reads the fence directly and
runs **17 native/diagnostic cases** for its first-version constructor, typed
outcomes and propagation bodies. `print`, `str` and numeric converters are
covered by the type/overview audits; their use as observers here does not turn
`Json.str` or `Json.int` into implemented standard-library functions.

```sh
ctest --test-dir build -L doc-stdlib --output-on-failure
```

Normal and sanitized variants use the shared C runner, requiring exact output,
status, diagnostic messages/source lines, no sanitizer reports and clean debug
ownership exits. Fence/method/case counts, unique names, exact exclusion and
adaptation anchors, and the chapter's six design-goal bullets and non-final-API
qualification are guarded. Generated cases live in
`build/tests/test_doc_stdlib-fixtures/programs/`, separately from the sanitized
variant and checked-in fixtures. M6 remains in progress.

## Fence and prose classification

| Source | Acceptance or explicit boundary |
|---|---|
| Fence 1: `pub User` | Retain the complete nominal definition and its two fields in one source file. `pub` compiles in the single-file context; no privacy or module discovery is established. |
| Fence 1: `new` | Retain the full original signature/body, including explicit `:ok(User)` and `:invalidAge(int)`. Observe invalid ages -1/17 and valid boundary/ordinary ages 18/20. |
| Fence 1: `fromJson` | Retain the full original body and multiline signature, localizing only `Json.Error` to `JsonError`. Deterministic field access supplies the omitted first-version contexts. Check success, both getters' failures, skipped work, validation failure, error compatibility and ownership. |
| Fence 1: `toJson` | Deferred: remove only the exact complete method. `Json.object`, `Json.str`, `Json.int`, pipeline `set`, a JSON value representation and serialization contracts are not implemented or simulated. |
| File/module comment and `Json.Error` | Deferred as module/qualified-library contracts. The comment is retained as a comment, not evidence that `User.brf` is found/loaded. The localized alias checks only supported local symbol-set composition. |
| Typed/composable standard APIs | Design direction. The local methods are test scaffolding, not the published standard library. Collection operations remain with their separate pending chapter audit. |
| Encapsulation | Not proved by a same-file constructor. Module privacy is deferred; same-file direct struct initialization is legal. |
| Constructor invariants | The original constructor rejects ages below 18. This is function behavior, not a compiler-maintained refinement on every User value. `direct-initialization` makes that boundary explicit. |
| Typed parsing/validation errors and compatibility | Representative local error sets and derived contract negatives exercise language checking and propagation. They do not define the future JSON API's actual tags or parsing behavior. |
| Short/readable functions | Qualitative goal. The original in-scope function bodies remain unchanged, not expanded into an invented implementation. |
| Non-final JSON/error-group contracts | Explicitly retained as a scope boundary. A source anchor requires review if the chapter changes that qualification. |

## Scaffolding and ownership

`Json` is a nominal DTO with a heap-backed name, integer age and two integer
status fields. Status 0 succeeds; 1 returns a missing-field error; 2 returns a
wrong-field-kind error. These statuses are internal test controls, not JSON
syntax, a dynamic JSON representation, a schema or a parser.

`getStr`/`getInt` are ordinary read-only methods. Each prints its key before
returning a typed success or one of two illustrative string-payload failures.
`JsonError = Sym<:missing(str), :wrongType(str)>` is a local, transparent named
set chosen only for the fixture. Errors allocate their key strings independently.
The original `fromJson` body still performs its two getter calls left to right
with `?`, then returns through the original `User.new(name, age)?` tail.

`load` owns the DTO locally and propagates the result. Its frame ends before the
observer reads either the successful User/name or an error string. The cases
therefore exercise owned successes and independent failure payloads, including
cleanup of the fetched name when the age getter or constructor fails. The entry
failure cases separately require exact stderr/status and cleanup of active
string versus scalar payloads. These are behavioral/ownership observations,
not exact ARC-count, allocation-frequency or last-use-optimization claims.

## Cases

All cases retain the adapted original fence unless a named negative adapter
changes the single indicated contract/operator/payload.

| Cases | Check |
|---|---|
| `constructor` | Original age guard, accepted boundary, explicit success wrapping and exact scalar failure payload. |
| `getter-flow-and-ownership` | Success, missing/wrong name, missing/wrong age and invalid age. Getter effects prove evaluation order and skipped second work; returned data survives the DTO's local lifetime. |
| `local-error-group` | Both illustrative field errors match the local named group, distinct from validation and success. This is local set composition, not a finalized `Json.Error` contract. |
| `wrong-validation-payload` | Change only the returned validation payload to the known string parameter; require the specific written-return mismatch, not a payload-union cascade. |
| `missing-field-error-contract` | Remove only the localized field-error group from the written `fromJson` outcome; require propagation-contract errors at both original getter calls. |
| `missing-validation-contract` | Remove only validation failure from the written outcome; require the propagation-contract error at the original constructor tail. |
| `unhandled-name`, `unhandled-age`, `unhandled-constructor` | Remove one original `?` at a time and require mandatory handling. Getter cases also pin the existing argument-type follow-on described below. |
| `unhandled-constructor-caller`, `unhandled-from-json-caller` | Unhandled calls in tail position still need handling, even for valid runtime inputs. |
| `missing-missing-arm`, `missing-wrongType-arm`, `missing-invalidAge-arm` | Remove one failure arm from an otherwise complete observer; each requires nonexhaustiveness at the match input. |
| `field-entry-error`, `validation-entry-error` | Exact getter traces, stderr and exit 1; active error payloads and DTO/name owners are cleaned. |
| `direct-initialization` | A same-file literal with age 0 remains legal. Constructor validation is not a global type invariant or proof of privacy. |

The original chapter has no intended-error examples. All negatives here are
explicitly derived tests of its stated error-compatibility/handling goals, not
claims that the original fence contains those errors.

## Diagnostic finish and exclusions

Removing `?` from a getter currently leaves the binding with the full outcome
type. Besides the required handling error, passing that binding into `User.new`
then reports `Function argument has the wrong type.` on the constructor line.
Both messages/source lines are required by these tests; the second is not hidden
or treated as a different successful rejection. Suppressing or clarifying this
follow-on cascade remains M6 diagnostic finish work, recorded in the tracker.

The audit does not establish modules, private fields, library discovery, JSON
text parsing/serialization, collection APIs, custom trait conversions or finalized
standard-library error groups. Independent resolver/checker, IR/RC, emitter,
runtime, conversion and native-driver tests remain in place. No compiler or
runtime changes were needed for this slice.
