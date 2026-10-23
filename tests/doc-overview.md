# M6 acceptance slice: `docs/lang-desc.txt`

The overview contains **zero code fences**. `test_doc_overview.cmake` guards that
count and checks **24 representative first-version prose-contract cases**.
These are test scenarios assembled from the stated responsibilities, not
original programs extracted from the overview or new language-reference examples.
The chapter audits remain the detailed evidence for individual language rules.

```sh
ctest --test-dir build -L doc-overview --output-on-failure
```

The complete eleven-bullet “First version” scope and its later-version paragraph
are exact source anchors. Scope edits therefore require an audit review. The
script also checks the overview's thirteen document-table links for existence
and uniqueness and guards the eleven-item open-question inventory. A link check
is not acceptance of the linked document's contents. Remaining chapter audits
and diagnostic improvements are still M6 work.

Normal and sanitized runs use the shared C runner, exact output/status and
message/source-line expectations, source-located panics, sanitizer checks and
clean debug exits. Generated cases live under
`build/tests/test_doc_overview-fixtures/programs/`, separately from the sanitized
variant and the checked-in program fixtures. Unique names and the expected case
count remain guarded. The shared harness now handles zero fences without CMake's
`RANGE 1 0` iteration; three additional regression guards check invalid fence
indices, invalid classifications and successful passage to runner invocation
for a valid prose-only manifest. There are now fourteen harness failure guards.

## Overview classification

| Section | First-version acceptance or explicit boundary |
|---|---|
| Status / 1. Overview | Experimental goals, not an assertion that every listed characteristic exists. Native typing, structs, permissions, symbols, pipelines and ARC have representative checks below. Modules, traits, `const`, cloning, collections, resource cleanup and cycle collection are not established. |
| 2. Design principles | Qualitative goals, not independent performance or API guarantees. Observable aliasing is checked; no manual retain/release is introduced. Const, privacy, traits and resource behavior remain deferred rather than simulated. |
| 3. Documents | All thirteen distinct table references must name existing document files. Their acceptance status stays in the all-document tracker. |
| 4. Type checking | Primitive/nominal inference, argument/binding types and typed symbol compatibility are exercised. Generic constraints and trait verification remain deferred. |
| 4. Control-flow validation | Exhaustive handlers, propagation, mandatory handling, implicit/written/partial returns and invalid contracts have representative positive/negative checks. |
| 4. Memory safety | Automatic ARC, owned results, pinned strings and ordinary versus writable access are exercised. This is not exclusivity, deep const, exact count elimination, or cycle collection. |
| 4. Encapsulation | Complete struct initialization is checked. Modules/privacy, traits and overrides remain deferred. Local associated constructors do not establish privacy or module finding. |
| Compilation strategy | The normal native runner goes through Brief-to-C and the configured C compiler. Independently emitted C is also validated under strict GCC/Clang warnings. Bootstrapping is a long-term goal, not acceptance work for this version. |
| First version | The exact scope block is guarded and each of its eleven bullets maps to cases below; single-source scenarios do not claim multi-file/module support. |
| 5. Open questions | All eleven areas remain design questions: const/call aliases, cloning depth, trait coherence, weak references, resource cleanup, concurrency, ABI/FFI, inferred tag spelling, external impls, nested modules, and custom conversions. No final semantics are invented or promised here. The inventory count and the statement that these are not commitments are guarded. |
| 6. Agent guidance | Design constraints, not additional first-version features. Ordinary alias/permission and handled-call rules are tested; ORC, cloning, deep const, declarative collections, privacy and resource cleanup are explicitly outside these scenarios. |
| Long-term vision | Qualitative goals, not benchmark, safety-proof, compatibility or completed-feature claims. |

## First-version scope to case mapping

| Scope bullet | Representative checks |
|---|---|
| Entry point | `entry-owned-result`, `entry-failure`, `missing-entry`, `entry-parameters`. Ordinary owned results are discarded; propagated entry errors report/release their payload. Exact entry-result semantics are defined in the function/error chapters, not invented by this overview test. |
| Primitive types | `primitives-and-control` observes all eight numeric types, bool/chr/string values, contextual literals and explicit conversions. Unit is also exercised by the written-unit method and handler results. |
| `let` / `var` / contextual literals | Written primitive annotations, inferred locals, wide literal/arithmetic contexts and updates run; `binding-type` and `immutable-binding` check invalid forms. |
| Arithmetic / comparison / logic | `primitives-and-control`, `arithmetic-overflow`, `arithmetic-zero`; integer panics must identify their Brief operation's source line in both native configurations. |
| Functions / return types | Inferred `twice`/`make`, written recursive `factorial`/constructor/method/source, partial `double`, plus `parameter-type` and `return-contract`. |
| Control flow | Value `if`, range `for`, `while`, continue/break and updates run. `jump-context` and `conditional-value` reject invalid use. |
| Structs / methods / `&` | `aliases-and-ownership` checks constructor/receiver calls, shared mutation and an independently owned string snapshot. `ordinary-access`, `mutable-parameter-binding`, `mutable-forwarding`, `retained-access`, `incomplete-object` check specific permission/initialization errors. |
| Symbols / handlers / inference | `outcomes-and-inference` uses named failure/result sets, inferred failures under a written success contract, `?`, `!`, exhaustive tag/group arms and both outcomes. `symbol-compatibility`, `incomplete-match`, `unhandled-outcome` and `propagation-contract` check exact errors. |
| Pipeline calls | Primitive and fallible-result calls run as stages before conversion/output or `?`; handlers remain separate from the pipeline transfer. |
| Builtins | `print`, `str`, numeric widening and explicit `chr`-to-number conversion make values observable. Detailed domains/grammar/formatting stay covered by the type/conversion audits. |
| ARC without cycle collection | Heap-backed strings, aliases, field replacement, retained snapshots, ordinary/failed returns and entry cleanup run with clean debug ownership exits. No cyclic runtime graph or collector is synthesized. |

## Scenario details and fidelity

`Box` and `Holder` are deterministic local nominal types. The associated
constructor creates an ordinary object with a heap-backed name. `make` returns an
owned object; a copied alias mutates it via `&`, while a getter's previous string
result remains valid after field replacement. Content equality observes the
shared state without adding an identity operation or `.clone()` implementation.
The written-unit method mutates reachable data, not its immutable parameter
binding. Its negative counterparts distinguish those permissions explicitly.

`source` supplies a written `Result`, returning either an integer or a heap-backed
`:bad(str)`. The partially annotated `double` infers that failure through `?`.
The observer exercises success and the named failure group; `!` is exercised on
success. The fallible entry case checks exact stderr and exit 1, including payload
cleanup. This is a local outcome API, not a filesystem/JSON/module substitute.

The seventeen compile-error cases each require the intended error, not an
arbitrary rejection. The missing-entry case places its helper declaration on the
first source line because that diagnostic is attached to the whole-program span
at byte zero. Every other diagnostic is annotated at its actual offending source
line. No existing runtime or compiler diagnostic behavior needed changing.

These scenarios do not replace the independently testable resolver, checker,
lowering, ownership insertion, emitter, runtime and native driver tests. They do
not measure count elimination, allocation frequency, prompt last-use release,
stack safety, resource reclamation, or performance. Deferred features are not
made milestone blockers by the overview's broad aspirational text. No compiler
or runtime changes were needed for this slice.
