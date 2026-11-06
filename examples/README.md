# Examples

Small programs that show the first-version feature scope, with N1 spelling
updates as they are implemented: `not`, `dbl`, `->` returns, keyword-free
aliases, `=` literal fields and functions inside struct bodies. Methods infer
readonly receivers from `self`, or write `&self` for mutation. Associated
functions remain type-qualified; ordinary owner-typed parameters are not
implicit receivers. Unambiguous failure tags use `:!name` in types, values and
patterns. Shared `:ok` and optional `:none` stay neutral. Outcome handling and
runtime spelling still follow the first version until N2; these are source
spelling changes, not an outcome-policy switch. Each program runs on its own:

```sh
./build/brief run examples/calendar.brf
./build/brief run --debug examples/calendar.brf   # with sanitizers and leak checks
```

All six complete sources now run in normal and sanitized CI. Their original
bytes are preserved in generated copies, with committed output/status markers
from `tests/examples/*.expect` appended. Source hashes and exact inventory
checks require review when an example changes; tests never regenerate goldens
from the compiler under test. stderr must be empty and exit status zero.

```sh
ctest --test-dir build -L native-examples --output-on-failure
```

This is independent example coverage, not acceptance of the mixed raw reference
sheet in `docs/lang-ex-base.txt`. N1 remains in progress; N2/N3 semantics are
unchanged.

| Program | What it does | Language features it shows |
|---|---|---|
| `calendar.brf` | Validates dates, finds weekdays and days of the year, adds and subtracts days, counts business days, computes Easter Sunday and prints month grids. | Struct member functions, associated calls, a named error set, `?` in `main`, value-producing `if` and matches, loops. |
| `loan.brf` | Calculates loan payments, total interest, the effect of extra payments and an amortization table, in exact integer cents. | `lng` arithmetic, a binary search over a simulation, fallible results matched where they are made, string formatting by hand. |
| `life.brf` | Runs Conway's Game of Life on an 8x8 wrapping board stored in one `ulng`, and detects when a pattern dies, freezes or cycles. | Bitwise operations and shifts, a linked history built from a recursive struct, `?` on an optional result. |
| `symbolic.brf` | Prints, simplifies and differentiates expressions in `x`, evaluates them with division-by-zero errors, and finds roots with Newton's method. | A recursive symbol type through structs, exhaustive matches, the product, quotient and chain rules, error propagation with `?`. |
| `numbers.brf` | Primes and factorizations, gcd and Euler's totient, exact fractions and Egyptian fractions, factorials that stop before overflow, Roman numerals, base conversion, perfect numbers and Collatz chains. | Checked arithmetic, typed failure instead of overflow, struct invariants in a constructor, unsigned integers. |
| `bank.brf` | A ledger of accounts with deposits, withdrawals, transfers that validate before moving money, monthly interest, statements and a report. | `&` parameters, shared mutable structs, linked lists, an inferred error set on `transfer` that callers handle exhaustively. |

The programs work around features that later versions add: strings have no
length or indexing yet, there are no collections, and the only output is
`print`. Lists are therefore built from recursive structs, and text is
measured by the code that formats it.
