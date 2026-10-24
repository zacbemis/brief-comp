# M6 acceptance slice: `docs/lang-syntax.txt`

`test_doc_syntax.cmake` reads all **33 code fences** directly from the document.
Thirty are represented in **65 native/diagnostic cases**; three collection-only
fences are deferred. Fences **3, 4, 6, 17, 26, 32 and 33 are explicitly partial**.
Selected inline examples and first-version prose rules have additional cases.
This is executable/diagnostic acceptance, not a replacement for parser coverage
or completion of M6.

```sh
ctest --test-dir build -L doc-syntax --output-on-failure
```

Normal and sanitizer-enabled variants use the shared document harness and C
runner. They check exact output/status, every diagnostic message/source line,
sanitizer failures and clean debug ownership exits. Fence counts, line/alternative
counts, single adaptation anchors, classifications, unique case names and the
expected case count are guarded. Expression wrappers preserve the original
expression and comment, placing only the complete expression inside `print(str(...))`.

## Fence-by-fence record

Names identify generated `.brf` files under
`build/tests/test_doc_syntax-fixtures/programs/`, with a separate directory for
the sanitized variant.

| Fence | Example | Check or classification |
|---|---|---|
| 1 | Line/block comments | `comments`: retain both comments and execute a following print. |
| 2 | Newline-separated bindings | `newline-statements`: retain both statements and observe the getter result. |
| 3 | Multiline statements | **Partial:** `continuations` retains arithmetic, fallible pipeline and multiline constructor calls. Three inputs exercise success, first-stage failure and second-stage failure, with stage effects proving skipped work. The complete `users.find(lambda)`/getter chain is removed by one exact anchor and remains deferred; it is not replaced with an invented collection API. |
| 4 | `let`, `var`, `const` | **Partial:** `binding-kinds` omits only the guarded `const` line. Object mutation through `let` and updating `var` both run. |
| 5 | Immutable/mutable bindings | Split the two blank-line alternatives. `immutable-let` requires the reassignment diagnostic on the original line; `mutable-var` runs and prints 1. |
| 6 | Written binding types | **Partial:** `annotated-bindings` retains `lng`/`flt` declarations and proves their contexts with a wide integer expression and a binary32 division (`0.5 / 3.0` prints `0.16666667`, not the binary64 digits). The `const` and `Vec` lines remain deferred. |
| 7 | Inferred bindings | `inferred-bindings`: retain all three declarations and observe the integer, string and nominal object. |
| 8 | Incorrect annotation | `annotation-mismatch`: exact binding-contract diagnostic on the original line. |
| 9 | Arithmetic operators | `arithmetic`: observe all six original expressions, including division, remainder and power. |
| 10 | Updates/compound assignment | `updates`: retain every original statement and observe both variables after each one, rather than letting intermediate mistakes cancel. |
| 11 | Comparisons | `comparisons`: observe every original expression. |
| 12 | Bitwise operators | `bitwise`: observe every original expression, including complement and shifts. |
| 13 | Logical conditions | `logical-conditions`: retain both conditions/early returns in a function; exercise the age guard, inactive/banned paths and fallthrough. |
| 14 | Arithmetic precedence | `arithmetic-precedence`: observe multiplication versus addition, left subtraction, right-associative power, power before unary minus, and method-call before logical not. |
| 15 | Bitwise precedence | `bitwise-precedence`: retain both expressions, including mask/comparison precedence and the `\|`/`^`/`&` hierarchy. |
| 16 | Nonchaining comparisons | `chained-comparison` rejects the original chained form; `separate-comparisons` executes the `and` alternative. |
| 17 | Range precedence | **Partial:** `range-precedence` uses the first original expression as a first-version loop bound and observes 0, 1, 2 for count 2. It does not claim a first-class range value. The `0..10 \|> sum()` expression remains deferred. |
| 18 | Pipeline precedence | `pipeline-precedence`: observe all three expressions and effectful boolean stages, checking pipeline placement relative to comparisons, logical operators and arithmetic. |
| 19 | Match on comparison | Split the alternatives. `comparison-match-unparenthesized` requires both wrong-input pattern diagnostics and the integer/string comparison diagnostic from the documented parse. `comparison-match-parenthesized` runs both sides of the threshold. |
| 20 | Arithmetic on pipeline result | `pipeline-arithmetic-unparenthesized` rejects the original invalid stage shape; `pipeline-arithmetic-parenthesized` runs the corrected expression. |
| 21 | Logical/binding precedence | `logical-precedence`: retain both expressions and observe results distinguishing `and` from `or` grouping and a complete boolean binding initializer. |
| 22 | `if` / `else if` / `else` | `if-branches`: execute all three original branches. |
| 23 | Value-producing `if` | `if-value-first` / `if-value-second`: split the duplicate declarations into separate functions and execute both outcomes of each original form. |
| 24 | `if` without `else` as value | `if-missing-else`: exact value-context diagnostic on the binding. |
| 25 | `while` | `while`: retain the loop and observe final count 10. |
| 26 | Range/collection iteration | **Partial:** `integer-range` executes the unchanged range loop and checks every output 0–9. The collection loop remains deferred. |
| 27 | Collection search with jumps | Deferred: collection iteration. Integer-loop jump semantics have independent prose cases below; no user collection is synthesized. |
| 28 | `break` outside a loop | `break-outside-loop`: retain the complete program and require its exact loop-context diagnostic. |
| 29 | Nested collection loops | Deferred: nested collection iteration. The innermost-loop rule is separately tested on integer ranges. |
| 30 | Collection loop/body braces | Deferred: collection iteration. |
| 31 | Struct literals in conditions | `struct-condition-unparenthesized` checks both parser recovery diagnostics with the token-preserving line adaptation described below. `struct-condition-parenthesized` retains both corrected alternatives; method/function effects prove both calls run. |
| 32 | Generic calls versus comparisons | **Partial:** `generic-versus-comparison` retains both comparison declarations and executes true/false inputs. Both `Vec` calls remain deferred, including the nested generic closer. |
| 33 | Generic-call ambiguity | **Partial:** `unambiguous-comparison-arguments` retains the two unambiguous comparison-call forms and observes their boolean arguments. The first line is deliberately parsed as a generic call and is deferred, not counted as native acceptance by rejecting unavailable generic support. |

## Scaffolding and negative-example fidelity

`User` is a local nominal type with a deterministic associated constructor and
receiver getters/predicates. Its four fields supply omitted contexts for this
chapter; mutable `active`/`banned` fields let the original logical examples take
each path. Heap-backed names exercise ordinary ownership. `Json`, `parse` and
`validate` in fence 3 are local deterministic stand-ins, not modules or a JSON
implementation. `Guest`, its receiver method and the global predicate supply
the original condition examples without unknown bindings. No deferred API is
implemented.

Expression-list fences receive only input bindings and whole-expression output
wrappers. Statement-list fences receive local function contexts; duplicate
alternative declarations are split at guarded boundaries. No operator grouping
inside an original expression is changed. `MASK` is an ordinary local binding,
not a newly supported `const` declaration.

Fence 31 currently emits two identical `Expected a newline between items.`
diagnostics on its first line. The C runner supports one error expectation per
source line. The adapter inserts only a newline **before `.`**, which the
specified newline filter removes, preserving the filtered token stream while
placing the two diagnostic sites on separate lines. Both are required, not
suppressed or collapsed. The valid counterparts run unchanged. Existing parser
document tests also retain the original unadapted fence.

These generic parser messages do not explain the required struct-literal
parentheses particularly well. An actionable condition-literal hint remains a
**diagnostic-finish opportunity**; the audit records current exact behavior and
does not claim that all M6 diagnostic work is done. Another open gap is the
follow-on `A local binding is not callable.` after fence 33's deferred
generic-call line reports `Generic calls are not implemented yet.`. Fence 19
similarly records all errors from its documented unparenthesized parse, rather
than weakening the runner to accept an arbitrary failure.

## Guarded inline and prose cases

- Reject the original update initializer/argument and a condition counterpart
  with the specific update-statement error. The condition's illustrative `...`
  is replaced only with an empty body, so the error must be the update rule.
  Prefix `++x`/`--x` now require `Increment and decrement must be postfix
  standalone statements (x++ or x--).`, following a later M6 diagnostic update.
- Reject chained assignment, assignment in a condition/argument, and a bare block
  argument. Observe binding/field assignment and field increment/decrement.
- Reject self-initialization, duplicate names, parameter/iteration reassignment
  and use of an iteration binding outside its body. Execute an inner initializer
  reading the outer binding of the same name, preserving the outer value.
- Reject incompatible value-producing branches and loop values in an initializer
  or argument; accept discarded heterogeneous branches, including a heap string
  at the tail of a written-unit function, with leak-free cleanup.
- Evaluate same-typed narrow integer bounds once, left to right; check ascending,
  empty, descending and unsigned-maximum-edge ranges. Reject different typed
  bounds, floating bounds and chained ranges.
- Observe nearest-loop break/continue behavior in nested integer loops and
  continued induction-variable advancement; reject `continue` outside loops.
- Reject `&&` and `||` with valid boolean operands. The updated diagnostics
  suggest `and`/`or` and highlight both operator bytes, rather than a generic
  expression error at the second character.
- Exercise newline continuation after all in-scope listed declaration/control
  keywords (`let`, `var`, `type`, `impl`, `pub`, `if`, `else`, `for`, `in`,
  `while`), operators, colon, `=`, `=>`, `|>` and `.`, including before a line
  beginning with `.`. A match inside call parentheses keeps its arm separators.
  Multiline call arguments/comma and leading pipeline continuation are also
  checked by the original fence 3.

`const`, lambdas, collections, indexing/array-specific continuation and
user-written generic calls remain deferred. First-version ranges are loop bounds,
not collections. Existing parser/newline, semantic, arithmetic, matching and
ownership suites remain independent stage-level checks; their broader grammar
coverage is not represented as native support. The original audit needed no
compiler/runtime changes. The later operator-diagnostic slice changes only
parser errors, leaving tokenization, accepted grammar and runtime semantics
unchanged. Stage tests additionally check LF/CRLF spans, item/declaration
recovery, reparsing unchanged tokens, lexer-only independence, and separated
single bitwise operators (including intervening comments/newlines).
