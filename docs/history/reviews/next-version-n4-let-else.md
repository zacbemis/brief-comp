# N4 slice: let-else through checked matching

Base: `f7fce5d`, the committed assignment-path slice. Initialized named let
bindings now accept an else block. This completes the next checklist item,
not N4: inclusive ranges, typed loop variables, audits and closure remain.

## Syntax and scope

`let name = value else { ... }` accepts the usual optional type annotation
and a newline before `else`, like an if's else. The initializer is evaluated
once. Its own if expressions still consume their own else in the ordinary way.
Only initialized named lets have this form; var, const, missing initializers,
missing blocks and general destructuring patterns gain no substitute syntax.
No guessed rewrite or default value is supplied.

The AST keeps an optional else body; dumps add an else only when written.
Both syntax-depth checking and syntactic self inference visit the new body.
Nested bodies respect the existing depth limit. Input and else resolution happen
before the new binding is introduced. An outer name remains visible in both;
the new name and private success projection do not leak into the else scope.
References to self in an else still classify a struct function as a method.

## Checked match and remaining value

Resolution creates an ordinary semantic match with two arms and a private
success variable. Checking partitions the input's raw variants after its type
stabilizes. Raw tag tests survive canonical singleton normalization. The input
must contain an outer `:none` or error tag; plain data, including normalized
singleton `:ok`, rejects rather than acquiring an invented optional envelope.

The fallback covers all outer errors and absence; successes are what remain:

- One remaining tag yields its sole payload, or unit if it has no positions.
  This is the same projection rule as postfix handling, after removing absence
  as well as errors. A unit payload is still a real payload position.
- Several remaining tags stay a narrower tagged union, even if payload types
  happen to agree. Positional schemas and tag identity are preserved.
- No remaining tag makes the success arm unreachable. The else executes and
  diverges; the declaration produces no value or store.
- Several positions on one surviving tag still need an explicit match. This
  slice invents neither tuples nor multi-position success extraction.

The fallback is checked in discard context and must have no fallthrough path.
Return, break, continue or nonterminating control flow can satisfy that rule.
A conditional return on just one path does not. Fallback values do not compete
with the success type, and ordinary binding annotations constrain the result.

This is handling, not automatic propagation: input errors do not silently join
the enclosing result. An explicit return or postfix in the else still obeys
normal result contracts. To inspect failure payloads, use an explicit match;
no extra error-binding API or hidden source name is exposed.

## Flow, owners and permissions

The checked node uses the existing match flow, IR lowering, RC and permissions;
none of those sources or the runtime changes. Each input is evaluated once.
Returning/breaking/continuing fallback paths clean the scopes they actually
leave. A declaration's result is stored only on its success path. Existing
empty-slot cleanup still handles other deferred locals on an early fallback.

Owned payloads and tagged remainders retain the input's owners through the
private projection, match result and written binding. Aliases remain live
without a destructive runtime move. Ordinary borrowed input permissions stay
read-only through projection, field stores and forwarding; `&` access stays
writable. An owned return is not a permission upgrade.

Using a stored outcome as the input handles it once. A later use cannot handle
it again, including from the fallback. Only outer failure/absence is removed:
a nested error payload extracted as the new value becomes a new outcome-binding
obligation; a nested absence remains ordinary data. Recursive inference probes
can acquire variants later without permanently poisoning an SCC. Canonical
results remain stable across rechecks and function order.

## Every changed existing expectation

**None.** All old C-test inputs/assertions, Brief fixture bytes/markers, ASTs,
diagnostics, runtime observations, goldens, `.expect` files, parser/audit
classifications and generated chapter programs remain unchanged. All retained
successful C output is byte-identical. No old negative is replaced or weakened.

Seven independent base/current probes show four newly emitted programs and
three exact semantic rejections replacing old parse failures. They cover
optional and mixed extraction, tagged remainders, terminal inputs, missing
fallthrough exits, ineligible values and second handling. These are new complete
programs, not repaired chapter fragments or changed committed expectations.

The new diagnostics are `A let-else value must contain :none or an error tag.`
and `The else block of a let binding must not fall through.`. Non-let syntax
gets `Only let bindings can have an else block.`. Multi-position extraction
uses the existing explicit-match boundary; other type, scope, return, outcome
and permission errors retain their established messages. No new fix is guessed.

## New coverage

`test_let_else.c` adds eight groups with LF/CRLF, immutable token/source checks,
three semantic checks per input, canonical return identity, blocked invalid
lowering, idempotent RC and C emission. They cover:

- Typed/named syntax, newline else, AST and two-arm metadata, malformed forms,
  nested syntax and depth limits without mutating parser tokens.
- Optional, error-only, mixed and sole non-`:ok` successes; unit/payload
  extraction, several remaining tags and unsupported positional extraction.
- Mandatory divergence on every path, infinite else loops checked without
  execution, annotation mismatches and terminal declarations with no store.
- Explicit replacement errors and written return contracts, break/continue,
  definite assignment on departing paths and postfix-before-else composition.
- Outer-name shadowing, no premature scope for the new name, syntactic self
  in else and second handling of a stored input.
- Writable/read-only projections, forbidden field storage and `&` forwarding,
  nested error obligations and nested absence without recursive filtering.
- Recursive inferred optional results in both function orders, exact canonical
  result contents and an unanchored recursive rejection without guessed types.

Three fixtures bring the native inventory to **217**:

- `let-else-n4.brf`: owned struct/string successes, absence and errors taking
  fallback, once-only side effects, continue/break, shared aliases, tagged
  remainders, payload-less success, nested errors, terminal input cleanup and
  recursive success/absence. Nine source calls are observed exactly once.
- `main-let-else-n4.brf`: an explicit replacement error leaves before the new
  result and another deferred reference are stored, with exact stdout/stderr,
  payload cleanup and status 1. Original input errors need not be propagated.
- `errors/let-else-n4.brf`: thirteen exact primary sites and two declaration
  notes, no fixes, across eligibility, divergence, types, scope, permissions,
  return contracts, repeated handling and extracted outcome obligations.

No language chapter is edited, pending fence promoted, missing API completed or
document fragment extracted. The broader scoped N4 audit update remains open.

## Validation

Default GCC Debug ASan/UBSan and fresh `-Werror` Clang Debug ASan/UBSan pass
**55/55** each. Pinned clang-format 23.1.3, diff/prose widths and protected plan
section checks pass; the top status stays one line. Goal, Scope, Principles,
Design, Tests and Milestones remain byte-identical. The full four-configuration
matrix remains reserved for N4 closure.

The archived `f7fce5d` baseline captures **2,885 source-create records** from
sixteen suites (**2,648** unique). All **6,646 AST/emit observations across
3,323 retained sources**, including **1,437 successful C outputs**, match.
All **296 prior sanitized executable observations** retain exact split
stdout/stderr/status, including examples and existing chapter programs.

Both new executables pass **8/8** strict GCC/Clang normal/sanitized runs and
two CLI debug runs. All thirteen negative sites reject emit/build/run before
a nonexistent CC, without fixes, source mutation or executable publication.

One initial new reuse test mixed a unit fallback return with an integer final
expression and legitimately produced two errors. An explicit unit function
contract now isolates the intended second-handling rejection. No compiler rule
or old assertion was relaxed to fix that newly authored test.

Evidence and scripts are in `build/validation/n4-let-else.20261109T185126Z/`.
N4 remains in progress; this slice stops uncommitted for review.
