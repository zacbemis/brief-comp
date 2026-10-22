# M5 acceptance audit: `docs/lang-errors.txt`

M5's definition of done is the **first-version** error-handling examples,
including their compile errors. It is not every future symbol feature. The scope
is `docs/lang-desc.txt`, “First version”, and the milestone table in the plan.
Refutable/nested payload patterns and symbol ordering are deferred features,
not M5 acceptance requirements.

`test_doc_errors.cmake` reads all 36 code fences directly from the document.
It checks 28 fences in **49 generated cases**, through native compilation and
execution or exact diagnostic/message/source-line checks. Both normal and
sanitized CTest variants run the existing C program runner. Run just this audit:

```sh
ctest --test-dir build -L doc-errors --output-on-failure
```

The eight excluded fences require modules or collections/lambdas. Every fence
must be explicitly tested or deferred; the fence-count guard and adapter anchors
fail on document drift rather than silently dropping a case. This is a targeted
M5 acceptance test, not M6's broader audit of every first-version design document.

## Scaffolding, not a standard-library implementation

The document explicitly leaves irrelevant function bodies out and calls its
library names illustrative. The tests provide local nominal `Json`, `User`, and
`File` structs, deterministic functions with the documented signatures, and
`impl` methods. These use only first-version features: there is no JSON parser,
filesystem access, `Vec`, lambda, or module loader hidden in the tests.

Complete example bodies and usage fragments are retained. Adapters supply
omitted bodies, put usage inside functions, give illustrative methods their local
`impl` declarations, and split alternatives that deliberately reuse a function
name. The expanded `?` fragment is completed with its bound value as the tail.
Compile-error annotations are supplemented with exact compiler expectations.
Negative cases must fail for the documented semantic reason, not an unknown
library name, parser error, or duplicate alternative declaration.

## Fence-by-fence record

Numbers are the one-based code-fence indices, also used by the parser manifest.
Case names below refer to generated `.brf` files under
`build/tests/test_doc_errors-fixtures/programs/` (and the analogous sanitized
fixture directory).

| Fence | Section / claim | Check or exclusion |
|---|---|---|
| 1 | Explicit `:ok`, `?` unwrap/propagate | `explicit-success`: supply the omitted `parse` body; exercise success and `:syntax(7)`. |
| 2 | Plain tail wraps success | `implicit-success`: same two outcomes as fence 1. |
| 3 | Expanded `?` match | `expanded-try`: bind success, return residual error; same observations as fences 1–2. |
| 4 | `!` | `panic-success`, `panic-failure`: unwrap success; failure reports its tag/payload and original call-site file/line. |
| 5 | Optional collection result | Deferred: `Vec`, `.find` and lambda. Scalar propagation is checked above and in `test_errors`. |
| 6 | Payload-free `:ok` | `payload-free-ok`: local `File.close` signature/body and the unchanged call fragment. |
| 7 | No `:ok` to unwrap | `no-success`: the data-only classifier call rejects `?` at the usage line. `test_errors` also rejects `!`. |
| 8 | Input signatures for accumulation | `declarations`: fill the omitted bodies; observe a read failure and parse failure. |
| 9 | Written accumulated errors | `accumulation-9`: observe success, not-found, permission-denied and syntax. |
| 10 | Pipeline accumulation | `accumulation-10`: same four outcomes as fence 9. |
| 11 | Missing propagated errors | `missing-errors`: exact propagation diagnostic at `readFile(path)?`. |
| 12 | Inferred complete set | `accumulation-12`: an exhaustive observer requires exactly the four expected tags and their payload types. |
| 13 | Partly written return | `accumulation-13`: the same observer checks fixed success and inferred errors. |
| 14 | Multi-file error aliases | Deferred: two modules, exported prototypes and module-qualified sets. Local aliases/composition are covered by `test_symbols` and `symbol-groups.brf`. |
| 15 | Module-qualified union and APIs | Deferred: `File.Error`, `Json.Error` and the corresponding module APIs. Local set composition/propagation has independent symbol/outcome regressions. |
| 16 | Expanded seven-outcome type | `expanded-set`: wrap the exact type in a local alias with a nominal `User`. |
| 17 | Same-payload collision across libraries | Deferred: qualified `File.read` / `Http.get` declarations are multi-module API notation. Same-tag/same-payload unions are checked in `test_symbols`. |
| 18 | Conflicting payload types | `payload-collision`: reject both the conflicting written set and the conflicting propagated errors. |
| 19 | Rename a colliding error | `renamed-collision`: observe success, renamed `:syntax(int)` and unchanged `:invalid(str)`. |
| 20 | Generic error tag is discouraged | `generic-tag-is-legal`: “avoid” is guidance, not a compile error; `:err(str)` remains legal. |
| 21 | Specific tags and response choice | `specific-tags-0` through `-2`: a local associated `File.open` stand-in supplies each of the three outcomes; the original match prints each documented response. |
| 22 | Discarding a fallible call | `unhandled-22`: exact handled-call error, even though the mock would succeed. |
| 23 | Three accepted handlers | `handled-success`, `handled-panic`, `handled-propagation`, `handled-match`: success, panic location/payload, propagated entry error/status, and handled failure output. |
| 24 | Storing is not handling | `unhandled-24`: exact handled-call error on the initializer. |
| 25 | Infallible result may be ignored | `infallible-print`: unchanged statement prints `Hello`. |
| 26 | Handled error disappears | `removed-error`: success, handled not-found yielding `{}`, permission-denied and syntax; observer cannot mention the removed tag. |
| 27 | Subset propagation through module APIs | Deferred: `Json.Error` and JSON getter APIs. Local subset transfers are covered by `test_symbols`; the reverse-direction error is fence 28. |
| 28 | Larger error set cannot fit a smaller one | `no-widening-errors`: local `User.new` cannot propagate `:invalidAge` into a success-only written result. |
| 29 | Plain success ABI | `plain-abi`: unchanged `double` definition returns 6 for 3. Independent `test_errors` assertions check its erased C ABI and full semantic outcome. |
| 30 | Plain use and symbol view | `success-as-symbol`: unchanged usage prints 6 from `:ok`, then the wrapped test prints the bound ordinary value 7. |
| 31 | Lambda outcome parameterization | Deferred: collection selection, `Vec`, lambda and generic error sets. |
| 32 | Handling at the final call | Split the two alternatives: `unhandled-tail` rejects bare `parse()`; `handled-tail` observes all four outcomes with `parse()?`. |
| 33 | Handling inside a lambda | Deferred: collection selection and lambdas. Ordinary argument/return/forwarding enforcement is covered by `test_errors` and `errors/handled-calls.brf`. |
| 34 | Data-only symbols are ordinary values | `ordinary-data`: unchanged `describe`, with discarded, stored, passed and tail-returned results; exits 0. |
| 35 | Module-qualified group pattern | Deferred: `Json.Error` and JSON/User module APIs. Local named-group usefulness/exhaustiveness is covered by `test_symbols` and `symbol-groups.brf`. |
| 36 | Exhaustive seven-outcome handling | `seven-outcomes-0` through `-6` execute every original arm; `missing-arm-0` through `-6` reject each individual removal; `added-variant` rejects a newly unhandled tag. |

## Rules outside fenced snippets

Section 12 has prose rather than a code fence. Existing fixtures check ordinary
and successful entry results (`entry-value.brf`, `entry-object.brf`,
`main-data-symbol.brf`, `main-symbol-success.brf`), fallible entry results and
owned error cleanup (`main-error.brf`, `main-error-object.brf`,
`main-conversion-error.brf`), and panic payload/location behavior
(`panics/symbol-error.brf`). Native CLI tests additionally check stdout/error
ordering and bounded diagnostic output on shared payload graphs.

`test_errors` and `test_symbols` retain stage-level assertions for partial-return
restrictions, payload identity (including absent versus `()`), recursive error
closure, exact residual narrowing, permissions, repeated checking, and
independent/idempotent ownership insertion. This audit supplements those checks;
it does not claim the excluded future APIs execute today or that representation
optimizations such as zero-cost set widening are finished.
