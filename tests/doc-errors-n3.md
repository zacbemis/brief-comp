# N3 native audit: handling outcomes

Base: `27d7eb1`, committed unchanged. This extends
`test_doc_errors_n2.cmake` in place; its existing name, N2 cases and hash/count
remain. [The N2 audit](doc-errors-n2.md) records its historical narrower scope.
All 38 fences are classified once using unchanged `doc_native.cmake` helpers.
Two whole fences execute, 36 remain listings, and 33 independent complete
prose programs exercise available rules. No missing API/type/body is supplied.

```sh
ctest --test-dir build -L doc-errors --output-on-failure
```

Normal/sanitized sources, inventories and logs are in
`build/tests/current-doc-errors{,_sanitized}/`. Exact stdout/stderr/status,
panic locations, diagnostic messages/lines and no-unexpected-fix checks use
the existing native runner. Shared integrity guards are unchanged.

## Section 6 fence inventory

| Fence | Disposition |
|---|---|
| 24 | Context: missing file, File type, close method and qualified errors. |
| 25 | Context: readFile is not defined; no body is supplied. |
| 26 | Context: Loan and done are unavailable; no fabricated error proof. |
| 27 | Context: file/close are absent; not a missing-API rejection test. |
| 28 | Context: missing User and getAgeRange; no substitute type/API. |
| 29 | Run: the complete print body, unchanged inside main. |

Fence 29 prints `Hello`, with empty stderr and status zero, establishing that
unit results may be ignored. Fence 5's existing complete Health alias and all
other N2 fence dispositions remain unchanged. The five context listings in
section 6 were formerly marked later/N3; fence 29 was also a later listing.
These explicit coverage promotions change no old execution expectation.

No fence is split, relocated, repaired or mined for a program. The literal
File/Loan/User examples remain listings until their real dependencies exist.
Lambda bodies remain outside native support and are not simulated.

## New independent prose programs

Twelve prose programs add five executions and seven rejection programs;
the whole print fence is a sixth new execution. The prose executions cover
returned calls in written/inferred/partial functions, branch/match tails,
final pipeline stages, stored postfix/match/return handlers, intrinsic
conversion bindings, rearmed owned values and retained aliases. Error-only
returned entry values print and release the owned payload before status 1.
Neutral-only data, optional values and nested errors remain freely copyable.

Seven new rejection programs require 24 exact diagnostic sites:

- Non-tail unhandled calls in statements, arguments, fields and symbol payloads.
- Forbidden copies/passing/storing of pending bindings, without type cascades.
- Second postfix, match and return handling after the value is already handled.
- Pending normal scope, early return, propagation, break and continue exits.
- If, match and loop joins, each with one primary despite later reads/exits.
- Pending/immutable writes and a separately wrong assignment type.
- Returned outcomes excluded by written plain or nested-success contracts.

All 22 prior generated N2 programs are retained byte-for-byte, including their
nine negative diagnostic sites. The extended audit has **35 runner cases**:
22 executions (including two expected panics) and thirteen rejection programs
with 33 exact sites. Existing unit/CLI/native fixtures provide broader loop,
recursive inference, permission and diagnostic-note coverage; this extension
does not replace their tests with a narrower prose sample.

## Expectations and validation

**Changed existing expectations: none.** All old Brief fixtures, C-test inputs,
runtime markers, goldens, `.expect` files, language chapters, parser inventories
and N1/N2 generated programs are unchanged. Only the six section-6 native
classifications described above change coverage status. No language rule is
changed in this audit/closure slice.

Initial new tests put two written-return markers on expressions rather than
function openings; a new tail match also accidentally joined int and unit.
The markers were located correctly and the intended discarded match was made
explicit. These were new-program corrections, not altered old expectations.

See [the N3 closure review](next-version-n3-review.md) for the full matrix,
retained-source comparison, strict native checks and remaining scope boundaries.
