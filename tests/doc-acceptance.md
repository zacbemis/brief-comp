# First-version document acceptance tracker

M6 is **in progress**. Its definition of done includes executable/diagnostic
acceptance for every in-scope document example, improved diagnostics, and an
accurate README. Parsing an example is not proof that it compiles or behaves as
described. The scope remains `docs/lang-desc.txt`, “First version”; deferred
modules, collections/lambdas, ordering and refutable/nested payload patterns are
not being added to that scope by these tests.

## Current audits

| Document | Native/diagnostic audit |
|---|---|
| `lang-errors.txt` | [M5 audit](doc-errors.md): 36 fences classified, 28 tested in 49 cases, eight later-version exclusions; entry-result prose has additional existing coverage. |
| `lang-pipelines.txt` | [First M6 slice](doc-pipelines.md): 17 fences classified; ten represented in 20 cases (fence 11 is partial), six later-version exclusions and one syntax reference. Selected inline syntax examples are also checked. |
| `lang-desc.txt` | [Overview audit](doc-overview.md): no code fences; 24 representative prose-contract cases, with the full eleven-bullet first-version/later-version scope guarded. Thirteen document-table links and eleven open-question entries are checked; broad design goals are explicitly distinguished from implemented behavior. The shared harness supports prose-only manifests, with three added regression guards (14 total). |
| `lang-syntax.txt` | [Syntax audit](doc-syntax.md): 33 fences classified, 30 represented in 65 cases, three collection-only exclusions. Fences 3, 4, 6, 17, 26, 32 and 33 are partial. Inline/prose cases check rejected expression updates/assignments, scope, continuations, control flow and range rules. Struct-condition parser messages still need a more actionable hint; parser acceptance remains separate. |
| `lang-types.txt` | [Type audit](doc-types.md): 24 fences classified, 15 represented in 64 cases, nine deferred feature/library exclusions. Equality fences 20/21 are partial (identity calls deferred). Guarded prose cases cover primitive/literal domains, arithmetic panics, numeric/text conversions, IEEE behavior, binary strings, unit and nominal equality. The audit added `chr`-to-number conversion to the chapter and compiler and a specific recursive-equality error; source-exponent errors still use a generic message. |
| `lang-functions.txt` | [Function audit](doc-functions.md): 27 fences classified, 20 represented in 33 cases (fences 9 and 27 are partial), seven later-version exclusions. Selected parameter, inference and evaluation-order prose rules are also checked; a partial-return diagnostic cascade is fixed. |
| `lang-symbols.txt` | [Symbol audit](doc-symbols.md): ten fences classified, nine represented in 32 cases, one module-qualified type exclusion. Guarded prose cases cover local composition, transparent/recursive aliases and exact/contextual payload types; payload-free ordering prose remains deferred. |
| `lang-memory.txt` | [Memory audit](doc-memory.md): 17 fences classified, ten represented in 48 cases, seven later-version/API exclusions. Fences 4, 6 and 7 are partial: exact count-elimination and collector claims are not tested. Guarded prose cases check access-path/flow permissions, symbol handlers, pinning and owned cleanup. |
| `lang-ex-base.txt` | Pending in-scope example audit. |
| `lang-stdlib.txt` | Pending scope classification and builtin example audit. |
| `lang-modules.txt` | Pending explicit scope classification; modules themselves are later-version work. |
| `lang-collections.txt` | Pending explicit scope classification; collections/lambdas themselves are later-version work. |
| `lang-traits.txt` | Pending explicit scope classification; traits themselves are later-version work. |

Run all current native document acceptance tests and their support guards:

```sh
ctest --test-dir build -L doc-acceptance --output-on-failure
```

The `doc-errors`, `doc-pipelines`, `doc-functions`, `doc-symbols`, `doc-memory`,
`doc-syntax`, `doc-types` and `doc-overview` labels select individual audits. Each
has normal and, when enabled, sanitized runs, using the C program runner without
interpreter or shell dependencies. Generated cases live in separate build-tree
directories per test/configuration; they are not included in the 86 checked-in
program-fixture count.

## Audit requirements

- Read examples from the documents instead of copying an unchecked approximation.
- Record every code fence as tested, explicitly deferred, or a syntax reference;
  name partial coverage and every omitted subfragment.
- Guard fence counts and adaptation anchors, reject duplicate generated names,
  and require expected case counts. The shared helper's failure paths are tested.
- Supply only first-version scaffolding. Explain omitted bodies and illustrative
  API stand-ins; do not imply future libraries or modules have been implemented.
- Exercise successful programs and documented compile errors. Require exact
  diagnostic messages/source lines, and check output, status and ownership under
  sanitizers where relevant.
- Keep stage-level tests and strict GCC/Clang emitted-C validation in addition to
  native document tests. Track diagnostic gaps rather than declaring M6 finished
  after adding a few audits.

## Diagnostic gaps

The audits pin current messages exactly, so improving one means updating the
cases that name it. Known gaps found so far:

| Input | Current diagnostic | Problem | Pinned by |
|---|---|---|---|
| `a && b`, `a \|\| b` | `Expected an expression.` | No hint to use `and`/`or`. | `doc-syntax` |
| `++x`, `--x` | `Expected an expression.` | No hint that updates are postfix statements. | `doc-syntax` |
| `if Guest { name: name }.isAllowed() {` | Two `Expected a newline between items.` | No hint to wrap the struct literal in `( )`. | `doc-syntax` |
| `check(a < b, c > (d))` | `Generic calls are not implemented yet.` then `A local binding is not callable.` | The second error is a misleading cascade. | Not pinned (deferred line) |
| `let value = 1e2` | `Expected a newline between items.` | Does not say source literals have no exponent syntax. | `doc-types` |
| Passing a read-only value to an `&` parameter | `A mutable argument cannot be borrowed through an ordinary parameter.` | Reads backwards: the argument is read-only and the parameter is `&`. | `doc-memory`, `doc-overview`, `tests/programs/errors` |
| Storing a read-only object-bearing symbol in a field | `A read-only struct cannot be stored in a field; …` | Says "struct" when the value is a symbol. | `doc-memory`, `tests/programs/errors` |
