# N2 slice: reject postfixes without error tags

Base: `aaba1f1`. This slice is uncommitted and stops for review. It does not
close N2 or implement exact returns, singleton-`:ok` normalization, optional
endings/ambiguity or the scoped symbols/errors native audits.

## Rule and implementation

`?` and `!` require an operand with top-level error tags. Plain values,
neutral-only unions, optionals and neutral wrappers containing nested errors
cannot use them, even when `:ok` occurs in the outer union. The diagnostic is:

```text
? and ! cannot handle this value: it has no error tags.
```

There is no universally correct fix: removing the operator, changing the result
contract or matching the data are different choices. No fix or note is guessed.
Unknown/error/never operands keep their existing recovery. SCC probes defer
provisional neutral symbol sets rather than poisoning inferred error paths.
Rejected postfixes have no propagated set and cannot reach lowering.

Only `src/sema/check.c` changes production behavior. Error-only and mixed
postfix handling, method hints, call wrapping, lowering, permissions, ownership,
code generation and runtime sources are unchanged. The legacy `sema_failures`
API remains characterized by existing tests but is no longer used by postfixes.

## Every changed existing expectation

1. The old `? and ! require a symbol outcome with an :ok variant.` message
   becomes the diagnostic above. Direct assertions change in `test_compile.c`
   (`:nope?`), `test_errors.c` (`1?`, `:data!`, `:none?`, `:box[:!bad]!`),
   `test_sema.c` (both recursive neutral-union postfix cases), and
   `programs/errors/propagation.brf` (`:data?`). Counts and spans stay the same.
   Retained CLI observations also change only this message for the two
   receiver-classification `self?`/`self!` inputs and parser-pass fixture
   `38-error-tag-kinds.brf:12:3`. Their syntax/AST expectations do not change.
2. The original compound success-only error-test source now rejects its two
   `only()!`/`only()?` sites instead of accepting them. Its ABI assertions stay;
   an additional positive source uses plain calls and retains the old match and
   pipeline coverage. The separate `f() -> :ok[int]` / `f()!` source now rejects
   once instead of accepting.
3. The 30 infallible numeric conversion inputs in `test_conversions.c` now
   reject their original `!` (and additionally test `?`). Positive inputs omit
   it. The same 30 sites in `programs/conversion-matrix.brf` omit `!`:

   | Source domain | Infallible target domains |
   | --- | --- |
   | shrt | shrt, int, lng, flt, dbl |
   | ushrt | ushrt, int, uint, lng, ulng, flt, dbl |
   | int | int, lng, flt, dbl |
   | uint | uint, lng, ulng, flt, dbl |
   | lng | lng, flt, dbl |
   | ulng | ulng, flt, dbl |
   | flt | flt, dbl |
   | dbl | dbl |

   Every source/target domain, converter shape, result type and ownership check
   remains covered. Fallible conversions and their handling are unchanged.
4. `f() { int(f())? }` now rejects once: its inferred conversion is infallible.
   The original source remains a negative check; a plain-call control retains
   the recursive converter inference coverage.
5. The written-return mismatch source containing `g() { f()! }` now has two
   errors rather than one: the same body-contract error and one no-error
   postfix rejection. Its original source is retained; the written contract
   and absence of partial-set collisions remain asserted on repeated checks.
6. The original neutral-overflow recursive probe with `int(g(n)?)?` and
   `lng(f(n)?)` now has two no-error postfix rejections instead of its one
   converter-kind collision. Removing the two inner postfixes restores the
   isolated collision check, including its exact note/span, LF/CRLF and
   repeated checks. The original LF source also remains an explicit two-error
   regression. No converter diagnostic or collision policy changes.
7. Both functions in both recursive converter C-test programs lose a phantom
   `:ok[int]`: their inferred result is only `:!bad`, not a two-tag outcome.
   The unhandled-input program still has its one handling error; the handled
   program still accepts. `numeric-conversions.brf` drops the corresponding
   unreachable `:ok[_]` observer arm; the `:!bad` route still prints recursive.
8. Existing native sources remove redundant operators, without changing any
   stdout/stderr/status marker: `numeric-conversions.brf` removes the three
   bangs on `flt(16777216)`, `flt(-0.0)` and inner `dbl(exact)`;
   `outcomes.brf` uses plain `text()` instead of `text()!` and a plain binding
   instead of `data()?`; `examples/life.brf` removes `?` from the recursive
   optional `seen(older, board)` call. Life's whole-source audit hash is
   repinned; its committed golden stays unchanged. The matrix edits are listed
   in item 3. Observer/source line moves are not runtime location guarantees.

No other existing expectation changes. No current language document, quoted
fence, parser classification or example golden changes. Nothing is extracted
from a document or supplied with a stand-in API.

## Added regressions and validation

`test_symbols.c` checks 26 complete source shapes with both operators and
LF/CRLF, each checked three times. It pins one exact primary, its expression
span, no fix/notes, immutable source bytes, error type, no propagated set and
refusal to lower. Cases include scalars/unit/owned strings, plain/only-`:ok`
calls, ordinary/optional unions, nested errors, structs, infallible converters,
pipelines, a second postfix after successful error removal and unreachable
syntax. Additional recovery controls cover missing calls/bindings, nested
postfixes, checking later syntax and terminal error-only operands.

`programs/errors/no-error-postfix-n2.brf` adds fourteen independent rejection
sites, including a stored optional. CLI emit/build/run checks require the same
primaries, no help, no emitted C/executable and rejection before a nonexistent
C compiler can be invoked. Program inventory is now 180 fixtures.

Commands run:

```sh
cmake --build build -j 6
ctest --test-dir build --output-on-failure --parallel 4
# Also a fresh Clang Debug ASan/UBSan build with -Werror, and its full ctest.
build/format-env/bin/clang-format --dry-run --Werror <changed C files>
git diff --check
```

Default GCC Debug ASan/UBSan and fresh Clang Debug ASan/UBSan pass 37/37 each.
This is not the four-configuration milestone-closing matrix. Initial focused
and full runs failed on the explicitly inventoried old expectations. Two newly
authored controls initially lacked parentheses around a string expression and
put a function's opening brace after a terminating newline; only those test
inputs were corrected, without broadening grammar or production recovery.

A fresh Release baseline is built from archived HEAD, not an old binary. It
captures all 1,675 source-create records from the seven semantic/compiler C
suites, yielding 1,529 unique raw inputs, plus 462 retained complete CLI
sources. All comparisons use saved original bytes, including edited native
sources. The 1,991 ASTs are identical. Only 59 emit-C observations differ: 38
formerly accepted sources now reject, 20 rejection observations change as
inventoried above, and the handled recursive converter source loses its
phantom-success C layout/paths. Of 806 prior C outputs, the remaining 767 are
byte-identical. The standalone recursive observer emits and runs under strict
GCC/Clang, normally and sanitized (8 checks), with the same `:!bad` entry
status/output.

The 156 retained sanitized executable comparisons differ only for the five
old source copies containing newly rejected postfixes (life appears both as an
example and an audit copy). Re-running their corrected whole sources preserves
all five original status/stdout/stderr observations. The other 151 observations
are unchanged, including the other five examples; no golden is regenerated.
The thirty corrected infallible converter C-test sources emit byte-identical C
with the baseline and current compiler.

Ignored evidence, baseline sources, scripts, complete comparison deltas and
logs are under `build/validation/n2-no-error.20261107T004210Z/`. N2 remains in
progress. The unrelated untracked `skills/` directory is untouched.
