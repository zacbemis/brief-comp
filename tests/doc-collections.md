# First-version scope audit: `lang-collections.txt`

`test_doc_collections.cmake` classifies **all 31 fences**. **Four are explicitly
partial (7, 11, 22, 29), represented with two prose/derived scenarios in 14
native/diagnostic cases; 27 fences are deferred.** No fence is accepted as an
unchanged executable collection program. All 26 operations in the chapter's
three tables remain deferred; their complete name inventory is guarded.

## Fence classification

| Fence | Scope and evidence |
|---|---|
| 1 | **Deferred:** query pipelines over collections with predicate/key/selection lambdas. |
| 2 | **Deferred:** the corresponding collection receiver-call chain and lambdas. Existing pipeline/method tests do not prove these collection methods exist. |
| 3 | **Deferred:** `const`, generic `Vec`, retaining `add` and a frozen-value error. A conservative ordinary-parameter permission test would not establish `const`. |
| 4 | **Deferred:** sorted/offset/limited result paging. |
| 5 | **Deferred:** filtering and summing paid orders. Numeric addition alone is not this aggregate. |
| 6 | **Deferred:** collection `any` and its lambda inside a condition. |
| 7 | **Partial:** extract only the original `:ok(user) => :found(user)` / `:none => :missing` pipeline match. Feed a controlled typed outcome and observe both data-symbol results after the producer returns. Remove one arm to require exhaustiveness. `UserCollection`, its `userVec`, `find`, the predicate lambda and the complete `findByName` function are not implemented/tested. |
| 8 | **Deferred:** retaining mutable collection removal and returned `Vec<User>`. |
| 9 | **Deferred:** grouping, named-tuple rows and per-group counts. |
| 10 | **Deferred:** collection join, two-argument lambdas and named-tuple construction. |
| 11 | **Partial:** extract the original display match and run both `:ok(User)` and `:none` cases, observing `Bob` and `No users`. No `max`, element traversal or oldest-user selection is tested. |
| 12 | **Deferred:** predicate-driven mutation of every matching element through collection `update`. |
| 13 | **Deferred:** generic `Vec<User>` lookup and lambda, with propagated absence. The supported `?`/`:ok` syntax inside this future function has independent error/pipeline acceptance coverage; it is not this collection program's native acceptance. |
| 14 | **Deferred:** fallible `select` lambda, `Vec<Json>`/`Vec<User>`, JSON module errors and error accumulation through collection processing. |
| 15 | **Deferred:** success-only lambda/selection and direct `Vec<str>` result. |
| 16 | **Deferred:** unhandled failure inside a lambda. Rejecting an unsupported lambda would not prove the intended handling diagnostic. |
| 17 | **Deferred:** error-recovering lambda, JSON group, blank records, output positions and `Vec<User>` production. Ordinary match coverage does not establish these collection/lambda rules. |
| 18 | **Deferred:** collection iteration, collecting successes with `Vec`/`add`, and skipping failures. The source loop is not changed to a range and described as unchanged iteration. |
| 19 | **Deferred:** `where` result allocation and object identity/sharing between two vectors. Shared struct aliases are covered elsewhere, not a substitute for this query result. |
| 20 | **Deferred:** ascending/descending stable collection sorting. |
| 21 | **Deferred:** named-tuple lexicographic sort keys and independent field directions. |
| 22 | **Partial:** replace only the `users |> max(...)` query with a controlled fallible call; retain direct `!` handling and the age getter. Check age 20 and `:none` panic with source location. This does not test `max`, tie-breaking or selecting an actual oldest element. The reference's delayed-handling example was corrected; its prior shape is a separate first-version negative. |
| 23 | **Deferred:** grouped named-tuple/vector results and group-key order. |
| 24 | **Deferred:** `toMap`, map lookup and counting the vector inside the selected group. |
| 25 | **Deferred:** transitive `const` result restrictions and copied scalar aggregation. |
| 26 | **Deferred:** rejecting mutation of the collection being walked through the same path. Ordinary read-only permissions are not an iteration invalidation analysis. |
| 27 | **Deferred:** aliased mutation during iteration and runtime length-change detection. |
| 28 | **Deferred:** generic cursor protocol declaration `next(&Cursor<T>)`. The concrete fixture cursor below does not implement generic types or a trait. |
| 29 | **Partial:** remove the exact original collection `for` and `is compiled as` separator; run the original written-out `let cursor` / `while true` / `cursor.next()` match with a concrete synthetic cursor. Check empty/exhausted inputs and adapted `break`/`continue`/`ret` bodies. This is not evidence that the compiler lowers collection `for` to cursor calls. |
| 30 | **Deferred:** generic `Query<T>`, required/default trait methods, `Fn` and illustrative `...` bodies. |
| 31 | **Deferred:** trait conformance supplying inherited query operations and a lambda. |

## Explicit fixtures and observations

The local `User` has only `name`/`age` and ordinary getters. `input(bool)` returns
`Sym<:ok(User), :none>`; its successful object owns an allocated name. It does
not search a collection or compute a maximum. Fence 7's retained handler returns
`Sym<:found(User), :missing>`, whose active object payload must survive input
cleanup and retagging. These are data-only output symbols, not unhandled calls
with an `:ok` plus failures.

The concrete cursor has integer `index`/`end` fields. A local `Source` holds only
a size; its ordinary `cursor` method constructs that state. `next(&Cursor)` logs
each call and creates a fresh named `User` on demand, or returns `:none`. There
is **no vector, stored element sequence, shared collection element, generic
cursor, trait or collection operation**. The original manual expansion runs for
sizes zero and three. Derived body adapters stop at the second user with `break`
or `ret`, or skip it with `continue`; exact call/name traces distinguish their
behavior. A caller marker distinguishes returning from the enclosing function
from merely exiting the loop. Sanitized debug runs exercise active-payload and
loop/jump cleanup of the fresh users, strings and cursor. They do not establish
collector behavior, count elimination or mutation-while-walking protection.

A prose-only case checks `"Zoe" < "adam"`, `"Z" < "a"`, and the contrary UTF-8
case `"Éclair" < "adam"` (false). These are byte comparisons, not sorting. The
reference now says **ASCII** capital/lowercase letters; byte order is not Unicode
case ordering. Symbol listing-order comparison, numeric/tuple key support,
stability and `desc` remain deferred.

## Reference correction and guards

The original oldest-user example bound a fallible `max` call, then applied `!`
to the binding. The language's direct-handling rule forbids the first statement,
even when the value is handled later. The example now applies `!` directly to
the call, binds the resulting `User`, then calls `getAge`. A derived negative
reconstructs delayed handling with the local fallible `input` and requires the
exact mandatory-handling error on that call. The corrected path and panic are
both tested. No compiler/runtime changes were needed.

Fence counts, unique extraction/adaptation anchors, every deferred fence's
subject, the 26-operation inventory, selected rule/scope prose, valid unique
case names and the 14-case total are guarded. The omitted collection features
and their intended negative diagnostics are classified, not accepted by causing
unrelated unsupported-feature errors. Parser acceptance remains independent.
