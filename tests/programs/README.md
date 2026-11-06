# Program tests

Current inventory: **146 fixtures**. N1 retired the v0 generated document tests;
references to those audits in the milestone history below are historical. See
[document status](../doc-acceptance.md) for the current parser inventory and gap.

`../run_programs.c` compiles each `.brf` with `brief build`, runs successful
binaries, and checks the expectations below. CMake builds the runner alongside
the compiler. CTest runs normal and `--debug` (ASan/UBSan) variants; no interpreter
or third-party packages are required.

- `// expect: text`: one stdout line. Expectations are collected in source
  order, so put them together at the top when execution order differs from
  source order. `\0`, `\n`, `\t`, `\r`, and `\\` represent bytes; other text
  is UTF-8. An empty expectation represents a blank output line.
- `// stderr: text`: one exact stderr line, with the same byte escapes as
  `expect`. Use `// exit: 1` for an error-valued entry point. Cannot be combined
  with compile-error or panic expectations.
- `// error: message`: compilation must fail with this exact diagnostic on
  this source line. Every diagnostic must be listed; ordering does not matter.
  No executable may be published. Do not combine these with runtime expectations.
- `// fix: code`: follows an `// error:` marker on the same line, once for each
  fix that diagnostic offers. The diagnostic must print a `help:` line whose
  preview, without its indentation, is this code followed by nothing but the
  fixture's own comment. Every printed fix must be listed:

  ```
  true && false // error: Use 'and' instead of '&&'. // fix: true and false
  ```
- `// panic: message`: execution must fail and stderr must contain the message.
  Arithmetic panic fixtures under `panics/` exercise this in both normal and
  sanitized builds.
- `// exit: N`: exact program status, default 0 (or any nonzero status for a
  panic unless specified). Only one exit expectation is allowed.

Successful compilation must be silent; stdout must match exactly, and stderr
must be empty unless exact stderr or a panic is expected. Sanitizer reports always fail a test,
including reports after an expected compilation error. Compilations and programs
have 30-second and 10-second timeouts respectively. The shared POSIX process
helper captures binary stdout/stderr without pipe deadlocks, and kills the
process group on timeout. Each captured stream is limited to 16 MiB. All
executables live in disposable test directories.

Run manually, using a single C compiler executable:

```sh
./build/tests/run_programs --brief "$PWD/build/brief" --cc /usr/bin/cc \
  --programs tests/programs
```

`print` accepts only `str`; `str(number)` and `str(bool)` now make values
observable directly. Older numeric tests also use comparisons and control flow. `numbers.brf`,
`control-flow.brf`, and `returns.brf` cover contextual literals, all numeric
widths, evaluation order, loops, and recursive return inference.
`float-literals.brf` covers single-rounding and finite/underflow boundary cases;
negative fixtures reject updates in value contexts and preserve independent
inference diagnostics. The runtime's
checked helpers also have exhaustive small-width and boundary/panic unit tests.

M4 fixtures exercise heap strings and binary-safe comparisons; all numeric/bool
string conversions; nominal structs, methods and concrete owner types; shared
aliases and field replacement; target/initializer evaluation order; field
arguments invalidated by later arguments or mutable callees; owned returns and
discarded entry results; scope/loop/early-return cleanup; fieldwise equality
including NaN; generated-name collisions; and a panic with live heap objects.
Negative fixtures check fields, conversion boundaries, and mutation permissions.
Read-only alias fixtures cover local/field copies, reassignment, branch joins,
loop backedges/exits, and fresh rebinding without changing the caller's original
object. Call results whose type can reach a read-only argument, and literals
built from one, stay read-only; results that cannot hold it are writable.
`stored-references.brf` and its error fixture check that a read-only struct is
never stored in a field. Every executable fixture runs in normal and sanitizer
configurations, and normal debug exit reports remaining owners.

The first M5 slice adds `pipelines.brf`, `scalar-matches.brf`,
`integer-matches.brf`, `match-ownership.brf`, and two negative match fixtures.
They check method-first lookup, numeric argument contexts, input/callee/argument
order, interval overlap and boundaries, all shrt/ushrt values, signed zero,
NaN/infinities, match results and nonlocal jumps, and read-only propagation.
The next slice adds `symbols.brf`, `symbol-groups.brf`, `symbol-ownership.brf`
and two negative symbol fixtures (68 total fixtures). They cover data-symbol
payloads, aliases/composition, subset transfers, recursive set inference, group
matches and narrowed catch-alls, payload-aware equality including NaN, contextual
numeric payloads, active-payload ARC, argument snapshots, loop/jump cleanup and
read-only propagation through symbols. The outcome slice adds nine fixtures
(77 total): pure-success ABI erasure, explicit/implicit successes, recursive
error inference, partial annotations, stage handlers, early-return ownership,
propagation from initializers/conditions, argument pinning, exact entry error
formatting/status/cleanup, ordinary data-only symbol entry results (discarded with
exit 0), symbol-valued successful entry results, symbol panics
and their original source location, and exact handled-call/propagation errors.
The conversion slice adds six fixtures (83 total). `conversion-matrix.brf`
executes every numeric source/target pair and all eight text targets in one
program. The others cover exact integer narrowing, rounded floating conversions,
numeric/text range errors, contextual literal arguments (including pipeline
inputs), direct-precision floating parsing, original binary invalid payloads,
propagation/panic/entry handling, argument snapshots and loop/return cleanup.
The string-pattern slice adds `string-matches.brf`,
`string-match-ownership.brf` and `errors/string-matches.brf` (86 total). They cover
byte-exact literal/heap equality, empty strings and embedded NULs, prefixes,
escapes and byte-distinct Unicode spellings, input evaluation order, pinned
binding snapshots, owned results, return/break/continue/propagation cleanup, and
exact coverage/type/immutability diagnostics. Refutable/nested payload patterns
and symbol ordering remain deferred outside M5's first-version acceptance scope.

M5 is complete against its definition of done. The separate
[`lang-errors.txt` acceptance audit](../doc-errors.md) reads every code fence,
checks 28 in-scope fences in 49 generated native/diagnostic cases (normal and
sanitized), and explicitly excludes eight later-version module/collection
examples. These generated cases are separate from the 87 source fixtures here;
section 12's entry-result prose is also covered by the existing fixtures and
native/stage-level tests.

M6's acceptance/diagnostic work developed in the following slices. Its first
slice adds 20 generated pipeline/matching doc cases and shared acceptance
helpers with eleven failure-guard checks. The [acceptance
tracker](../doc-acceptance.md) and [pipeline audit](../doc-pipelines.md) record
exact scope, including partial coverage and later-version exclusions. The next
slice adds 33 [function-document cases](../doc-functions.md), covering return
typing, owned/unit results, call forms and selected prose rules, plus regression
coverage for a misleading partial-return payload-collision cascade. The symbol
slice adds 32 [symbol-document cases](../doc-symbols.md), covering
constructors/handlers, named sets, forward/recursive aliases, exact payload
identity and contextual payload widening, with module-qualified sets and
ordering explicitly deferred. The memory slice adds 48 [memory/permission
cases](../doc-memory.md), including exact access-path errors, flow joins,
symbol/handler permissions, argument snapshots, retained/returned owners and
scope/jump cleanup. Count elimination, collector claims, cloning, `const` and
real resource APIs have explicit partial/deferred classifications. The syntax
slice adds 65 [syntax-document cases](../doc-syntax.md), observing
operators/precedence, bindings, control flow and continuations, and checking
exact syntax/type errors. Deferred fragments are explicit; later diagnostic
slices remove the struct-condition newline adapter and the unsupported-generic
ordinary-callee cascade without adding those deferred features. The type slice
adds 64 [type-document cases](../doc-types.md): primitive/literal domains,
checked arithmetic, numeric/text conversion rules including `chr` sources, IEEE
behavior, binary strings, content equality and unit effects. Identity calls in
two equality fences and nine feature/library fences remain deferred. A later
lexer slice gives source exponents an explicit unsupported-notation diagnostic,
without changing numeric text conversion. The overview slice adds 24
[prose-contract scenarios](../doc-overview.md), not extracted programs: the
overview has no code fences. It guards first-version/deferred scope, checks the
document/open-question inventories, and exercises representative entry, typing,
control-flow, permission, outcome and ownership contracts. The shared harness
now handles prose-only manifests, with fourteen failure-guard checks. The raw
[running-sample audit](../doc-base.md) adds 38 cases from 38 completely
classified source spans, including preserved arithmetic/control flow and
localized User/Admin/retaining/handler bodies. Module, trait, const, JSON,
vector and clone limitations are explicit; adapted bodies are not acceptance of
those features. The [standard-library audit](../doc-stdlib.md) adds 17 cases for
its one partial JSON fence, preserving constructor/propagation bodies with
deterministic field access. It tests local error compatibility, handling,
ownership and coverage, not JSON parsing/serialization or module privacy. A
later checker slice removes the unhandled-getter argument-type cascade, without
weakening direct handling. The chapter contains no builtin examples. The
[modules-chapter audit](../doc-modules.md) adds 30 cases covering eight of its
17 fences and guarded prose contracts. It checks local structs/literals,
explicit receivers and `Self`, constructors and entry ownership/results in one
file. Nine fences are deferred; three represented fences are partial. Neither
local `File.read` outcomes nor local public declarations establish filesystem
APIs, module loading or cross-file privacy. A later parser slice gives top-level
calls/bindings a direct inside-a-function hint, pinned here and by the base
audit. The [collections scope audit](../doc-collections.md) classifies all 31
fences: only four are partial, represented with prose/derived checks in 14
cases. It runs original symbol handlers and the written manual `while` expansion
with controlled inputs, not vectors, queries, lambdas or compiler-generated
cursor iteration. All 26 table operations and 27 whole fences are deferred.
Direct `!` handling in the oldest-user example is corrected and its former
delayed shape is rejected; ASCII/UTF-8 byte comparisons are tested without
claiming sorting support. The [traits scope audit](../doc-traits.md) classifies
all six fences: five partial fences and derived scenarios run in 16 cases; the
generic example is deferred. Original getters/default/custom bodies run as
explicitly installed ordinary methods, with nominal composition, permission,
coverage and ownership checks. No trait conformance, inherited defaults,
override dispatch or generics are implemented by these fixtures.

`errors/top-level-statements.brf` brings the checked-in fixture count to 87,
requiring exact statement hints across calls, bindings, assignments, control
flow and literals, while preserving a malformed declaration's parameter error.
Parser/CRLF CLI regressions also check recovery, source columns,
token-preserving lookahead and lexer-only independence. No accepted language
feature is added.

A permission-diagnostic follow-up updates existing error fixtures to identify
read-only arguments passed to `&` parameters and read-only values (including
symbols) stored in fields. The rules are unchanged. Exact stage-level tests
cover argument/RHS spans, loop report counts, repeat checking and valid mutable
counterparts. `symbol-ownership.brf` also checks retaining an ordinary
immutable-only symbol and forwarding it to an `&` parameter with heap cleanup.

The operator-diagnostic follow-up extends `errors/update-values.brf` with
prefix update and C-style logical spelling errors. `control-flow.brf` checks
unchanged short-circuit/RHS effects, single bitwise operators and postfix
updates. Parser and CRLF CLI checks verify full operator spans, exact messages,
later-item recovery and unchanged lexer tokens; no new language syntax is
accepted.

The source-exponent diagnostic follow-up extends `errors/lex-and-parse.brf`
with integer/fraction, signed, unfinished, range-bound and pattern cases, all
requiring one source-backed lexical diagnostic per literal and no parser
cascade. `numeric-conversions.brf` preserves lower/uppercase signed text
exponents. Lexer, parser and CRLF CLI checks cover spans, boundaries, recovery
and independent stages. No source exponent feature or runtime change is added.

The handling-recovery follow-up extends `errors/handled-calls.brf`: invalid
getter results no longer cause dependent argument/field/method errors, while a
wrong sibling argument still fails. Stage tests cover unknown-versus-error
recovery during SCC/final checking, repeated checks, rejected lowering and
correctly handled counterparts, without treating forbidden outcomes as valid
values or suppressing independent errors. The 17-case standard-library audit
now pins only the primary handling error for each missing getter `?`. A
recursive conversion negative also guards against losing the primary error to
poisoned SCC outcomes; `numeric-conversions.brf` exercises the correctly
double-handled `int(recursiveForward()?)?` counterpart and observes its
propagated failure.

The struct-header diagnostic follow-up extends `errors/lex-and-parse.brf` with
`if`, `while` and `for` cases requiring an opening-brace parentheses hint rather
than generic recovery errors. `control-flow.brf` checks parenthesized literals
in conditions and integer range bounds, with ordinary cleanup. Parser and CRLF
CLI tests pin spans, preserved grammar/escape contexts, recovery, EOF, unchanged
tokens and independent errors. The 65-case syntax audit now uses its original
negative without a newline adapter; no struct-literal syntax is added.

The wrong-method guidance follow-up extends `errors/struct-fields.brf` with a
top-level-function-as-method rejection. Its primary error is unchanged;
compatible visible user functions now get a declaration note explaining
direct/pipeline forms. Stage/CLI tests pin note text/spans, scope/arity/type
exclusions, repeated checking and blocked lowering. Dotted calls do not gain
fallback, and actual methods still outrank pipeline top-level functions. The
raw-sample audit retains its primary rejection and existing direct/pipeline
execution cases.

The generic-call recovery follow-up extends `errors/unsupported.brf` with the
original ambiguous generic spelling and a generic pipeline stage. The dedicated
unsupported error remains, but those headers no longer resolve as ordinary
callees. Stage/CLI tests preserve argument VALUE/flow/permission checks, actual
nested-call dependencies, independent errors and enclosing ordinary arity rules.
Generics are not implemented or reinterpreted as comparisons; their diagnostic
rejection is separate from the 450 generated document-acceptance cases.

The first final-review follow-up extends `errors/recursive.brf` with member and
pipeline lookup errors plus a contextual literal overflow in anchored recursive
groups. These could lose their errors when speculative error types erased the
anchors. Expression-local recovery now defers diagnosed probe failures while
preserving intrinsic conflicts and independent final errors. `structs.brf` runs
valid recursive projections/method/pipeline calls, including an acyclic
symbol-indirected chain with heap strings and clean ownership exits;
`numeric-conversions.brf` checks a corrected recursive `shrt` context. Stage
tests pin anchors, order/line-ending variants and rechecks; CLI paths reject
before CC or publication.

The final boundary review extends `error-ownership.brf` with a pinned
symbol/string argument before a mutating/fallible later argument, plus
heap-owning loop conditions that succeed or propagate an owned failure across
body/continue cleanup. Exact callee traces distinguish success from skipped
calls, and debug runs require clean ownership exits. Independent native-driver
tests cover hard-link protection before CC, missing compiler output, compiler
and program signals, failed publication and semantic rejection before CC.
`structs.brf` also drops a 200,000-link chain built in a loop, which previously
exhausted the C stack through recursive release in debug runs.

These generated cases remained separate from the 87 source fixtures at M6
completion. Every tracked chapter has an audit, the initial diagnostic
inventory is addressed, and the [final review](../first-version-review.md) records completed
implementation/acceptance/README status verification. M6 is complete for the
first-version scope, without claiming the deferred surrounding features.

N1's first lexer slice adds five underscore-prefixed-name rejections to
`errors/lex-and-parse.brf`. Each must report one complete lexical error without
parser cascades; declaration grammar and keywords are otherwise unchanged.
That checkpoint retained 87 files.

N1's logical-negation slice converts the existing prefix expressions to `not`
without changing their expectations. `not.brf` adds direct observations of
boolean results, short-circuit side effects, newline continuation and combinations
with `?`, postfix `!` and `!=`; that checkpoint brought the inventory to 88
files. Five old prefix-`!` expressions in `errors/update-values.brf` must reject
with exact fix previews.

N1's double-precision slice converts the type and converter spelling to `dbl`,
keeping all existing outcomes and literal strings unchanged. `dbl.brf` observes
annotations, direct and pipeline conversions, text parsing, signed zero, NaN,
infinity and keyword boundaries. `errors/old-dub.brf` checks nine old-spelling
errors with exact fix previews, including nested types and a parenthesized
converter with comments. That checkpoint brought the inventory to 90 files.

N1's return-annotation slice uses `->` without changing return checking, inferred
results, explicit-unit tail discarding or outcome behavior. Existing headers and
fix previews were converted in place. `return-arrows.brf` observes typed/forward
calls, methods, unit tails, partial outcomes, newline continuation and unchanged
literal text. `errors/old-return-colons.brf` checks nine old annotations with
exact fixes, preserving parameter/field/binding colons and comments. The inventory
was then 92 files.

N1's alias slice uses `Name = type`, removes `type` keyword status and rejects
`pub` on aliases. `aliases.brf` observes composition, forward aliases, payloads,
ordinary names spelled `type` and unchanged literal text. `errors/old-aliases.brf`
checks modifier-removal previews while preserving comments. Existing aliases,
including old public aliases, retain their runtime behavior. That checkpoint
brought the inventory to 94 fixtures.

N1's literal-field slice converts initializers to `name = value`, leaving field,
parameter and binding annotations unchanged. `literal-fields.brf` observes nested
initialization, side-effect order, owned strings, field reads and assignment-only
control bodies; literal text with old-looking colons stays unchanged.
`errors/old-literal-colons.brf` checks four exact fixes with comments/continuation.
Existing initialization/ownership observations are unchanged. The inventory
was then 96 files.

N1's visibility slice removes `pub` from converted struct type declarations.
`visibility.brf` observes public/private fields and functions within one file,
including heap strings, private-field reads/writes and unchanged `pub`-looking
literal text. `errors/pub-structs.brf` requires three exact modifier-removal
previews, including a valid public field and retained comment. This records
visibility, not cross-module privacy enforcement; borrowing permissions remain
independent. The inventory was then 98 files.

N1's indexing slice uses `value.(index)`, with an adjacent two-byte opener.
`errors/old-indexes.brf` requires paired one-byte delimiter fixes: each help line
previews one edit, and both edits together correct an index. They leave old-looking
brackets in strings untouched. `errors/indexing.brf` keeps index reads, writes and
updates explicitly unimplemented; this slice adds no collection runtime behavior.
The existing unsupported-index case preserves its original semantic message/line.
The inventory was then 100 files.

N1's type-argument slice uses `Name[Type]` in types and calls, removing accepted-call
speculation. `errors/old-type-arguments.brf` checks grouped delimiter previews;
`errors/old-array-types.brf` checks `Arr[Type]` fixes without sizes in annotations.
Existing generic-scope errors and `dub` fixes retain their observations under the
new spelling. `bracket-comparisons.brf` observes formerly ambiguous comparisons,
ordinary shifts and unchanged angle/bracket-looking strings. Runtime generics and
collections remain unimplemented. The inventory was then 103 files.

N1's declaration-casing slice requires uppercase struct names and lowercase
functions, fields, parameters, bindings/constants and loop variables. `_` cannot
stand in for a declared name. `declaration-names.brf` observes ordinary reads,
loops and valid mixed-case/underscore suffixes; the rule concerns the first byte,
not a general camel-case style check. Two error fixtures check seven casing fixes
and suppression of replacements that would create keywords. `MAX_USERS` suggests
`maxUsers`, but there is no automatic whole-program reference renaming.

`object-c-names.brf` retains its outputs, C-keyword field `auto`, naive joined-name
collision (`A_b.c` versus `A.b_c`) and generated/global function-name collision.
Lowercase type `string` is no longer expressible, so it becomes `String`; emitted
names change, not runtime observations. The inventory was then 106 files.

N1's tag-casing slice requires lowercase tag names in type entries, values and
patterns. `tag-names.brf` observes unchanged heap payloads, wildcard payload
patterns, valid mixed-case suffixes and tag-looking strings. Two error fixtures
check four exact fixes and keyword/wildcard non-fixes. This does not case-fold tag
identities, change payload syntax or change `:ok`-based outcome rules. The inventory
was then 109 files.

N1's trait-only keyword retirement makes `trait`/`ovr` ordinary names. The new
`trait-ovr-names.brf` observes functions, fields, parameters, local/loop shadowing,
heap strings, tags and unchanged keyword-looking literals. Two removed-prefix
fixtures still reject, with fixes that delete the old word: the new language
writes traits without `trait` and replaced defaults without `ovr`. This adds no
trait, override or fieldless-declaration capability. The inventory
was then 112 files.

N1's pattern-name slice assigns roles directly from casing: lowercase names
bind, uppercase names reference symbol sets, and `_` stays a wildcard. Known set
matches and residual/payload binders keep their old behavior; the resolver no
longer guesses whether a binding is an alias. `pattern-names.brf` observes both
group and residual arms, heap payloads and scalar bindings. Its error companion
ensures an unknown uppercase name cannot silently become a catch-all, and
uppercase payload references remain unsupported refutable patterns. No
speculative lowercase rename is offered for an unknown reference. The inventory
was then 114 files.

N1 now parses top-level module-selection imports, without loading anything.
`errors/imports.brf` requires one scope error per import, even for nonexistent
paths and dependent names/signatures: resolution stops rather than guessing them.
`errors/import-modifiers.brf` checks public-import removal and final-module casing
fixes. A corrected import still cannot reach native compilation. There were then
116 fixtures.

N1 parses typed array/vector literals, but `errors/collection-literals.brf`
keeps native compilation explicitly unimplemented. Empty/nonempty literals and
unknown internal names/types each get one collection-scope error, not guessed
element or API behavior. No allocation, fill, iteration, indexing or collection
ownership is implemented. The inventory was then 117 files.

N1 records `:!name` error kinds, requiring one kind per module/name. The new
`error-tag-kinds.brf` observes heap/unit payloads, error-tag matching, equality
and literal text under the still-current outcome rules. `error-tag-main-n1.brf`
intentionally exits 0: kind-based entry failure belongs to N2, not this metadata
slice. Payload delimiters and runtime formatting were also unchanged in that
increment. Three rejection fixtures cover kind conflicts with source notes,
shared neutral `:ok`/`:none`, and adjacency fixes that preserve trivia. Existing
failure tags remain neutral until their source conversion. The inventory was
then 122 files.

N1 now uses `[...]` for payloads in type entries, values and patterns, and all
existing native sources keep their output/status/panic markers. The entry/panic
formatter still prints parentheses until N2; expected output text is not syntax
and was not rewritten. `bracketed-payloads.brf` adds empty versus unit payloads,
trailing commas, heap/nested values and literal text containing the old
spelling. `errors/old-payload-parentheses.brf` rejects all three old contexts
with paired fixes. `errors/multiple-payloads.brf` locks the explicit scope error
for lists with several positions, without unknown-type/name or arm-body
cascades. Such lists parse but have no native representation yet. The inventory
was then 125 files.

N1 now writes unions with `|`, retaining native set composition and old outcome
rules. `union-types.brf` observes named groups, nested payloads, partial returns
and the separate bitwise operator; `sym-name.brf` uses ordinary `Sym` as a
struct. `errors/old-symbol-unions.brf` requires rejection/fixes for the retired
wrapper, with no guessed unwrap of a lone named inclusion.
`errors/union-types.brf` keeps plain-type union inclusions and misplaced/bare
partial markers rejected. The older inclusion fixture now uses `int |
:included`, preserving the invalid inclusion rule rather than accidentally
converting it into a plain type alias. All previous source/expectation lines are
retained except syntax spelling. The inventory was then 129 files.

Typed payload binders now parse with retained annotations, but
`errors/typed-payload-patterns.brf` requires one explicit scope error per
affected arm. Unknown annotation types, input-shape checks, nested refutability
and uses of the unavailable binder do not cascade into additional errors.
Existing multiple-payload scope diagnostics still take precedence for several
positions. `errors/payload-binding-names.brf` checks exact lowercase-name fixes
and keyword safeguards. These are rejection tests, not merged-symbol or typed
matching support. The inventory was then 131 files. Multiple-payload
checking/lowering, typed matching, refutable payload patterns,
members/receivers, remaining keywords, broader module casing, N2 outcome
behavior and native chapter audits remain pending.

`Self` is now an ordinary type name. Existing owner-type references were
converted to the explicit struct name without moving source/expectation lines.
`self-name.brf` observes actual struct/constructor/receiver behavior, including
references to a real `Self` from another owner. `self-alias.brf` exercises a
symbol alias named `Self`; primitive aliases were still outside the native
subset at that increment. `errors/old-self-types.brf` requires exact owner-name
fixes for the retired parameter/literal/call shorthand, with no emitted C. Real
declarations take precedence; an unknown name outside an owner gets no guessed
replacement. The inventory was then 134 files. Member/receiver grammar and the
other pending work above remain incomplete.

Native aliases now reuse every existing type's canonical identity, including
primitives, unit and structs. `type-aliases.brf` executes forward/mixed alias
chains, struct literals and associated/receiver calls, shared mutation, numeric
and string converter aliases, unit results, symbol payloads and recursion
through structs. `panics/alias-overflow.brf` pins the unchanged underlying
overflow panic. `errors/type-aliases.brf` rejects cycles, unknown/unsupported
types, scalar union inclusions, partial aliases and non-symbol named patterns;
these are not general typed patterns. `errors/type-alias-checks.brf` keeps type
mismatches, literal ranges, explicit unit returns and read-only access rejected
through aliases. The inventory was then 138 files. No prior native source or
expectation is rewritten, and no IR, ownership or runtime type is added.

Empty type bodies are now recognized as unsupported traits. The old fieldless
struct fixtures use explicit unit fields and initializers, retaining equality,
nominal typing, lookup/inference errors and original expectation lines.
`unit-structs.brf` observes an initializer's side effect even though its field
has no runtime storage, shared borrowing and equality. `errors/empty-traits.brf`
requires one scope error per trait, without guessed fields or dependent
alias/type/body/main errors. Trait member/signature grammar and conformance are
still pending. That inventory had 140 files. The converted equality fixture's
only C change is its unused `Empty` diagnostic formatter describing the explicit
unit field; its runtime output is unchanged.

Qualified symbol syntax is now retained but not resolved. The rejection fixture
`errors/qualified-symbols.brf` requires an explicit scope error before external
payload types/values, nested patterns or arm bodies can cascade. A qualified
payload creates no local alias-cycle edges or local tag of the same name.
`errors/qualified-symbol-names.brf` checks module/tag casing fixes, keyword
safeguards and the shared-tag qualifier prohibition. These are rejection tests,
not module loading, remote identity, merged symbols or N2 outcome acceptance.
That inventory had 142 files; prior native sources and expectations were intact.

Struct functions now belong inside their owner's declaration, without `impl`
blocks. Existing fixtures keep their explicit receiver parameters and observed
behavior; alias-targeted functions now live on the canonical struct. The new
`struct-members.brf` checks fields after functions, constructor/member calls,
mutation permissions and an ordinary function named `impl`.
`errors/old-impl-blocks.brf` requires rejection without an unsafe relocation
fix. Typed trait signatures/default bodies still hit the unsupported-trait
barrier. The inventory was 144 files. Native rejection lines and expectations
remain unchanged except the declaration message no longer lists retired `impl`
syntax. Receiver inference, `self`/`&self`, wrong-way rules and N2 behavior
remain pending.

Primitive-spelled members use the existing owned function representation.
`primitive-members.brf` executes all eleven primitive spellings, associated and
explicit receiver calls, alias lookup and ordinary builtin conversions in
member bodies. `errors/primitive-member-names.brf` permits a casing fix to a
primitive only for a member function; ordinary function/binding guards remain.
The inventory is now 146 files. No prior source or expectation is rewritten,
and JSON/module APIs, receiver inference and N2 behavior are not added.

These are compilation/execution tests for the current subset, not the much
broader syntax-only examples in `tests/parse/` and the doc parser manifest.
