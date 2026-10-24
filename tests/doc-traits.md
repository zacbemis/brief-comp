# First-version scope audit: `lang-traits.txt`

`test_doc_traits.cmake` classifies **all six fences**. **Five (1–5) have
explicitly partial coverage**, represented with derived nominal-type scenarios
in **16 native/diagnostic cases**. Fence 6 is deferred. No unchanged trait
program is claimed executable: the fixtures use only ordinary local structs and
`impl` blocks, with methods explicitly installed for each concrete type.

## Fence classification

| Fence | Scope and evidence |
|---|---|
| 1 | **Partial:** replace the trait header with ordinary `impl User` or `impl Admin`, remove exactly the abstract `getUser(value: Self): User` declaration, and retain all three complete method bodies and their `Self` annotations. Supply concrete getters from fences 2/3. Exercise names, ages, both classification tags and eight integer ages including 99/100 and full `int` endpoints. Removing either comparison arm requires the exact nonexhaustive-match diagnostic. Required-method checking, trait `Self`, inherited defaults and shared trait code generation are not tested. |
| 2 | **Partial:** replace only `impl User: HasUser` with ordinary `impl User`, retaining the identity getter's signature/body. Keep its owned result alive after its creator returns; reject mutation through a result when the input originates in an ordinary read-only parameter. This is not conformance checking or automatic default provisioning. |
| 3 | **Partial:** retain the complete `Admin` declaration and getter body; remove only conformance from its impl header. Read the nested User after Admin's creator/owner is gone, project it explicitly to a User parameter, and observe shared updates/snapshots. Manually copied ordinary methods supply the omitted call context, not trait forwarding. |
| 4 | **Partial:** observe every complete original call expression with `print(...)` for names and `print(str(...))` for ages. User and Admin have distinct age inputs (20/100). Their methods are explicitly installed, so these calls do not prove the preceding claim that writing only `getUser` acquires defaults. |
| 5 | **Partial:** replace the conformance header and remove `ovr`, keeping both method signatures/bodies. Explicitly omit the earlier ordinary Admin `getName` to avoid duplicate methods; retain the other two transplanted bodies. Observe `Admin: Bob`, base User name, age and range, and an allocated custom name after its creator returns. This is a single ordinary implementation, not override selection or dispatch from a trait default. |
| 6 | **Deferred:** proposed `Collection<T>`, `Vec<T>` and `impl<T: HasUser>` with trait constraints. The proposal is not a complete function body or a first-version generic algorithm. |

## Omitted contexts and ownership

The chapter omits User's definition; a local two-field `User { name: str, age:
int }` supplies that context. Factories allocate names and create Users/Admins
with ordinary struct literals. Admin's original `user` and integer `tier` fields
are unchanged. No modules, publication/privacy, inheritance, vectors or trait
runtime representation are supplied by this scaffolding.

For the default-body fixtures, the same source methods are copied explicitly
into both ordinary impls. `Self` therefore has its existing enclosing-struct
meaning; there is no trait instantiation or constraint solver. For the custom
Admin fixture, the `getUser` comes from fence 5, the two non-name methods come
from fence 1, and only the default name body is excluded. The exact exclusion
and every original method inventory (including the abstract signature) are
guarded, as is the four-expression call inventory.

The identity/projection cases exercise owned returns from borrowed parameters,
including nested User storage after Admin cleanup. Forwarded names retain the
stored string independently; the custom name allocates concatenated text. A
shared-update scenario preserves the old name snapshot, mutates through a
projected User from a writable Admin, and observes the update through the
original User, Admin and projected alias. No identity builtin, clone or exact
transient ARC count is asserted.

Permissions are caller-sensitive, not a blanket rule that all getter results
are read-only. A result that aliases an ordinary read-only User/Admin parameter
cannot upgrade its input permission. Conversely, a writable local argument can
produce a writable alias, as the shared-update case demonstrates. Neither
ownership nor `Self` constitutes trait purity, deep const or exclusivity.
Normal and sanitized debug runs check behavior, active ownership and clean exit
counts without changing the independent permission/IR/runtime tests.

## Derived nominal-type and forwarding checks

Two explicitly derived cases reject passing/binding Admin as User; explicit
`admin.getUser()` projection succeeds. This checks distinct first-version
nominal types and concrete composition, not generic/trait substitutability.
Another fixture leaves an ordinary Admin impl with only `getUser`: calling its
missing `getName` requires `Unknown method for this type.`. This deliberately
does **not** expect a trait conformance/required-method diagnostic. It shows why
plain struct composition alone is not evidence of automatic default methods.

## Deferred design and remaining finish work

Trait requirements, default acquisition, conformance, default-to-override
resolution, `ovr`, conflict/coherence rules and generic constraints remain
unimplemented. The chapter itself says conflicts/coherence require further
specification; the audit guards that qualifier rather than inventing a rule.
Its no-inheritance and generic non-interchangeability statements are scoped
separately from current nominal-type evidence.

Fence counts, all method/call inventories, unique adapters, generic-fence
subjects, selected scope prose, valid unique case names and the 16-case total
are guarded. No language-reference, compiler or runtime changes were needed.
Every tracked chapter now has a scope/acceptance audit, but this does **not**
complete M6: the diagnostic gaps and final README/status verification in
`doc-acceptance.md` still need finish work.
