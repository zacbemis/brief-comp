# M6 acceptance slice: `docs/lang-ex-base.txt`

This chapter is a **raw running syntax sample**, not a fenced Markdown chapter
or a complete single-source program. It combines notation, unrelated local
fragments, intentional errors and proposed multiple-file APIs. Compiling the
whole file and accepting any failure would not establish acceptance.

`test_doc_base.cmake` reads it directly, guards **zero fences**, and consumes the
entire text into **38 contiguous named spans**: **20 tested, six partial, four
notation/prose references and eight deferred**. No bytes between selected
fragments are omitted from the classification. **38 native/diagnostic cases**
exercise the in-scope material and selected derived counterparts.

```sh
ctest --test-dir build -L doc-base --output-on-failure
```

The normal and sanitized variants use the shared C runner. They require exact
stdout/status, every diagnostic message/source line, source-located panics,
no sanitizer reports and clean debug ownership exits. Boundaries and
subfragment anchors are unique; span kinds/names, complete method inventories
of partial spans, expression/type-list counts, complete source consumption,
case names/count and coverage of every tested/partial span are guarded.
Generated files live in `build/tests/test_doc_base-fixtures/programs/`, with a
separate directory for the sanitized variant. They are not checked-in fixtures.

## Complete raw-span manifest

The boundaries follow source order, so these classifications cover comments,
blank lines and outer contexts as well as selected declarations/statements.

| # | Span | Classification and cases |
|---|---|---|
| 1 | `comments` | Tested: `comments` retains the comments and observes newline-separated statements. |
| 2 | `types` | Reference: `bool x` through `str x` are type/name notation, not valid Brief declarations. `primitive-reference` derives correctly annotated probes from all eleven original type/name lines. It does not claim that the notation itself compiles. |
| 3 | `array` | Deferred: fixed-array type notation `int[5]`. |
| 4 | `bindings` | Reference: bare `let`/`var` keyword/comment lines are not complete declarations. `annotated-bindings`, `updates` and the object-alias scenario exercise the stated binding/data distinction. |
| 5 | `const-keyword` | Deferred: `const` and deep immutable data. |
| 6 | `typed` | Tested: `annotated-bindings` retains both declarations; a wide `lng` expression, binary32 division and `var` reassignment make their types/mutability observable. |
| 7 | `defaults` | Tested: `inferred-bindings` retains both declarations and checks the default domains through `int`/`dub` parameter contracts. |
| 8 | `implicit` | Tested: `implicit-conversion` preserves the original invalid annotation and requires its binding-contract error. It is isolated from the following duplicate-name alternative. |
| 9 | `conversions` | Tested: `conversions` retains the direct/pipeline/text calls and observes all results. `invalid-conversion` changes only the text input, requiring the exact propagated entry error and cleanup of the earlier owned string. |
| 10 | `overflow` | Reference: `integer-overflow` applies the original `x++` statement to maximum `int`, requiring overflow and the operation's Brief source line. |
| 11 | `borrow` | Reference: standalone `&` is annotation notation, not an expression. `retaining` uses the unchanged writable parameter annotations/body from the sample. |
| 12 | `animal` | Tested: `animal` retains the nominal struct; a local `Species` symbol alias supplies its omitted type context. Observe both fields. |
| 13 | `color` | Tested: `color` retains the named symbol set and exercises all three tags. No enum implementation or listing-order comparison is inferred. |
| 14 | `function` | Tested: `addition` retains the complete written-return function; direct and pipeline calls both produce 7. |
| 15 | `lambda` | Deferred: function values/lambdas and the illustrative shadowing binding. |
| 16 | `arithmetic` | Tested: `arithmetic` wraps every complete original expression in `print(str(...))`, observing all four results. |
| 17 | `updates` | Tested: `updates` retains every statement and observes each target immediately, with nontrivial multiplication/division inputs. `integer-overflow` uses the same original update list. |
| 18 | `logic` | Tested: `logic` retains both expressions and exercises every boolean input pair. |
| 19 | `precedence` | Tested: `pipeline-precedence` preserves the whole original expression/comment, exercising both sides of the threshold with a typed local `double`. |
| 20 | `chained` | Tested: `chained-comparison` preserves the invalid expression; replace only its shorthand `// error:` text with the exact current diagnostic, rather than adding a second expectation. |
| 21 | `conditional` | Tested: `conditional` retains all branches and runs less/equal/more inputs. |
| 22 | `while` | Tested: `while` retains the loop and observes entered/zero-iteration cases from 7, 10 and 11. |
| 23 | `range` | Tested: `range-jumps` retains all original statements/comments. Added prints observe visits, continue at 3, skipped tail work, break at 8 and absence of iteration 9. |
| 24 | `unit` | Tested: `logging` executes the unchanged function with a heap string. `tail-written`/`tail-inferred` are explicitly derived counterparts that make the adjacent tail-discard/return prose observable. |
| 25 | `user-module` | Deferred: file naming, automatic module discovery and privacy claims. |
| 26 | `user` | **Partial:** retain `User`, `Tier`, concrete `getUser`, transplanted getter bodies, constructor and `fromJson` body. `user-methods`, `user-constructor`, `user-from-json` and dependent cases run. Trait declaration/abstract requirements/conformance, module visibility, shared `const VERSION` and `toJson` remain deferred. See adapters below. |
| 27 | `admin-module` | Deferred: automatic `User` discovery and imported/qualified namespace rules. |
| 28 | `admin` | **Partial:** `admin` retains the nominal fields, getter/constructor bodies and implicit success/`?`. Localize only `User.Tier`, replace the trait impl header with ordinary `impl`, and omit only `ovr`. No module, trait or override-resolution claim is made. |
| 29 | `collection-module` | Deferred: separate `UserCollection.brf` module context. |
| 30 | `collection` | **Partial:** `retaining` executes the unchanged `pub add(...): ()` method on a documented one-slot stand-in. The actual `Vec` fields/constructor, JSON/collection iteration, serialization/select, find/remove/count/where and lambda bodies remain deferred. |
| 31 | `module-cycle` | Deferred: multi-file dependency-cycle rules. This is not a runtime ARC-cycle test. |
| 32 | `entry` | Tested: `entry` retains the complete `main` and prints Bob using localized first-version definitions. `top-level-binding` checks the adjacent declarations-only rule with an isolated original binding line. This does not validate the original multi-file program. |
| 33 | `constructors` | **Partial:** `construct-propagate`, `construct-bang`, `construct-handled`, `construct-panic` retain construction/handling expressions, replacing only the outer `const` binding kind with `let` and varying age inputs. Propagation and match forms exercise both outcomes; `!` has success and panic/location cases. Const permissions remain deferred. |
| 34 | `age-message` | **Partial:** `age-message` retains every original match arm and observes all three messages. Only `const` becomes `let`; this is pattern/result acceptance, not const support. |
| 35 | `calls` | Tested: `call-forms` observes all three original calls with whole-expression `print(...)` wrappers. `invalid-method` checks that the module-only `save` is not accepted as a receiver method. |
| 36 | `pipeline` | **Partial:** `pipeline-handling` retains both stages and `?` operators, changing only `const` to `let`. Deterministic local parse/validate functions exercise success and both failures; effects prove stage order/skipping. This is not JSON or const support. |
| 37 | `aliases` | Tested: `shared-object` retains both original bindings; mutations through each alias are visible through the other. |
| 38 | `clone-const` | Deferred: `.clone()`, independent-object creation, both frozen-binding alternatives and const-to-let restrictions. Their documented errors must not be approximated by an unsupported-const error. |

## Partial-span adapters and scaffolding

### Localized User and Admin

The original `pub User` and `pub type Tier` declarations are retained in one
source file. The concrete `getUser` method and the trait's complete `getName`,
`getAge` and `getAgeRange` bodies become ordinary methods under `impl User`.
`Self` remains unchanged and resolves in that first-version impl context.
This deliberately does **not** implement trait requirements, conformance,
automatic forwarding, modules or privacy.

The constructor keeps its complete original body. Its unsupported shared
`pub const VERSION: flt = 0.5` declaration is adapted to an ordinary local
`let VERSION: flt = 0.5` inside that constructor. The spelling, type and literal
are guarded; there is no claim of global/static constant semantics. Constructor
cases observe name, age, version and rejection below 18. Getter/classification
cases use direct struct literals to exercise negative/ordinary/old ages without
pretending rejected ages passed the constructor.

The complete `fromJson` body runs unchanged. Its header localizes `Json.Error`
to `JsonError`. A deterministic nominal `Json` record supplies `getStr`/`getInt`,
with explicit missing-field outcomes and visible call effects. Success, missing
name, missing age and constructor rejection all run, checking propagation and
skipped work. These are field-access stand-ins, not a JSON parser/library or
module-qualified error-set implementation. The original `toJson` method is
excluded, not replaced with a fabricated serializer.

Admin keeps its original initialization/constructor/getter bodies. The field
annotation `User.Tier` becomes the retained local `Tier`; `impl Admin:
User.HasUser` becomes ordinary `impl Admin`; `ovr` is omitted from the getter.
Cases check success, composed User data, `:tier1` and propagated constructor
failure. They do not establish override or imported-trait behavior.

All eight User-span method signatures (including the abstract trait signature),
all three Admin methods and all eight collection methods are inventory-guarded,
including omitted methods. Adding an unclassified method cannot silently pass
as the same partial audit.

### Retention and result handling

Only the collection's original retaining method is transplanted. The local
`UserCollection` owns a `UserSlot` and a scalar version; `UserSlot.add` stores
a writable User and returns an allocated string. The original written-unit
method discards that owned result. A helper creates/adds a heap-backed User;
its local owner ends before the caller observes the retained name. No vector,
collection constructor, length/search/filter/removal/iteration, generic API,
or serializer has been implemented by this stand-in.

The later constructor/message/pipeline blocks use `const` in the original.
Their binding-keyword adapters establish only the in-scope expressions and
control flow. Const's alias restrictions/deep immutability are explicitly not
covered. The final clone/frozen examples are excluded entirely because their
central behavior depends on those deferred features.

The `logging` function's original body returns only the unit result of `print`.
To observe its adjacent prose, two **derived** cases replace that call with a
local `echo` that prints and returns an owned string. One retains written `()`;
the other removes that annotation. The first discards the tail with clean
ownership; the second returns and prints it. Neither is described as an
unchanged original function body.

Other contexts are ordinary typed inputs and local functions/types. Whole-value
observers do not change grouping inside source expressions. Loop instrumentation
adds effects without removing/reordering any original jump or condition.

## Remaining scope and diagnostics

Only the in-scope implicit-conversion and chained-comparison errors are native
acceptance of documented error examples here. The separate derived wrong-method
and top-level-binding cases require their specific diagnostics too. Const errors
at the end remain deferred with their features; parser rejection of unsupported
syntax is not acceptance of their intended semantics.

The wrong-method and top-level-declaration diagnostics are currently generic
and are listed in the tracker's diagnostic gaps; there is no claim that all M6
diagnostic finish is complete. Acyclic native
objects and leak-free debug exits do not establish cycle collection, ARC count
elimination, deep const, identity comparison or clone behavior. Independent
parser, checker, IR/RC, arithmetic, conversion, matching and ownership tests stay
in place. No compiler or runtime changes were needed for this slice.
