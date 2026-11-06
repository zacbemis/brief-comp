# N1 final completeness review

**N1 is complete for its milestone scope. N2 has not started.** This review is
against the three N1 “Done when” criteria in
[`next-version.txt`](../docs/plans/next-version.txt), not the full next-version
language, full chapter/API acceptance or production readiness. Reviewed base:
`ca94731` (raw-base audit and example CI, committed unchanged).

## 1. Converted tests retain their expectations

- All **156 native fixtures**, **155 parser fixtures** (41 pass / 114 fail),
  embedded C tests and CLI/native-driver tests pass in the full fresh matrix.
  Normal and sanitizer program runners check exact output, status, expected
  panic/error messages and diagnostic source lines, including required fixes.
- Comparing Git history to pre-N1 `8fc08f5` finds all **87 original native
  fixtures** still present. All runtime markers (`expect`, `stderr`, `panic`,
  `exit`) are identical, including the 53 files with explicit runtime markers.
  Old negative rules remain covered. Only one original primary message changes:
  the declaration message no longer advertises the removed impl form. Added
  underscore/not rejection cases are additional checks, not weakened errors.
- The previously reviewed not cases insert six lines before the old update-value
  errors in `errors/update-values.brf`. Those fixture-local marker locations
  move with their source; the old rules/messages are retained. This is not a
  claim that added source lines leave physical line numbers unchanged.
- The nine renamed/replaced original parser-failure paths are accounted for:
  collection/import/conformance/partial-marker promotions, retired trait/ovr
  rejection names, missing-literal-delimiter renaming and obsolete Sym delimiter
  rules replaced by union member/separator errors. Their dispositions are in
  `parse/README.md` and the corresponding historical plan increments.
- Slice-by-slice migration evidence remains in the plan's historical records,
  including actual assembled/generated C-test inputs, not just literal grep.
  No source migration or expectation adjustment is made by this final review.
- All **432 retained complete CLI comparisons** (`dump-ast` and `emit-c`),
  including **144 successful C outputs**, match the saved pre-base-audit
  observations exactly. All six generated example copies also retain their C
  hashes. This is a current regression check, not a claim that pre-N1 ASTs or
  source-excerpt spellings are identical after the syntax migration.
- All six original examples retain baseline sanitized stdout/stderr/status.
  CI additionally runs their whole-source copies with committed expectations:
  269 stdout lines / 7,311 bytes, empty stderr, status zero. The examples are
  independent programs, not completion of the raw sheet's missing context.

The source-spelling guard checks 24 complete files. Neutral data/optional tags
remain intentional, including append's reviewed failure `:!blank` versus nested
neutral `:empty`. Tag kinds remain metadata; outcome handling, entry behavior
and runtime formatting have not switched to N2 rules.

## 2. Old grammar rejects with safe fixes

The existing tests cover these families at parser, semantic and CLI boundaries:

| Removed form | Rejection/fix evidence |
|---|---|
| Prefix bang / dub / return colon | `test_parser`, `test_cli`, `errors/update-values.brf`, `errors/old-dub.brf`, `errors/old-return-colons.brf` |
| Alias keyword / pub types | `test_parser`, `test_cli`, `errors/old-aliases.brf`, visibility fixtures |
| Literal colons / bracket indices | `test_parser`, `test_sema`, `test_cli`, `errors/old-literal-colons.brf`, `errors/old-indexes.brf` |
| Angle type/call arguments / sized arrays | Nested composable fixes and repeat immutable-token parsing in `test_parser`; blocked lowering in `test_sema`; CLI and old-* fixtures |
| Sym wrappers / parenthesized payloads | Trivia-preserving union/type/value/pattern fixes in `test_parser`, `test_cli_ast` and native rejection fixtures |
| Implicit Self | Ordinary lookup first, concrete-owner fixes and no cascades in `test_structs`, `test_cli_ast`, `errors/old-self-types.brf` |
| Separate impl / trait / ovr prefixes | Parser/CLI/native rejection; ordinary-name controls; no guessed impl relocation |
| Name casing / wrong call forms | Exact spans, LF/CRLF fixes and safe no-fix cases; receiver classification, permissions, SCC/recheck/lowering controls in `test_structs` and CLI tests |

Recovery nodes are not a second accepted grammar. Diagnostic-only angle closer
helpers remain for fix composition; accepted calls do not use speculative angle
parsing. Parser errors prevent resolution/lowering/C publication. Ordinary
`Sym`, `Self`, `type`, `impl`, `trait` and `ovr` names are valid in their name
roles; retired-prefix recognition does not reserve them again.

A separate final probe exercises **22 old/wrong-form sources in LF and CRLF**:
**168 CLI checks** across dump-ast where applicable, emit-c, build and run. Each
requires status 1, empty output, no executable, the intended diagnostic, the
expected fix or deliberate absence of one, and unchanged source bytes. It covers
nested angles, payload types/values/patterns and both wrong call directions.
The probe's initial guessed alias/ovr/array message text was corrected to the
existing exact diagnostics; no compiler change was needed.

Missing receivers, effectful discarded receivers, uncertain aliases/modules,
wildcard names and impl relocation receive no invented edits. Existing C tests
apply certain structured fixes, reparse/recheck, preserve comments and LF/CRLF,
and verify source/token immutability. The reviewed wrong-call comma trim still
removes only spaces/tabs, never line breaks.

## 3. Every current document fence parses or is explicitly classified

The hash-guarded parser inventory covers **13 current chapters / 239 fences**:

- **157 successful whole-fence parses** and **five intentional syntax
  rejections**, each with one error on the document's Compiler error line;
- **77 fenced listings**: 12 await N4/N5, 43 later features and 22 incomplete or
  mixed-context fragments;
- **two unfenced listings**: overview and the whole raw-base reference sheet.

There is no listing blocked only by N1 syntax. An independent reader checks
all 239 fences against generated source bytes and the allowed whole-source
contexts. No missing names/APIs/bodies, extraction, split alternatives or local
stand-ins are supplied. The parser manifest guards missing/duplicate chapters
and classifications, stale hashes/counts, invalid indices/reasons and wrappers.

All five N1 audit entry points exist for the rules available today:

| Audit | Whole checked fences | Native listings | Independent authored programs |
|---|---:|---:|---:|
| functions | 6 / 27 | 21 | 8 prose (14 total cases) |
| types | 8 / 28 | 20 | 12 prose (20 total cases) |
| syntax | 7 / 35 | 28 | 13 prose (20 total cases) |
| modules | 5 / 19 | 14 | 15 prose (20 total cases) |
| raw base | 0 / 0 | One whole unfenced sheet | Six existing full examples |

The 26 checked fences are unchanged sources, with minimal entry/observer
context or exact rejection contracts. Listings remain unaccepted; authored
prose programs do not fill a quoted fence's missing context. The 323-line raw
sheet is saved byte-for-byte, never fed to the native runner or extracted into
passing fragments. Six example prefixes are likewise checked byte-for-byte.
Shared native integrity checks have two positives and 27 independent negatives,
including exact diagnostics/fixes, raw bytes and example inventory/hash/counts.

No active test reads `docs/v0/`. The 13 old executable audits, registrations and
spelling bridges remain retired. Historical Markdown is evidence only; retiring
those generated cases is not claimed to preserve full document/API coverage.

## Validation and completion decision

Fresh builds used `-Werror`, the project compiler/sanitizer flags and the full
suite (`ctest --output-on-failure --parallel 4`):

| Configuration | Result |
|---|---:|
| GCC Debug ASan/UBSan | 37/37 |
| Clang Debug ASan/UBSan | 37/37 |
| GCC Release | 37/37 |
| GCC Debug, sanitizers OFF | 31/31 |

Pinned clang-format 23.1.3 checks all tracked C/headers. Diff, edited prose
widths, plan design/table preservation and the 23-ending CRLF fixture are also
checked. Evidence and standalone probes remain ignored under
`build/validation/n1-final.20261106T202748Z/`.

N1's three completion criteria and its five scoped audit deliverables are met.
This sign-off combines implementation/test inspection, historical expectations,
independent rejection/source checks and fresh builds, not test counts alone.
No production compiler/runtime, Brief fixture/example, expectation marker or
frozen language document changes in this review.

Remaining limitations belong to later milestones or bootstrap scope: N2 kind
outcomes/propagation/exact returns/entry/formatting; N3 delayed/linear handling;
N4 bindings/loops/shifts; N5 interpolation; N6 remaining native inventories;
modules across files, traits/conformance, collections, merged/typed/refutable
matching, generics and other unavailable APIs. Multiple :ok payloads retain the
explicit single-result ABI boundary. The next-version plan is not complete.
Stop here for review; do not start N2 as part of this change.
