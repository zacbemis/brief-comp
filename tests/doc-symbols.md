# M6 acceptance slice: `docs/lang-symbols.txt`

`test_doc_symbols.cmake` reads all **10 code fences** directly from the document.
Nine are represented in **32 native/diagnostic cases**. Fence 10 needs a
module-qualified type and is deferred; the adjacent payload-free ordering prose
is also explicitly outside this first-version audit. No fence is partly counted.
Additional guarded prose cases exercise local composition, aliases, recursive
type shapes, exact payload identity and contextual nested payloads.

```sh
ctest --test-dir build -L doc-symbols --output-on-failure
```

Normal and sanitizer-enabled variants use the shared document harness and C
program runner. They check exact stdout, status, diagnostic messages/source
lines, panic payload/location and sanitizer failures. Fence counts, fragment
boundaries, adaptation anchors, classifications, unique case names and the case
count are guarded. This is another M6 slice, not completion of the milestone.

## Fence-by-fence record

Case names refer to generated `.brf` files under
`build/tests/test_doc_symbols-fixtures/programs/`, with a separate directory for
the sanitized variant.

| Fence | Example | Check or classification |
|---|---|---|
| 1 | `Sym<:ok(User), :invalidAge(int)>` | `outcome-type`: use the original type fragment as a local alias and function contract; observe an owned user success and the integer failure. |
| 2 | `:ok(user)` / `:invalidAge(15)` | `constructions`: split the two original alternatives into function bodies with the fence-1 contract; match both actual outcomes and inspect their payloads. |
| 3 | `Sym<:found(User), :missing>` | `optional-type`: use the original type fragment in a local alias; match present/absent outcomes and allow a discarded ordinary-data call without `:ok`. |
| 4 | Classification set | `classification-type`: use the original type fragment as a return contract; execute all three variants and a discarded ordinary-data call. |
| 5 | Named `Tier`, `Account` and match | `tier-gold`: retain declarations and put local statements in `main`; the original prints `Gold`. `tier-bronze` and `tier-silver` change only the initializer tag to execute the other original arms. All code is tested; no ordering operation occurs in the fence. |
| 6 | `User.new(...)?` | `propagate-success` and `propagate-failure`: retain the original call, changing only the age for the derived failure case. Observe the projected user or forwarded error; failure skips the following print. `incompatible-propagation` requires the exact written-return-set error; `unhandled-call` removes only `?` and requires the handled-call diagnostic even for age 20. |
| 7 | `User.new(...)!` | `panic-success`: retain the original call and read the user. `panic-failure` changes only the age and checks `:invalidAge(-1)`, the original usage line in the generated source, nonzero exit and skipped following output. |
| 8 | Explicit outcome match | `match-success` / `match-failure`: retain the full match and exercise both arms, changing only the age for failure. `nonexhaustive` removes the error arm; `unreachable` adds a duplicate error arm. Both require exact coverage diagnostics in the discarded-value context. |
| 9 | Public `Error` alias | `error-alias`: retain the complete declaration, construct and inspect all three variants with owned text payloads, and allow an ordinary-data call to be discarded. The declaration is tested in a single file, not as a module export. |
| 10 | `Sym<:ok(User), Json.Error, :invalidAge(int)>` | Deferred: module-qualified type lookup/composition. `local-composition` separately checks the in-scope prose rule with the local fence-9 alias; it does not replace or count this fence as tested. |

## Scaffolding and adapters

`User` is a local nominal struct, with first-version associated `new` and receiver
`getName` functions. Its deterministic constructor rejects negative ages and
otherwise allocates a user with a heap-backed name. It is not a standard-library
API, module, JSON parser or validation specification beyond this test context.
It supplies the omitted declarations needed by the original examples without
changing their intended symbol/handler behavior.

Type-only fragments receive a local alias and function/body context. Fence 2's
two lines are alternatives, not a function that discards its success and always
returns the error. Fence 5's local statements move into `main` while its original
public declarations stay at file scope. Derived negative and alternative-path
cases use guarded single-anchor changes; none fail for missing APIs.

The sentence about payload-free tags sorting in listing order is **deferred**.
Matching every `Tier` arm does not prove that rule. Neither global tag IDs nor
canonical-set ordering is treated as language ordering, and no comparison result
is invented to satisfy the prose. Symbol ordering and module-qualified types
remain later-version work.

## Additional prose cases

| Rule | Cases |
|---|---|
| Named entries include a whole group | `local-composition`: the original local `Error` alias joins success and a separate integer error. A named-group arm handles all three included errors; the other two outcomes have distinct observations. |
| Transparent forward aliases / recursive shapes | `forward-recursive-shape`: forward aliases refer to another alias and a later struct; a differently listed structural symbol annotation accepts the same value. A struct-indirected recursive type constructs an **acyclic** two-node graph and reads the leaf, with leak-free debug cleanup. No cycle collector is claimed. |
| Direct alias/payload cycles are invalid | `cycle-alias` and `cycle-payload`: require the struct-indirection diagnostic on the offending declaration. |
| Disjoint sets may share a tag with different payloads | `disjoint-payloads`: independently match `:value(int)` and `:value(str)` in one program. `conflicting-payloads` then rejects their alias union with the exact collision diagnostic. |
| Absent payload differs from `()` | `absent-and-unit`: inspect both shapes and preserve a unit-payload initializer's observable effect. `wrong-shape-absent` and `wrong-shape-unit` reject crossed initializers; `conflicting-shapes` rejects combining the two sets. |
| Payload identity is exact | `typed-payload-mismatch`: an already-typed `int` cannot initialize `:number(lng)`. |
| Literals receive payload context | `numeric-context`: a literal outside `int` range receives the declared `lng` payload type and prints 4294967296. |
| Nested payloads widen only with a common written context | `nested-context`: a written nested set accepts different literal payload subsets and a typed subset value, with owned text and both alternatives observed. `inferred-nested-mismatch` rejects the otherwise analogous unannotated branch union. Nested matches use payload bindings followed by a second match, not deferred refutable/nested payload patterns. |

Existing `test_symbols` separately checks resolution/checking, canonical type
identity, repeat analysis, invalid-program lowering rejection, ownership insertion
and its idempotence, emitted-C prerequisites, large alias/SCC graphs and an
independent coverage oracle. Those stage tests remain separate from these native
document cases. No compiler/runtime changes were needed for this slice.
