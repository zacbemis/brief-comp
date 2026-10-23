# M6 acceptance slice: `docs/lang-functions.txt`

`test_doc_functions.cmake` reads all 27 code fences directly from the document.
Twenty fences are represented in **33 native/diagnostic cases**; fences 9 and 27
are explicitly partial. Seven fences require function values/lambdas, collection
iteration or traits and are deferred. Selected prose rules have additional cases.

```sh
ctest --test-dir build -L doc-functions --output-on-failure
```

Normal and sanitized variants use the shared document harness and C program
runner. Output, status, exact diagnostics/source lines, panic location and
sanitizer failures are checked. Fence counts, adapter anchors, classifications,
unique generated names and case counts are guarded. This is another M6 slice,
not completion of the milestone.

## Fence-by-fence record

Case names refer to generated `.brf` files under
`build/tests/test_doc_functions-fixtures/programs/`, with a separate directory
for the sanitized test.

| Fence | Example | Check or classification |
|---|---|---|
| 1 | Written primitive return | `add`: the unchanged definition returns 6; effectful argument helpers show left-to-right calls. `immutable-parameter` adds a rejected parameter assignment. |
| 2 | Explicit early return | `explicit-returns`: negative, below-threshold, threshold and above-threshold ages return the expected booleans. |
| 3 | Anonymous function | Deferred: lambda/function value. |
| 4 | Function types and higher-order APIs | Deferred: `Fn`, lambda, generic collection and function-valued field declarations. |
| 5 | Optional names in function types | Deferred: `Fn` declarations. |
| 6 | Lambda contextual parameters | Deferred: lambdas, including their documented parameter-inference error. The error is not counted as tested by rejecting all lambdas. |
| 7 | Captures | Deferred: lambdas, captured variables and collection APIs. In particular, rejecting unsupported lambda syntax is not proof of the `var` capture rule. |
| 8 | Iteration over users | Deferred: collection iteration, not first-version integer range loops. |
| 9 | Three return signatures | **Partial:** complete the first two headers with the same local body in `inferred-signature` and `partial-signature`; an exhaustive observer requires the expected success/error payloads. The third header's `File.Error`/`Json.Error` module-qualified sets remain deferred. |
| 10 | Inferred integer | `inferred-number`: the unchanged `double` produces an `int` binding and prints 6. |
| 11 | Inferred data-only set | `inferred-data`: observe both `:minor` and `:adult`, require exact coverage, and allow an unhandled ordinary-data call. |
| 12 | Inferred accumulated outcomes | `load-12`: retain the complete function body; local associated-function stand-ins exercise success and all four error variants. |
| 13 | Fixed success, inferred errors | `load-13`: same five outcomes and exact observer as fence 12. |
| 14 | Incorrect partial success | `wrong-success`: nominal `User` cannot satisfy `Admin`, even with identical fields; require the body-contract diagnostic at the function, without a secondary symbol-payload collision. |
| 15 | Incorrect written primitive | `wrong-tail`: the body-contract diagnostic is at `double`, not at a caller. |
| 16 | Undeclared explicit return | `undeclared-variant`: exact return-contract diagnostic at `ret :minor`. |
| 17 | Trait methods | Deferred: trait declarations/default methods and `Self` in a trait. |
| 18 | Assignment infers unit | `assignment-unit`: mutate the caller's object and require payload-free `:ok`. Derived cases reject replacing a `&` parameter binding and mutating through an ordinary parameter. |
| 19 | Unit-returning tail call | `call-unit`: log the owned name and require payload-free `:ok` from the unchanged function. |
| 20 | Inferred owned tail result | `inferred-owned-tail`: retain the unchanged body with a local nominal slot API; observe both its stored user and returned heap string. |
| 21 | Written unit drops tail | `discarded-owned-tail`: the same slot operation still happens, but its owned string is released and the outer function returns unit. |
| 22 | Unit cannot discard failure | Split the two alternatives. `unit-does-not-handle` rejects the raw fallible call; `unit-handled` succeeds with `!`; `unit-panic` checks failure output, payload and the original inner call-site location. |
| 23 | Ignore an ordinary integer | `ignored-int`: supply a local nominal `Users.len`, execute the original statement before another statement, and observe that the call happens without printing its integer result. |
| 24 | Cannot ignore failure | `ignored-failure`: exact handled-call diagnostic, regardless of whether the mock would succeed. |
| 25 | Receiver/direct/pipeline calls | `call-syntax`: execute all three original statements; the getter and both `save` calls have observable effects. |
| 26 | Non-receiver summary function | `local-summary`: retain the complete declaration and file comment, but call the function in a single file, directly and by an unqualified pipeline. This does not test module exports or loading. |
| 27 | Method versus module call | **Partial:** `not-a-method` rejects the original `user.summary()` line even with a local `summary(User)` function in scope. The two `Report.summary` module-qualified usages remain deferred. |

## Scaffolding and scope

Struct names such as `File`, `Json`, `User` and `Admin` are local nominal types.
The `File.read`, `Json.parse` and `User.fromJson` stand-ins are first-version
associated functions with deterministic outcomes, not modules, filesystem I/O or
a JSON implementation. This lets fences 12–14 keep their bodies unchanged while
checking their return-typing rules. Unlike a `File.Error` type path, these call
expressions also have a first-version associated-function interpretation. The
earlier pipeline audit conservatively deferred its module-API examples; these
local counterparts do not change that classification or claim module support.

For fences 20–21, `UserCollection.userVec` is a local `UserSlot`, not `Vec`. Its
`add` method stores one user and returns a heap string. Both `&` parameters allow
that retention; the function examples' headers and bodies are unchanged. This
checks the documented return/discard behavior without inventing collection
semantics. `Users.len` in fence 23 is likewise an ordinary local method.

Only omitted contexts/bodies and explicit alternative boundaries are adapted.
The first two fence-9 headers receive a body below their original comments;
fence 22's alternatives become separate programs. Negative cases must fail for
the documented rule, not an unavailable library name or duplicate declaration.

## Additional prose and diagnostic regressions

Guarded prose examples check:

- ordinary parameter immutability and the difference between mutable data access
  and replacing a parameter binding;
- ordinary struct-parameter read-only access;
- unanchored recursive-return rejection and a finite annotated recursive call;
- shared literal context across early/tail returns, with no implicit conversion
  of an already-typed number;
- ignoring unreachable returns for inference while still checking dead syntax;
- left-to-right arguments/operands, boolean short-circuiting, target-before-RHS
  assignment, and capturing a compound assignment's old value before RHS effects.

The audit exposed a redundant diagnostic when a return violates a partial
annotation: inference then tried to union its rejected `:ok` payload with the
written one. `checked_return` now reports the specific contract error and lets
the rejected value contribute nothing to inference, so the function keeps its
written contract and callers are still checked against it. `test_errors` checks
scalar/nominal, plain/explicit-success, early/tail, multiple independent and
dead-return cases, repeat checking, preservation of genuine conflicting error
payloads, and that a caller's unhandled call and type mismatch are still
reported. It does not suppress unrelated diagnostics based on a global error
count.

The runtime's host-stack recursion limit remains documented; this audit does not
try to exhaust the host stack or treat the limitation as an unimplemented
recursion feature.
