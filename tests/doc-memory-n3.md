# N3 scoped native audit: memory, borrowing and resources

Base: `27d7eb1`, committed unchanged. `test_doc_memory_n3.cmake` uses unchanged
`doc_native.cmake` helpers. All **18 fences** are hash/count guarded and
classified once: four whole fences execute, fourteen remain explicit listings.
Ten independent complete prose programs exercise available rules. No compiler,
permission, ownership, runtime or shared audit helper change is made.

```sh
ctest --test-dir build -L doc-memory --output-on-failure
```

Normal/sanitized cases check exact stdout/stderr/status, diagnostic messages and
source lines, with no guessed fixes. Sources, cases, inventory and logs live in
`build/tests/current-doc-memory{,_sanitized}/`.

## Every fence

| Fences | Disposition |
|---|---|
| 1, 12 | Context: User/constructor missing; no stand-in User API. |
| 2 | Run: complete primitive copy body, followed by observers of a and b. |
| 3, 15 | Later: clone is unavailable, including const cloning. |
| 4 | Fragment: interleaved definition/usage, missing User; never split. |
| 5 | Context: missing collection storage API and types. |
| 6 | Run: complete User struct; observer allocates and shares one object. |
| 7 | Run: complete recursive Node type; observer allocates an acyclic chain. |
| 8, 9 | Context: User type absent; no supplied type to complete the fence. |
| 10 | Run: complete User struct/method; observer calls its mutable self. |
| 11 | Fragment: duplicate alternatives/missing types; no extracted attach. |
| 13, 14, 16, 17 | Later: const rules and missing APIs are outside scope. |
| 18 | Context: real File/open/readAll/close APIs do not exist. |

All raw fence bytes remain unchanged. Observers use only the fields/methods
actually declared in the complete fences, not injected constructors. Fence 2
prints 10 twice; fences 6/10 observe mutation through shared references. Fence 7
executes recursive type layout and acyclic cleanup only: it neither forms a
cycle nor demonstrates a cycle collector. Fence 10 checks writable self; it
is not fabricated native acceptance of the incomplete fences 8/9.

## Independent available-rule coverage

Five executable prose programs cover:

- Shared struct/string aliases, field self-assignment and a snapshot of a field
  argument retained while a later argument replaces that field. Old aliases
  remain alive and mutation through a writable alias is observed.
- Owned outcomes with struct payloads on both success and error routes. Stored
  propagation retains the active payload across returning-scope cleanup;
  returning/owning a reference does not upgrade the caller's permission.
- Lexical exit, return and continue cleanup, reused loop slots and extracted
  aliases retained across outcome replacement. Native debug accounting and
  ASan/UBSan exercise release behavior without new hand-written retain/release.
- Writable `&` parameters, field storage, success projections and narrowed
  error catch-alls preserve writable access to reachable objects.
- Copied primitives/value-only symbols and a structurally disjoint call result
  remain writable, even when built using a read-only argument's string field.

Five negative prose programs require **20 exact diagnostic sites**:

- Read-only permissions persist through delayed `!`, `?`, match payloads,
  narrowed catch-alls, rearmed slots and ordinary result forwarding.
- Storing direct/call/literal aliases in writable fields or forwarding them to
  `&` parameters rejects; symbol parameters with reachable objects obey the
  same rule. ARC retention does not change permissions.
- Both ordinary and `&` parameter bindings remain immutable.
- Joins include read-only continuing paths, possible zero iterations and
  backedges/continues. Returning paths do not restrict continuing paths.
- Call results, wrapper literals and pipelines cannot upgrade read-only access
  merely by being owned, copied, stored locally or projected from an outcome.

There are **14 runner cases**: nine executions and five rejection programs.
The audit makes no promise about count-elimination optimizations, cycles/ORC,
weak references, concurrency, cloning, const, arrays/vectors or real external
resource lifetimes. The resource fence stays a context listing. Independent
outcome diagnostics establish mandatory handling, not a File implementation or
an implicit resource-cleanup facility.

## Expectations and validation

**Changed existing expectations: none.** No old fixture/input/runtime marker,
golden, `.expect`, language chapter, parser classification or generated audit
program changes. These cases use independent Box/Holder functions, not repaired
User/File/collection fragments. All new memory cases passed their first run.

See [the N3 closure review](next-version-n3-review.md) for the full matrix,
retained-source comparison, strict native checks and remaining scope boundaries.
