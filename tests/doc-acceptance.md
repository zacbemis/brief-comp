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
| `lang-syntax.txt` | [Syntax audit](doc-syntax.md): 33 fences classified, 30 represented in 65 cases, three collection-only exclusions. Fences 3, 4, 6, 17, 26, 32 and 33 are partial. Inline/prose cases check rejected expression updates/assignments, scope, continuations, control flow and range rules. Named-field header literals now receive an actionable parentheses hint; parser acceptance remains separate. |
| `lang-types.txt` | [Type audit](doc-types.md): 24 fences classified, 15 represented in 64 cases, nine deferred feature/library exclusions. Equality fences 20/21 are partial (identity calls deferred). Guarded prose cases cover primitive/literal domains, arithmetic panics, numeric/text conversions, IEEE behavior, binary strings, unit and nominal equality. The audit added `chr`-to-number conversion to the chapter and compiler and a specific recursive-equality error; a later lexer slice gives source-exponent notation its own diagnostic while preserving exponent text conversions. |
| `lang-functions.txt` | [Function audit](doc-functions.md): 27 fences classified, 20 represented in 33 cases (fences 9 and 27 are partial), seven later-version exclusions. Selected parameter, inference and evaluation-order prose rules are also checked; a partial-return diagnostic cascade is fixed. |
| `lang-symbols.txt` | [Symbol audit](doc-symbols.md): ten fences classified, nine represented in 32 cases, one module-qualified type exclusion. Guarded prose cases cover local composition, transparent/recursive aliases and exact/contextual payload types; payload-free ordering prose remains deferred. |
| `lang-memory.txt` | [Memory audit](doc-memory.md): 17 fences classified, ten represented in 48 cases, seven later-version/API exclusions. Fences 4, 6 and 7 are partial: exact count-elimination and collector claims are not tested. Guarded prose cases check access-path/flow permissions, symbol handlers, pinning and owned cleanup. |
| `lang-ex-base.txt` | [Raw sample audit](doc-base.md): zero fences; all source consumed into 38 contiguous classified spans (20 tested, six partial, four references, eight deferred), exercised in 38 cases. Exact subfragment/method inventories guard localized first-version bodies; modules/traits/const/JSON/vector/clone claims remain explicitly partial or deferred. |
| `lang-stdlib.txt` | [Standard-library audit](doc-stdlib.md): one explicitly partial JSON fence, represented in 17 cases; no builtin examples occur in this chapter. Preserve constructor/propagation bodies with a local field-access stand-in and error alias; module privacy, JSON representation/serialization and finalized API contracts remain deferred. Derived negatives pin error compatibility, handling and coverage; a later checker slice suppresses the unhandled-getter argument-type cascade without relaxing direct handling. |
| `lang-modules.txt` | [Modules-chapter audit](doc-modules.md): 17 fences classified, eight represented in 30 cases; 3, 4 and 15 partial, nine deferred. Local structs/literals, explicit receivers/`Self`, constructors and entry behavior run in one file; a deterministic `File.read` models selected outcomes, not resource access. Module discovery/imports/paths, privacy, ambiguity/misspelling diagnostics, traits/constants and dependency cycles remain deferred. |
| `lang-collections.txt` | [Collections scope audit](doc-collections.md): 31 fences classified; only 7, 11, 22 and 29 partially represented, in 14 cases including prose/derived scenarios. Original handlers and the written manual `while` expansion use controlled outcomes/a synthetic concrete cursor. All 26 table operations and 27 whole fences remain deferred; no vectors, lambdas, queries, traits, generic cursor lowering or iteration-invalidation rules are implemented. Corrected delayed `!` handling and qualified uppercase byte-order prose as ASCII. |
| `lang-traits.txt` | [Traits scope audit](doc-traits.md): six fences classified; 1–5 explicitly partial, represented with derived nominal checks in 16 cases; generic fence 6 deferred. Original concrete getters/default bodies and custom-name body run only as manually installed ordinary impl methods. Boundary coverage, explicit composition, caller-sensitive permissions and owned lifetimes are checked; trait requirements/default acquisition, conformance/overrides, conflicts/coherence and generics remain deferred. |

Run all current native document acceptance tests and their support guards:

```sh
ctest --test-dir build -L doc-acceptance --output-on-failure
```

The `doc-errors`, `doc-pipelines`, `doc-functions`, `doc-symbols`, `doc-memory`,
`doc-syntax`, `doc-types`, `doc-overview`, `doc-base`, `doc-stdlib`,
`doc-modules`, `doc-collections` and `doc-traits` labels select individual
audits. Each has normal and, when enabled, sanitized runs, using the C program
runner without interpreter or shell dependencies. Generated cases live in
separate build-tree directories per test/configuration; they are not included
in the 87 checked-in program-fixture count.

## Remaining M6 finish

All 13 tracked chapters have explicit acceptance/scope audits and the initial
diagnostic inventory is addressed. The final implementation/README/status review
has started, **not finished**: its [first checkpoint](first-version-review.md)
found and fixed silent acceptance of recursive member-lookup and contextual
literal errors. Continue the remaining in-scope paths and chapter-adapter/status
review before deciding M6 completion. Partial and deferred claims must stay
explicit throughout.

## Completed diagnostic improvements

- File-level statement-shaped input now reports `Statements are only allowed
  inside a function.` at the statement's first token, rather than treating a
  call as a malformed parameter list or listing unsupported `const` as advice.
  Non-consuming lookahead distinguishes closed calls from declaration evidence;
  typed parameters, explicit `pub`, and following bodies/return annotations keep
  their specific declaration diagnostics. Ambiguous struct/literal syntax still
  follows declaration parsing. A generic name such as `Box<T>` also keeps the
  declaration path. Remaining generic errors say `Expected a declaration
  (function, struct, named type, or impl).` (or `Expected a method
  declaration.` within an impl), no longer listing `const`; global `const`
  still reaches its existing unimplemented-feature check.
  Parser regressions cover exact counts/spans, recovery, truncated/malformed
  headers, lexer-error suppression and repeatable token-preserving lookahead.
  CRLF CLI checks and `tests/programs/errors/top-level-statements.brf` require
  exact messages/locations and rejection without an executable. `doc-base` and
  `doc-modules` now pin the improved hint.
- Permission diagnostics now say `A read-only value cannot be passed to an &
  parameter; take the parameter it comes from as &.` and `A read-only value
  cannot be stored in a field; take the parameter it comes from as &.`. The
  argument/parameter roles are correct, both messages give the same fix, and
  the store message covers structs and object-bearing symbols without calling
  a symbol a struct. Permissions, fixpoint rules and diagnostic spans are
  unchanged. Eighteen exact-span semantic cases cover direct/receiver/pipeline
  calls, aliases, symbol construction/narrowing, stores and loop backedges;
  each has a valid `&`-source counterpart. Independent permission reruns and
  full rechecks report once and keep invalid programs out of lowering.
  Copied scalars/strings, immutable-only symbols, fresh rebinding and unrelated
  argument-type errors retain their prior behavior. Updated native/document
  expectations and an immutable-symbol ownership case cover the integration.
- Expression diagnostics now suggest `and`/`or` for contiguous `&&`/`||` and
  require postfix standalone updates for prefix `++`/`--`. Logical hints cover
  the full two-byte operator; whitespace, comments or removed newlines cannot
  manufacture a pair. Lexer tokens, valid single `&`/`|`, mutable parameter
  annotations, keyword logical operators and postfix update rules are unchanged.
  Parser cases assert exact messages/spans in LF and CRLF expression contexts,
  recovery of later items/declarations, unchanged tokens on reparse and no
  duplicate lexer diagnostics. CLI checks require all four hints at exact CRLF
  columns with no AST, while lexer-only dumping still succeeds. Native and
  `doc-syntax` expectations pin rejection; positive native checks preserve
  short-circuiting, RHS effects, single bitwise operators and postfix updates.
- Exponent-shaped suffixes on source integer/fraction literals now report
  `Exponent notation is not supported in source number literals.` as one
  source-backed lexer error, rather than splitting into a number and identifier
  and later reporting a separator error. Both `e`/`E`, optional signs, and
  unfinished markers/signs at a lexical boundary are covered. Identifier-like
  suffixes (`1else`, `1e2name`) and separated tokens keep their prior lexing;
  member/range punctuation and following line endings are not consumed.
  Streaming/bulk lexer tests and LF/CRLF parser recovery checks assert exact
  lexemes/spans, retained later items/declarations and no duplicate diagnostics,
  including negative patterns. CLI token/AST/C-emission commands report the
  same single error with no output. Existing native and type-audit fixtures
  pin rejection; numeric text exponents, ordinary literals and ranges remain
  supported exactly as before. No exponent syntax is added to source literals.
- Unhandled fallible calls still require `?`, `!` or a direct match, but their
  invalid results no longer masquerade as usable full outcomes in dependent
  bindings/arguments. SCC probes defer those results as unknown; the final
  check reports the required handling error and supplies an error recovery
  type. This avoids poisoning inference in a way that could erase fallibility
  and lose the primary diagnostic. Callee contracts and valid handler semantics
  are unchanged; no success projection or implicit propagation is inserted.
  `doc-stdlib` now requires only the primary error for each missing getter `?`.
  Semantic regressions cover binding/argument/field/handler contexts, written
  and partial returns, converters, recursive components, repeated checking and
  rejected lowering, with correctly handled counterparts. Independent sibling
  type/arity/return/permission errors, multiple unhandled calls, data-only
  symbols and success-only calls remain checked normally. Full validation also
  exposed a formerly misclassified recursive conversion test: `int(g())?` left
  the fallible `g()` unhandled, but error-typed SCC outcomes hid the diagnostic.
  It is now a negative; `int(g()?)?` is the positive counterpart, with verified
  non-error outcomes, lowering/emission and native propagation coverage.

- Named-field struct literals in forbidden `if`/`while` conditions or `for`
  iterables now report `Wrap struct literals in conditions or iterables in
  parentheses.` at the opening brace, rather than a pair of newline-separator
  errors. A non-consuming field-prefix check preserves ordinary/empty blocks
  and the existing grammar, including literals inside grouping, calls and
  indexes. Exact LF/CRLF parser tests cover recovery, unchanged tokens/reparse,
  nested generic closers, EOF and independent lexer/parser errors. CLI AST/C
  commands require exact locations/highlights and no output; token dumping
  stays lexer-only. The syntax audit retains its original negative without the
  old newline adapter; native fixtures check all three headers and valid
  parenthesized condition/range-bound ownership. Counts remain 450 generated
  cases and 87 checked-in fixtures; no runtime or language feature is added.

- Unknown dotted methods keep their primary error and never fall back to
  top-level functions. When a visible same-name user function has matching
  arity and an assignable, known first-parameter type, a declaration note now
  gives the direct/pipeline call forms using that function's name. Locally
  shadowed functions, incompatible receivers/arity, builtins and error
  receivers do not get misleading advice. Other argument, permission and
  outcome rules still apply; this is call-form guidance, not a promise that
  the rewritten call is otherwise valid. Exact LF/CRLF stage and CLI
  regressions cover note spans, independent errors, repeated checks, blocked
  lowering, forward/inferred/SCC receivers, lexical scope and unchanged
  method/pipeline precedence. The raw sample audit still pins the primary
  rejection; stage/CLI tests pin the note.
  All 450 generated cases and 87 native fixtures remain separate and unchanged
  in count. No module loading, new call syntax or runtime behavior is added.

- Unsupported generic-call headers now retain their dedicated rejection but do
  not undergo ordinary callee lookup/signature checks. The documented
  `a<b, c>(d)` parse therefore no longer adds a local-not-callable error.
  Recovery uses a targetless call: value receivers and arguments still get
  VALUE/flow/permission checks and contribute actual nested-call edges; generic
  headers supply no target, signature, method fallback or callee edge.
  Twenty LF/CRLF contexts, repeated resolution of the same AST, immutable
  tokens, exact spans, SCC/return-flow controls and blocked lowering are
  covered.
  Independent receiver/argument/binding/return/permission errors and enclosing
  ordinary-call arity errors remain required. CRLF CLI guards isolate the
  unsupported error with a one-argument `check`, retain the original AST, and
  require no C output. This is diagnostic recovery, not generic support or
  reinterpretation as comparisons. Fence 33's deferred fragment remains outside
  native acceptance; all 450 generated cases and 87 checked-in fixtures retain
  their counts.

- The first final-review checkpoint found that speculative member-lookup and
  contextual literal errors could poison recursive anchors, erase their final
  diagnostics and leave error-typed functions marked checked. Expression-local
  probe recovery now defers locally diagnosed error results unless they contain
  an intrinsic inference conflict. Inherited errors without a local failure
  still propagate; final reporting never depends on these counters. Ten lookup
  and five numeric-domain contexts cover both declaration orders, LF/CRLF, exact
  spans, stable anchors and semantic rechecks. Self-recursion, advice notes,
  unanchored recursion and existing independent/intrinsic conflict diagnostics
  remain checked. Native fixtures preserve valid recursive ownership/number
  contexts and pin all discovered rejection forms. CLI `emit-c`/`run`/`build`
  reject before consulting CC or publishing a binary. The plan's stale status
  claims are corrected in the linked checkpoint. This is a correctness
  follow-up, not completion of the review; totals remain 450 generated cases and
  87 fixtures.

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

The initial diagnostic inventory has no open entries. The final review already
found the recursive probe-error correctness issue described above; that
follow-up is addressed, but the rest of the review remains open and may identify
further gaps. This does **not** mark M6 complete. Deferred features remain
deferred; rejecting their syntax is not evidence of native feature acceptance.
