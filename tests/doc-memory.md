# M6 acceptance slice: `docs/lang-memory.txt`

`test_doc_memory.cmake` reads all **17 code fences** from the document. Ten are
represented in **48 native/diagnostic cases**; fences **4, 6 and 7 are partial**.
Seven fences require cloning, `const` or real resource APIs and are deferred.
Guarded prose cases also check first-version access-path propagation and ARC
behavior. This does not implement ORC cycle collection or complete M6.

```sh
ctest --test-dir build -L doc-memory --output-on-failure
```

Normal and sanitizer-enabled variants use the shared document harness and C
runner. They check exact stdout, statuses, diagnostic messages/source lines and
sanitizer failures. Successful debug exit must have no live-owner report. Fence
counts, classifications, single adaptation anchors, alternative boundaries,
unique case names and the expected case count are guarded.

## Fence-by-fence record

Case names identify generated `.brf` files under
`build/tests/test_doc_memory-fixtures/programs/`, with a separate directory for
the sanitized variant.

| Fence | Example | Check or classification |
|---|---|---|
| 1 | `user` / `other` shared references | `shared-reference`: retain both original bindings; changing age through `other` is visible through `user`, and changing the heap name through `user` is visible through `other`. |
| 2 | Primitive copies | `primitive-copy`: retain the original bindings, mutate another copy, and observe unchanged `a`/`b` and the changed copy. |
| 3 | `user.clone()` | Deferred: cloning and its shallow/deep-copy policy. |
| 4 | Borrowed `getAge` call | **Partial:** `borrowed-call` retains the function and local statements, wrapping only the latter in `main`; the result is 20. Its `no count change` comment is not asserted: the current caller pins reference-valued reads, while the callee borrows. Count elimination remains a future optimization, not a missing language feature. |
| 5 | Collection retains an argument | `retained-argument`: retain the complete `pub add` declaration and supply a local one-slot API. The stored user outlives its creating function and remains writable through the slot. This tests keeping/ARC permissions, not `Vec`. |
| 6 | Nonrecursive `User` type | **Partial:** `nonrecursive-type` retains the declaration, constructs shared references and releases the heap data normally. The comments' future collector exclusion/classification claim is not tested; no collector exists. |
| 7 | Recursive `Node` type | **Partial:** `recursive-type` retains the declaration and constructs an **acyclic** two-node graph; both active next variants are observed and debug cleanup is leak-free. The comments about collector inspection and reclamation of cycles remain deferred. |
| 8 | Ordinary borrow | `immutable-borrow` executes the unchanged getter. `immutable-borrow-write` inserts a write before its return and requires the precise ordinary-parameter mutation error, not a return-type mismatch. |
| 9 | Mutable borrow | `mutable-borrow` executes the unchanged setter with an ordinary caller argument and observes the mutation through another alias. `immutable-parameter-binding` rejects replacing the `&` parameter binding. |
| 10 | Invalid/valid `attach` alternatives | Split the two original definitions at their single blank-line boundary, preserving the semicolon in the original error comment. `readonly-store` requires the field-store diagnostic; `writable-store` observes shared mutation and retention beyond the storing caller's scope. Derived alias/call/literal stores and a read-only caller are also rejected for their intended permission rule. |
| 11 | Shared aliases permit mutation | `nonexclusive-aliases`: retain the original bindings; an ordinary parameter sees a change made through a simultaneous writable parameter to the same object. Read-only access does not freeze the object or imply exclusivity. |
| 12 | `const` initialization | Deferred: `const`. |
| 13 | Freezing an existing binding error | Deferred: the intended `const` rule is not proved by rejecting all `const` syntax. |
| 14 | Clone into `const` | Deferred: cloning and `const`. |
| 15 | `const` assignment / mutable-argument errors | Deferred: `const`, including these specific intended errors. |
| 16 | `const` collection call results | Deferred: `const` and collection APIs. |
| 17 | File open/read/close lifecycle | Deferred: actual external-resource APIs. No filesystem implementation, automatic resource closing or `defer` is claimed; existing error-document mocks separately check fallible calls/handlers. |

## Scaffolding and scope

`User.new` is a local associated constructor with deterministic success/error
outcomes. Its names are heap-backed, and the successful examples use it with
nonnegative ages. `Report`, identity/getter functions, `Envelope`, `Summary` and
symbol-holder types are first-version nominal scaffolding, not library APIs.

Fence 5 uses the same kind of stand-in as the function audit: a `UserCollection`
contains one `UserSlot`, whose `add` stores an owned reference. Both retaining
parameters take `&`. The original declaration/body is unchanged, but no vector,
collection iteration or generic collection semantics are implemented or claimed.

No cyclic runtime graph is created. Without cycle collection, cycles remain live;
passing an acyclic recursive-type test is not evidence of their reclamation.
Weak references, concurrency, count-elimination optimizations, cloning and
`const` remain outside this slice. In particular, ordinary read-only access is
not deep `const` and does not prevent mutation through another writable alias.

## Guarded prose cases

| Rule | Cases / observations |
|---|---|
| Read-only field paths cannot be written/forwarded | `field-write`, `field-forward`. |
| Copies, rebinding and branch joins preserve restrictions | `copied-alias`, `reassigned-alias`, `branch-value`, `branch-assignment`; each isolates one exact permission error. |
| Loop headers/exits include continuing states | `loop-zero`, `loop-continue`, `loop-break` cover zero iterations, backedges/continue and break states. |
| Fresh rebinding restores writable access; returning paths do not taint continuing paths | `fresh-rebinding` executes both branch paths, mutates only its fresh object, and leaves the original unchanged. |
| Owning an identity/getter/overlapping result does not upgrade permission | `identity-result`, `receiver-result`, `overlapping-result`. The identity case also exercises an implicit success projection. |
| A disjoint result type can be writable | `disjoint-result` mutates a new summary holding only copied strings/numbers, leaving its read-only source unchanged. |
| Literals with read-only object initializers are wholly read-only | `literal-other-field` and `literal-whole-value` reject writes through the retained object and to an unrelated note field. |
| Copied primitive/immutable fields can be stored | `copied-fields` stores a read-only source's name/age, then independently replaces the target string and increments its age. |
| Mutable symbol access reaches object payloads | `mutable-symbol` mutates the original user through a `&Choice` payload; a no-payload alternative also runs. |
| Symbol copies, residual bindings and handlers retain permission | `symbol-copy`, `symbol-residual`, `readonly-propagate-payload`, `readonly-panic-payload`. |
| Object-bearing symbols cannot be forwarded/stored through ordinary access, and parameter bindings stay immutable | `symbol-forward`, `symbol-store`, `symbol-binding`. |
| Error propagation preserves permission and ownership | `propagated-error-permission` rejects a write through a forwarded borrowed error payload. `propagated-error-owner` releases the creating scopes, then observes and mutates the independently owned failure payload; code after `?` is skipped. |
| Immutable/value-only symbols are unrestricted | `immutable-symbol-payloads` forwards an ordinary text-bearing symbol into a writable field and reads the heap string. |
| Reference reads pin arguments | `pinned-arguments` overwrites the first argument's owner during evaluation of a later argument, then in a callee. Both original snapshots remain readable. |
| Owned returns / retain-before-release stores | `owned-return-self-store` self-assigns object and string fields, returns a child after its holder's scope ends, self-assigns an owned local, and mutates the returned user. |
| Scope/return/break/continue/loop-slot cleanup | `scope-and-jump-cleanup` allocates objects/strings on continue, early-return, fallthrough and break paths. Debug/sanitizer runs require clean exit. |

These native observations do **not** measure exact transient reference counts or
prove immediate release at every last use. The compiler intentionally keeps
lexical owners and conservative snapshots. Existing `test_ownership` separately
asserts read pinning, retain-before-release, borrowed-parameter/owned-return
behavior and idempotence on IR; `test_objects` checks runtime counts; and
`test_structs`, `test_symbols` and `test_errors` independently check permissions,
repeat analysis, lowering rejection and ownership integration. Those stage tests
remain separate. No compiler/runtime changes were needed for this slice.
