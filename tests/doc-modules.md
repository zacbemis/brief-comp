# Native acceptance audit: `lang-modules.txt`

`test_doc_modules.cmake` reads all **17 fences** directly from the chapter.
**Eight are represented in 30 native/diagnostic cases; nine are deferred.**
Fences **3, 4 and 15 are partial**. The test's name identifies the chapter, not
implemented module support: every generated program is one source file.

## Fence classification

| Fence | Scope and coverage |
|---|---|
| 1 | Original public three-field `User` declaration; instantiate it and observe all fields. This is local struct support, not a test of publication or default field privacy across files. |
| 2 | Run the comma-separated and newline-separated literals as separate alternatives, each with the original declaration and a function wrapper. Observe every field. Derived variants require exact missing, duplicate, unknown and wrong-type field diagnostics. |
| 3 | **Partial.** Keep the complete two-field declaration and associated `new` body unchanged and call it locally with an owned name. The `file: User.brf` comment establishes no filename/type relationship, discovery or privacy behavior. |
| 4 | **Partial.** Retain the original `User.new("Bob", 20)` call with fence 3's definitions, bind its result and inspect its fields. Remove exactly the `User.VERSION` and `User.HasUser` lines: constants, traits and qualified module items are deferred. |
| 5 | **Deferred.** External-module field access/initialization restrictions and public API access. The duplicate `describe`/`makeGuest` alternatives cannot establish privacy by being moved into one file. Local constructor/getter behavior is covered separately, not as this fence's privacy test. |
| 6 | **Deferred.** Automatic `User`/`Json` module discovery, `User.HasUser` conformance and real `Json.parse`. Its local composition/getter subexpressions already have first-version coverage in the running-sample audit; they are not a substitute for this example's discovery/conformance rules. |
| 7 | **Deferred.** Project/standard-library folder paths, module resolution and HTTP API. |
| 8 | **Deferred.** Project/standard-library module-name collision diagnostic. An unknown-name error would not validate ambiguity detection. |
| 9 | **Deferred.** Module-specific misspelling diagnostic. Do not substitute the current single-file unknown-associated-function error. |
| 10 | **Deferred.** Project-path import and shortened module name. |
| 11 | **Deferred.** Standard-library import disambiguating project/library `Json`. |
| 12 | **Deferred.** Unqualified versus module-qualified trait lookup/conformance. |
| 13 | **Deferred.** Cross-file module dependency-cycle rejection. A local nominal-type graph or call graph is not a module dependency graph. |
| 14 | Original invalid top-level `print` plus valid `main`; require the current exact parser error. Separately remove only the invalid line and run the retained `main` body, printing `Hello`. The file label is a comment, not a module lookup test. |
| 15 | **Partial.** Retain the complete entry body with deterministic local `File.read` success/failure implementations. Check owned text output, exact `:notFound("data.txt")` entry error and status 1. No files are opened and no library is discovered. |
| 16 | Original complete `main`, using the original fallible constructor from fence 17 and a local public `getName` implementation supplying its omitted context. Check `!` success and `Bob`. Neither local helper placement nor `pub` proves cross-file access. |
| 17 | Complete original validating constructor inside an ordinary local `impl User`, with the two-field declaration extracted from fence 3. Match calls at ages 17, 18 and 20; require the rejected payload and both successful values. Validation belongs to this function, not a compiler-enforced global invariant on all `User` values. |

## Additional guarded prose contracts

These are explicitly representative scenarios (index zero), not unchanged
programs extracted from prose:

- Field initializers run in written order: initialize version, age, then name,
  opposite declaration order, with observable calls and an allocated name.
- Infer a nominal struct return through forward functions; keep the returned
  object and its allocated field alive after the creator returns.
- Use `Self` in parameter/return annotations, a literal and an associated call;
  call the explicit receiver both as `value.readName()` and
  `User.readName(value)`. Keep a returned string snapshot across an `&Self`
  mutation and observe the update through a shared object alias.
- Reject a bare field and an unqualified associated `new` call: there is no
  implicit receiver or implicit associated-call qualification. The latter test
  has a valid `User.new`, but deliberately no module-level `new` function.
- Reject mutation through an ordinary receiver and rebinding of an `&Self`
  parameter. `&` grants data mutation, not parameter rebinding.
- Discard integer 23 (not exit status 23), unit, an allocated string, an owned
  struct and a data-only symbol at entry, exiting zero. A full written outcome
  returning `:ok` also exits zero. Data-only symbols remain ordinary results;
  they are not errors merely because their representation has a tag.
- Require `main()` and reject parameters. The missing-entry expectation is on
  generated line 1 because the whole-program diagnostic attaches at byte zero.

## Scaffolding, ownership and diagnostic boundaries

`File` is an empty local nominal type with an ordinary associated `read` function,
not a module or resource abstraction. Its typed result is
`Sym<:ok(str), :notFound(str)>`. The success allocates `"contents:" + path`; the
failure allocates `path + ""`. Thus text/error payloads must survive their
creator's cleanup. The original entry code is unchanged, except its illustrative
`// error: :notFound(...)` comment is reworded so the C fixture runner does not
mistake expected runtime stderr for a compile-error directive. Expected stderr
is registered separately and exactly. This models only this example's selected
success/failure, not a finalized filesystem API.

The local `getName` returns the stored string. The constructor is not replaced
with a stand-in: its original body and error payload remain intact. Only the
surrounding `impl`/type/caller context is supplied. The two literal alternatives
are separated at their guarded blank-line boundary rather than renamed and
executed as duplicate bindings in one scope.

Normal and `--debug` sanitizer runs use the C program runner, including its live
owner checks. They observe behavior and cleanup, not exact transient ARC counts,
count-elimination optimizations, cycle collection or cross-file visibility.

The top-level `print("Hello")` currently reports **`Expected a parameter name.`**:
the parser interprets the name and `(` as the start of a function declaration.
This rejects the intended invalid source but does not explain that statements
belong inside a function. The audit pins the message and records the actionable
diagnostic gap; no compiler/runtime changes were needed for this slice.

Fence counts, unique adapters, separate literal boundaries, selected local and
deferred prose rules, deferred-fence subject anchors, generated case names and
the 30-case total are guarded. All fences have exactly one classification.
Module filenames, finding/loading, paths/imports, collision/misspelling messages,
privacy, external impls, dependency listings/cycles, traits, constants and real
file/JSON/network APIs remain explicitly outside this audit's native claims.
